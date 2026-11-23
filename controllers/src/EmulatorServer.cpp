#include "EmulatorServer.h"

#include "DeviceWatcher.h"
#include "JoyCon.h"
#include "JoyConProtocol.h"
#include "Unit.h"
#include "UnitManager.h"

#include <QDir>
#include <QLoggingCategory>
#include <QSocketNotifier>

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcEmulator, "tvcontrollers.emulator")

namespace Emulator = TvControllers::Emulator;

namespace {

constexpr int RetryIntervalMs = 3000;
constexpr int RefreshIntervalMs = 5000;
// The largest message an emulator sends is far smaller.
constexpr int ReceiveSize = 256;

template <typename Message>
QByteArray bytes(const Message &message)
{
    return QByteArray(reinterpret_cast<const char *>(&message), sizeof message);
}

} // namespace

EmulatorServer::EmulatorServer(DeviceWatcher *watcher, UnitManager *units, QObject *parent)
    : QObject(parent)
    , m_watcher(watcher)
    , m_units(units)
{
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR", QStringLiteral("/run/user/%1").arg(::getuid()));
    m_path = runtime + QLatin1Char('/') + QLatin1String(Emulator::SocketDirectory) + QLatin1Char('/')
             + QLatin1String(Emulator::SocketName);

    m_retry.setInterval(RetryIntervalMs);
    connect(&m_retry, &QTimer::timeout, this, &EmulatorServer::listen);
    m_refresh.setInterval(RefreshIntervalMs);
    connect(&m_refresh, &QTimer::timeout, this, &EmulatorServer::announce);

    connect(watcher, &DeviceWatcher::joyConAdded, this, &EmulatorServer::watch);
    connect(watcher, &DeviceWatcher::joyConRemoved, this, &EmulatorServer::forget);
    connect(units, &UnitManager::changed, this, &EmulatorServer::announce);
    const QList<JoyCon *> joyCons = watcher->joyCons();
    for (JoyCon *joyCon : joyCons)
        watch(joyCon);

    listen();
}

EmulatorServer::~EmulatorServer()
{
    const QList<int> clients = m_clients.keys();
    for (int fd : clients)
        drop(fd);
    if (m_listening >= 0) {
        ::close(m_listening);
        ::unlink(m_path.toLocal8Bit().constData());
    }
}

void EmulatorServer::listen()
{
    if (m_listening >= 0)
        return;
    const QString directory = m_path.left(m_path.lastIndexOf(QLatin1Char('/')));
    const QString runtime = directory.left(directory.lastIndexOf(QLatin1Char('/')));
    if (!QDir(runtime).exists() || !QDir().mkpath(directory)) {
        if (!m_retry.isActive())
            qCInfo(lcEmulator).noquote() << "no" << runtime << "yet: waiting for the user's session";
        m_retry.start();
        return;
    }

    const QByteArray path = m_path.toLocal8Bit();
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= int(sizeof address.sun_path)) {
        qCWarning(lcEmulator).noquote() << "the socket's path is too long:" << m_path;
        return;
    }
    std::memcpy(address.sun_path, path.constData(), path.size());

    const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    // A socket left by a run that was killed.
    ::unlink(path.constData());
    if (fd < 0 || ::bind(fd, reinterpret_cast<const sockaddr *>(&address), sizeof address) != 0
        || ::listen(fd, 4) != 0) {
        qCWarning(lcEmulator).noquote() << "cannot listen on" << m_path << ":" << std::strerror(errno);
        if (fd >= 0)
            ::close(fd);
        m_retry.start();
        return;
    }
    m_retry.stop();
    m_listening = fd;
    m_acceptNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_acceptNotifier, &QSocketNotifier::activated, this, &EmulatorServer::accept);
    qCInfo(lcEmulator).noquote() << "emulators connect at" << m_path;
}

void EmulatorServer::accept()
{
    const int fd = ::accept4(m_listening, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0)
        return;
    Client client;
    client.notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(client.notifier, &QSocketNotifier::activated, this, [this, fd] { read(fd); });
    m_clients.insert(fd, client);
}

