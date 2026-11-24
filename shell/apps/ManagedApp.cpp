#include "ManagedApp.h"

#include <csignal>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QGuiApplication>
#include <QLoggingCategory>

#include <cstdlib>
#include <memory>

Q_LOGGING_CATEGORY(lcManagedApp, "tvshell.app")

namespace {

constexpr int WindowPollMs = 500;

const QString Systemd = QStringLiteral("org.freedesktop.systemd1");
const QString SystemdPath = QStringLiteral("/org/freedesktop/systemd1");
const QString SystemdManager = QStringLiteral("org.freedesktop.systemd1.Manager");
const QString SystemdUnit = QStringLiteral("org.freedesktop.systemd1.Unit");
const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");
const QString MprisPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString MprisPlayer = QStringLiteral("org.mpris.MediaPlayer2.Player");

template<typename T>
using XcbReply = std::unique_ptr<T, decltype(&std::free)>;

template<typename T>
XcbReply<T> reply(T *raw)
{
    return XcbReply<T>(raw, &std::free);
}

QDBusMessage systemdCall(const QString &method)
{
    return QDBusMessage::createMethodCall(Systemd, SystemdPath, SystemdManager, method);
}

QDBusMessage getProperty(const QString &service, const QString &path, const QString &interface, const QString &name)
{
    auto call = QDBusMessage::createMethodCall(service, path, Properties, QStringLiteral("Get"));
    call << interface << name;
    return call;
}

} // namespace

ManagedApp::ManagedApp(QObject *parent)
    : QObject(parent)
{
    // No X11 (the "offscreen" platform in a test run): the service can still be followed, but
    // there is never a window to show.
    if (auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>(); x11 && x11->connection()) {
        m_connection = x11->connection();
        m_root = xcb_setup_roots_iterator(xcb_get_setup(m_connection)).data->root;
    }
    m_windowPoll.setInterval(WindowPollMs);
    connect(&m_windowPoll, &QTimer::timeout, this, &ManagedApp::checkWindow);

    QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                          QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameOwnerChanged"),
                                          this, SLOT(nameOwnerChanged(QString, QString, QString)));
}

void ManagedApp::setUnit(const QString &unit)
{
    if (m_unit == unit)
        return;
    m_unit = unit;
    emit unitChanged();
    watchUnit();
}

void ManagedApp::setWindowClass(const QString &windowClass)
{
    if (m_windowClass == windowClass)
        return;
    m_windowClass = windowClass;
    emit windowClassChanged();
}

void ManagedApp::setAppId(quint32 appId)
{
    if (m_appId == appId)
        return;
    m_appId = appId;
    emit appIdChanged();
}

void ManagedApp::setMprisName(const QString &name)
{
    if (m_mprisName == name)
        return;
    m_mprisName = name;
    emit mprisNameChanged();
    findPlayer();
}

void ManagedApp::start()
{
    m_stopping = false;
    if (m_running || m_unit.isEmpty())
        return;
    qCInfo(lcManagedApp) << "starting" << m_unit;
    auto call = systemdCall(QStringLiteral("StartUnit"));
    call << m_unit << QStringLiteral("replace");
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        if (finished->isError())
            qCWarning(lcManagedApp) << "could not start" << m_unit << ":" << finished->error().message();
    });
}

void ManagedApp::quit()
{
    if (!m_running || m_unit.isEmpty())
        return;
    qCInfo(lcManagedApp) << "stopping" << m_unit;
    m_stopping = true;
    auto call = systemdCall(QStringLiteral("StopUnit"));
    call << m_unit << QStringLiteral("replace");
    QDBusConnection::sessionBus().asyncCall(call);
}

// For a program that doesn't end when asked: every process of the unit is killed.
void ManagedApp::kill()
{
    if (!m_running || m_unit.isEmpty())
        return;
    qCInfo(lcManagedApp) << "killing" << m_unit;
    m_stopping = true;
    auto call = systemdCall(QStringLiteral("KillUnit"));
    call << m_unit << QStringLiteral("all") << qint32(SIGKILL);
    QDBusConnection::sessionBus().asyncCall(call);
}