void EmulatorServer::read(int fd)
{
    char buffer[ReceiveSize];
    for (;;) {
        const ssize_t size = ::recv(fd, buffer, sizeof buffer, 0);
        if (size < 0 && (errno == EAGAIN || errno == EINTR))
            return;
        // Closed, or broken.
        if (size <= 0) {
            drop(fd);
            return;
        }
        if (size < ssize_t(sizeof(Emulator::Header)))
            continue;
        const auto found = m_clients.find(fd);
        if (found == m_clients.end())
            return;
        Client &client = *found;
        Emulator::Header header;
        std::memcpy(&header, buffer, sizeof header);

        if (header.type == Emulator::Type::Hello && size >= ssize_t(sizeof(Emulator::Hello))) {
            Emulator::Hello hello;
            std::memcpy(&hello, buffer, sizeof hello);
            client.name = QString::fromLatin1(hello.name, qstrnlen(hello.name, sizeof hello.name));
            if (header.version != Emulator::Version) {
                qCWarning(lcEmulator).noquote() << client.name << "speaks version" << header.version << "of the protocol, we"
                                                << Emulator::Version << ": refused";
                drop(fd);
                return;
            }
            client.greeted = true;
            qCInfo(lcEmulator).noquote() << client.name << "connected";
            m_refresh.start();
            send(fd, describe());
            continue;
        }
        if (!client.greeted)
            continue;

        if (header.type == Emulator::Type::SetRing && size >= ssize_t(sizeof(Emulator::SetRing))) {
            Emulator::SetRing request;
            std::memcpy(&request, buffer, sizeof request);
            if (JoyCon *target = joyCon(request.id))
                wantRing(fd, target, request.on != 0);
        } else if (header.type == Emulator::Type::Playing && size >= ssize_t(sizeof(Emulator::Playing))) {
            Emulator::Playing playing;
            std::memcpy(&playing, buffer, sizeof playing);
            if (client.playing != (playing.on != 0)) {
                client.playing = playing.on != 0;
                qCInfo(lcEmulator).noquote() << client.name << (client.playing ? "is playing" : "is not playing");
                updatePlaying();
            }
        } else if (header.type == Emulator::Type::SetLights || header.type == Emulator::Type::Rumble) {
            // Lights and rumble from the game: not built yet.
        } else {
            qCDebug(lcEmulator).noquote() << client.name << "sent a message we don't know:" << int(header.type) << "of"
                                          << size << "bytes";
        }
    }
}

void EmulatorServer::drop(int fd)
{
    const auto it = m_clients.constFind(fd);
    if (it == m_clients.constEnd())
        return;
    // Out of the list first: giving up its ring announces a change to every client, and
    // that must not reach this one again (it did, and dropped it a second time from
    // inside the first).
    const Client client = *it;
    m_clients.erase(it);
    // This may be running inside the notifier's own signal: it is deleted later.
    client.notifier->setEnabled(false);
    client.notifier->deleteLater();
    ::close(fd);
    if (client.greeted)
        qCInfo(lcEmulator).noquote() << client.name << "disconnected";
    for (JoyCon *ring : client.rings)
        updateRing(ring);
    updatePlaying();
    if (m_clients.isEmpty())
        m_refresh.stop();
}

void EmulatorServer::wantRing(int fd, JoyCon *joyCon, bool on)
{
    const auto found = m_clients.find(fd);
    if (found == m_clients.end() || on == found->rings.contains(joyCon))
        return;
    if (on)
        found->rings.insert(joyCon);
    else
        found->rings.remove(joyCon);
    qCInfo(lcEmulator).noquote() << found->name << (on ? "wants the ring on" : "no longer wants the ring on")
                                 << joyCon->name() << joyCon->address();
    updateRing(joyCon);
}

void EmulatorServer::updatePlaying()
{
    bool playing = false;
    for (const Client &client : std::as_const(m_clients))
        playing = playing || client.playing;
    if (playing == m_playing)
        return;
    m_playing = playing;
    emit playingChanged(playing);
}

// The ring is read for as long as any connected program wants it.
void EmulatorServer::updateRing(JoyCon *joyCon)
{
    // Gone meanwhile.
    if (!m_ids.contains(joyCon))
        return;
    bool wanted = false;
    for (const Client &client : std::as_const(m_clients))
        wanted = wanted || client.rings.contains(joyCon);
    joyCon->setRingWanted(JoyCon::RingUser::Emulator, wanted);
}

void EmulatorServer::watch(JoyCon *joyCon)
{
    // An id is given once it has answered: before that there is nothing to say about it.
    connect(joyCon, &JoyCon::ready, this, [this, joyCon] { adopt(joyCon); });
    connect(joyCon, &JoyCon::ringChanged, this, &EmulatorServer::announce);
    connect(joyCon, &JoyCon::motion, this,
            [this, joyCon](const JoyCon::MotionReport &motion) { sendInput(joyCon, &motion); });
    // Silent, it sends no reports: its neutral state goes out once, marked.
    connect(joyCon, &JoyCon::quietChanged, this, [this, joyCon] {
        if (joyCon->isQuiet())
            sendInput(joyCon, nullptr);
    });
    if (joyCon->isReady())
        adopt(joyCon);
}

void EmulatorServer::adopt(JoyCon *joyCon)
{
    if (m_ids.contains(joyCon))
        return;
    quint8 id = 0;
    while (m_ids.values().contains(id))
        ++id;
    m_ids.insert(joyCon, id);
    announce();
}

void EmulatorServer::forget(JoyCon *joyCon)
{
    for (Client &client : m_clients)
        client.rings.remove(joyCon);
    if (m_ids.remove(joyCon) > 0)
        announce();
}