void ManagedApp::pause()
{
    if (m_player.isEmpty() || !m_playing)
        return;
    qCInfo(lcManagedApp) << "pausing" << m_unit;
    QDBusConnection::sessionBus().asyncCall(
        QDBusMessage::createMethodCall(m_player, MprisPath, MprisPlayer, QStringLiteral("Pause")));
}

void ManagedApp::setAllWindows(bool all)
{
    if (m_allWindows == all)
        return;
    m_allWindows = all;
    emit allWindowsChanged();
}

// The other half of pause(): a program that was paused when it was left carries on.
void ManagedApp::resume()
{
    if (m_player.isEmpty() || m_playing)
        return;
    qCInfo(lcManagedApp) << "continuing" << m_unit;
    QDBusConnection::sessionBus().asyncCall(
        QDBusMessage::createMethodCall(m_player, MprisPath, MprisPlayer, QStringLiteral("Play")));
}

void ManagedApp::watchUnit()
{
    if (m_unit.isEmpty())
        return;
    auto bus = QDBusConnection::sessionBus();
    // systemd only sends change signals to those who asked.
    bus.asyncCall(systemdCall(QStringLiteral("Subscribe")));
    auto call = systemdCall(QStringLiteral("LoadUnit"));
    call << m_unit;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusPendingReply<QDBusObjectPath> loaded = *finished;
        if (loaded.isError()) {
            qCWarning(lcManagedApp) << "no unit" << m_unit << ":" << loaded.error().message();
            return;
        }
        m_unitPath = loaded.value();
        QDBusConnection::sessionBus().connect(Systemd, m_unitPath.path(), Properties, QStringLiteral("PropertiesChanged"),
                                              this, SLOT(unitPropertiesChanged()));
        readUnitState();
    });
}

void ManagedApp::unitPropertiesChanged()
{
    readUnitState();
}

void ManagedApp::readUnitState()
{
    if (m_unitPath.path().isEmpty())
        return;
    auto call = getProperty(Systemd, m_unitPath.path(), SystemdUnit, QStringLiteral("ActiveState"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusPendingReply<QDBusVariant> state = *finished;
        if (state.isError())
            return;
        const QString active = state.value().variant().toString();
        setRunning(active == u"active" || active == u"activating" || active == u"reloading");
    });
}

void ManagedApp::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    qCInfo(lcManagedApp) << m_unit << (running ? "is running" : "is not running");
    if (running) {
        m_windowPoll.start();
        checkWindow();
        findPlayer();
    } else {
        m_windowPoll.stop();
        m_tagged.clear();
        setWindow(0);
        setPlayer(QString());
    }
    emit runningChanged();
    if (!running && !m_stopping)
        emit exited();
    m_stopping = false;
}

void ManagedApp::checkWindow()
{
    if (!m_connection)
        return;
    // A program that opens windows as it goes (a dialog the rest of it then waits behind)
    // has every one of them tagged, or gamescope doesn't show them.
    if (m_allWindows) {
        const QList<xcb_window_t> windows = classWindows();
        for (xcb_window_t window : windows) {
            if (!m_tagged.contains(window)) {
                m_tagged.insert(window);
                tag(window);
            }
        }
        m_tagged.intersect(QSet<xcb_window_t>(windows.cbegin(), windows.cend()));
    }
    if (m_window) {
        // Still there?
        auto attributes = reply(xcb_get_window_attributes_reply(
            m_connection, xcb_get_window_attributes(m_connection, m_window), nullptr));
        if (attributes)
            return;
        setWindow(0);
    }
    setWindow(findWindow());
}

// The top-level windows of the program's class.
QList<xcb_window_t> ManagedApp::classWindows() const
{
    QList<xcb_window_t> windows;
    if (m_windowClass.isEmpty())
        return windows;
    auto tree = reply(xcb_query_tree_reply(m_connection, xcb_query_tree(m_connection, m_root), nullptr));
    if (!tree)
        return windows;
    const QByteArray wanted = m_windowClass.toUtf8();
    const xcb_window_t *children = xcb_query_tree_children(tree.get());
    for (int i = 0; i < xcb_query_tree_children_length(tree.get()); ++i) {
        // WM_CLASS is "instance\0class\0".
        auto property = reply(xcb_get_property_reply(
            m_connection,
            xcb_get_property(m_connection, 0, children[i], XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 0, 64), nullptr));
        if (!property || xcb_get_property_value_length(property.get()) == 0)
            continue;
        const QByteArray value(static_cast<const char *>(xcb_get_property_value(property.get())),
                               xcb_get_property_value_length(property.get()));
        if (value.split('\0').contains(wanted))
            windows.append(children[i]);
    }
    return windows;
}