JoyCon *EmulatorServer::joyCon(quint8 id) const
{
    return m_ids.key(id, nullptr);
}

QByteArray EmulatorServer::describe() const
{
    Emulator::Controllers message{};
    message.header = {Emulator::Type::Controllers};

    auto add = [this, &message](JoyCon *joyCon, Emulator::Grip grip, int player, JoyCon *partner) {
        const auto id = m_ids.constFind(joyCon);
        if (id == m_ids.constEnd() || message.count >= Emulator::MaxControllers)
            return;
        Emulator::Controller &controller = message.controllers[message.count++];
        controller.id = *id;
        controller.side = joyCon->side() == JoyCon::Side::Left    ? Emulator::Side::Left
                          : joyCon->side() == JoyCon::Side::Right ? Emulator::Side::Right
                                                                  : Emulator::Side::Pro;
        controller.grip = grip;
        controller.player = quint8(player);
        controller.partner = partner ? m_ids.value(partner, Emulator::NoController) : Emulator::NoController;
        controller.battery = quint8(joyCon->state().battery);
        controller.charging = joyCon->state().charging;
        controller.ring = joyCon->hasRing();
        const QByteArray address = QByteArray::fromHex(joyCon->address().remove(QLatin1Char(':')).toLatin1());
        std::memcpy(controller.address, address.constData(), qMin<qsizetype>(address.size(), sizeof controller.address));
    };

    const QList<Unit *> units = m_units->units();
    for (const Unit *unit : units) {
        const QList<JoyCon *> joyCons = unit->joyCons();
        const Emulator::Grip grip = unit->kind() == Unit::Kind::Pair       ? Emulator::Grip::Pair
                                    : unit->kind() == Unit::Kind::Sideways ? Emulator::Grip::Sideways
                                                                           : Emulator::Grip::Pro;
        for (JoyCon *joyCon : joyCons)
            add(joyCon, grip, unit->player(), joyCons.size() == 2 ? joyCons.at(joyCon == joyCons.at(0) ? 1 : 0) : nullptr);
    }
    const QList<JoyCon *> waiting = m_units->unassigned();
    for (JoyCon *joyCon : waiting)
        add(joyCon, Emulator::Grip::Waiting, 0, nullptr);
    return bytes(message);
}

void EmulatorServer::announce()
{
    if (m_clients.isEmpty())
        return;
    const QByteArray described = describe();
    if (described == m_announced)
        return;
    m_announced = described;
    const QList<int> clients = m_clients.keys();
    for (int fd : clients) {
        if (m_clients.value(fd).greeted)
            send(fd, described);
    }
}

void EmulatorServer::sendInput(JoyCon *joyCon, const JoyCon::MotionReport *motion)
{
    if (m_clients.isEmpty())
        return;
    const auto id = m_ids.constFind(joyCon);
    if (id == m_ids.constEnd())
        return;

    Emulator::Input message{};
    message.header = {Emulator::Type::Input};
    message.id = *id;
    message.quiet = joyCon->isQuiet();
    message.ring = Emulator::NoRing;
    if (motion && !joyCon->isQuiet()) {
        if (joyCon->hasRing() && joyCon->lastRingFlex() != JoyCon::NoRingFlex)
            message.ring = qint16(joyCon->lastRingFlex());
        // Home is the TV's.
        message.buttons = joyCon->state().buttons & ~quint32(JoyConProtocol::Home);
        if (joyCon->side() != JoyCon::Side::Right) {
            const JoyCon::Stick left = joyCon->stick(JoyCon::Side::Left);
            message.left[0] = float(left.x);
            message.left[1] = float(left.y);
        }
        if (joyCon->side() != JoyCon::Side::Left) {
            const JoyCon::Stick right = joyCon->stick(JoyCon::Side::Right);
            message.right[0] = float(right.x);
            message.right[1] = float(right.y);
        }
        for (int i = 0; i < Emulator::MotionReadings; ++i) {
            message.motion[i].time = quint64((*motion)[i].time);
            for (int axis = 0; axis < 3; ++axis) {
                message.motion[i].acceleration[axis] = (*motion)[i].accel[axis];
                message.motion[i].rotation[axis] = (*motion)[i].gyro[axis];
            }
        }
    }
    const QByteArray packet = bytes(message);
    const QList<int> clients = m_clients.keys();
    for (int fd : clients) {
        if (m_clients.value(fd).greeted)
            send(fd, packet);
    }
}

void EmulatorServer::send(int fd, const QByteArray &message)
{
    if (::send(fd, message.constData(), message.size(), MSG_DONTWAIT | MSG_NOSIGNAL) >= 0)
        return;
    // A program that doesn't keep up misses a report; one that is gone is dropped.
    if (errno != EAGAIN && errno != EINTR)
        drop(fd);
}