xcb_window_t ManagedApp::findWindow() const
{
    xcb_window_t best = 0;
    quint32 bestArea = 0;
    const QList<xcb_window_t> windows = classWindows();
    for (xcb_window_t window : windows) {
        // The program also has tiny helper windows of the same class: take the largest.
        auto geometry = reply(xcb_get_geometry_reply(m_connection, xcb_get_geometry(m_connection, window), nullptr));
        if (!geometry)
            continue;
        const quint32 area = quint32(geometry->width) * geometry->height;
        if (area > bestArea && area > 100) {
            best = window;
            bestArea = area;
        }
    }
    return best;
}

void ManagedApp::tag(xcb_window_t window)
{
    const quint32 id = m_appId;
    const char name[] = "STEAM_GAME";
    auto atom = reply(xcb_intern_atom_reply(
        m_connection, xcb_intern_atom(m_connection, 0, sizeof(name) - 1, name), nullptr));
    if (atom) {
        xcb_change_property(m_connection, XCB_PROP_MODE_REPLACE, window, atom->atom, XCB_ATOM_CARDINAL, 32, 1, &id);
        xcb_flush(m_connection);
    }
}

void ManagedApp::setWindow(xcb_window_t window)
{
    if (m_window == window)
        return;
    m_window = window;
    if (window) {
        tag(window);
        qCInfo(lcManagedApp) << m_unit << "window" << Qt::hex << window << Qt::dec << "tagged with app ID" << m_appId;
    } else {
        qCInfo(lcManagedApp) << m_unit << "window gone";
    }
    emit readyChanged();
}

void ManagedApp::findPlayer()
{
    if (m_mprisName.isEmpty() || !m_running)
        return;
    auto *bus = QDBusConnection::sessionBus().interface();
    if (!bus)
        return;
    const QStringList names = bus->registeredServiceNames();
    for (const QString &name : names) {
        if (name.startsWith(m_mprisName)) {
            setPlayer(name);
            return;
        }
    }
}

void ManagedApp::nameOwnerChanged(const QString &name, const QString &, const QString &newOwner)
{
    if (m_mprisName.isEmpty() || !name.startsWith(m_mprisName))
        return;
    if (newOwner.isEmpty()) {
        if (name == m_player)
            setPlayer(QString());
    } else {
        setPlayer(name);
    }
}

void ManagedApp::setPlayer(const QString &busName)
{
    if (m_player == busName)
        return;
    auto bus = QDBusConnection::sessionBus();
    if (!m_player.isEmpty()) {
        bus.disconnect(m_player, MprisPath, Properties, QStringLiteral("PropertiesChanged"), this,
                       SLOT(playerPropertiesChanged(QString, QVariantMap, QStringList)));
    }
    m_player = busName;
    if (m_player.isEmpty()) {
        setPlaying(false);
        return;
    }
    bus.connect(m_player, MprisPath, Properties, QStringLiteral("PropertiesChanged"), this,
                SLOT(playerPropertiesChanged(QString, QVariantMap, QStringList)));
    auto *watcher = new QDBusPendingCallWatcher(
        bus.asyncCall(getProperty(m_player, MprisPath, MprisPlayer, QStringLiteral("PlaybackStatus"))), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusPendingReply<QDBusVariant> status = *finished;
        if (!status.isError())
            setPlaying(status.value().variant().toString() == u"Playing");
    });
}

void ManagedApp::playerPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface != MprisPlayer)
        return;
    const auto status = changed.constFind(QStringLiteral("PlaybackStatus"));
    if (status != changed.constEnd())
        setPlaying(status->toString() == u"Playing");
}

void ManagedApp::setPlaying(bool playing)
{
    if (m_playing == playing)
        return;
    m_playing = playing;
    qCInfo(lcManagedApp) << m_unit << (playing ? "is playing" : "is not playing");
    emit playingChanged();
}
