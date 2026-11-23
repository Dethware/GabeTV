#include "JoyCon.h"

#include "JoyConProtocol.h"

#include <QLoggingCategory>
#include <QSocketNotifier>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcJoyCon, "tvcontrollers.joycon")

using namespace JoyConProtocol;

namespace {

constexpr int ReplyTimeoutMs = 250;
constexpr int MaxTries = 3;
// The first subcommand after a connection: a Joy-Con takes a while to answer anything
// (the node is there before the connection is fully up), so this one is patient: 5 s.
constexpr int MaxFirstTries = 20;
// The lights are read back this long after setting them, and set again if they differ.
constexpr int LightsCheckMs = 400;
// Full reports come every 15 ms; the worst gaps measured on a weak link were 150 ms. This
// long without one and the Joy-Con counts as quiet.
constexpr int QuietMs = 1000;
// A gap between two reports longer than this counts as a late report. Before a switch-off
// the longest gaps measured were 28, 44 and 45 ms (a report or two resent); on the way out
// of reach 252 and 255 ms, with eight or nine reports over 40 ms.
constexpr int LateGapMs = 100;
constexpr int MaxLightChecks = 5;

// Around the stick's centre, as a fraction of its reach, nothing is reported.
constexpr double DeadZone = 0.15;

// A stick is two 12-bit values packed into three bytes.
void unpackStick(const QByteArray &report, int offset, quint16 &x, quint16 &y)
{
    const auto b = [&](int i) { return quint8(report[offset + i]); };
    x = quint16(b(0) | ((b(1) & 0x0f) << 8));
    y = quint16((b(1) >> 4) | (b(2) << 4));
}

double axis(int raw, int center, int below, int above)
{
    const int reach = raw >= center ? above : below;
    return reach > 0 ? qBound(-1.0, double(raw - center) / reach, 1.0) : 0.0;
}

} // namespace

JoyCon::JoyCon(const QString &node, Side side, const QString &address, QObject *parent)
    : QObject(parent)
    , m_node(node)
    , m_side(side)
    , m_address(address)
{
    m_replyTimer.setSingleShot(true);
    m_replyTimer.setInterval(ReplyTimeoutMs);
    connect(&m_replyTimer, &QTimer::timeout, this, &JoyCon::replyTimedOut);
    m_lightsTimer.setSingleShot(true);
    m_lightsTimer.setInterval(LightsCheckMs);
    connect(&m_lightsTimer, &QTimer::timeout, this, &JoyCon::checkLights);
    m_quietTimer.setSingleShot(true);
    m_quietTimer.setInterval(QuietMs);
    connect(&m_quietTimer, &QTimer::timeout, this, [this] {
        const quint16 longest = *std::max_element(m_gaps.begin(), m_gaps.end());
        const auto late = std::count_if(m_gaps.begin(), m_gaps.end(), [](quint16 gap) { return gap > LateGapMs; });
        qCInfo(lcJoyCon).noquote() << name() << m_address << "went quiet; in the second before: longest gap" << longest
                                   << "ms," << late << "late reports:"
                                   << (late == 0 ? "an abrupt stop, taken as switched off" : "a fading link, left alone")
                                   << "- battery" << m_state.battery << "of 4";
        m_quiet = true;
        emit quietChanged();
        if (late == 0)
            emit switchedOff();
    });
}

JoyCon::~JoyCon()
{
    if (m_fd >= 0)
        ::close(m_fd);
}

bool JoyCon::open()
{
    m_fd = ::open(m_node.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0)
        return false;
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &JoyCon::readReports);

    send(DeviceInfo, {}, [this](const QByteArray &data) {
        if (data.size() >= 2)
            m_firmware = QStringLiteral("%1.%2").arg(quint8(data[0])).arg(quint8(data[1]), 2, 10, QLatin1Char('0'));
    });
    // The factory's stick calibration, then the user's own if there is one.
    if (m_side != Side::Right) {
        readCalibration(FactoryLeftStick, false, Side::Left);
        readCalibration(UserLeftStick, true, Side::Left);
    }
    if (m_side != Side::Left) {
        readCalibration(FactoryRightStick, false, Side::Right);
        readCalibration(UserRightStick, true, Side::Right);
    }
    // A Joy-Con starts in the simple mode, which only reports button changes.
    send(SetInputMode, QByteArray(1, char(InputFull)), [this](const QByteArray &) {
        qCInfo(lcJoyCon).noquote() << name() << m_address << "ready, firmware" << m_firmware << "battery"
                                   << m_state.battery << "of 4";
        m_ready = true;
        emit ready();
    });
    return true;
}

QString JoyCon::name() const
{
    switch (m_side) {
    case Side::Left:
        return QStringLiteral("Joy-Con (L)");
    case Side::Right:
        return QStringLiteral("Joy-Con (R)");
    case Side::Both:
        break;
    }
    return QStringLiteral("Pro Controller");
}

void JoyCon::readCalibration(quint32 address, bool user, Side which)
{
    // The user's calibration has two magic bytes in front.
    const int size = StickCalibrationSize + (user ? 2 : 0);
    QByteArray args;
    for (int i = 0; i < 4; ++i)
        args.append(char(address >> (8 * i)));
    args.append(char(size));
    send(ReadMemory, args, [this, size, user, which](const QByteArray &reply) {
        // The reply repeats the address and the length before the data.
        QByteArray data = reply.mid(5, size);
        if (data.size() < size)
            return;
        if (user) {
            if (quint8(data[0]) != UserCalibrationMagic[0] || quint8(data[1]) != UserCalibrationMagic[1])
                return;
            data = data.mid(2);
        }
        // Three pairs of 12-bit values. The left stick has them in the order above, centre,
        // below; the right stick centre, below, above.
        int pairs[3][2];
        for (int i = 0; i < 3; ++i) {
            const auto b = [&](int n) { return quint8(data[3 * i + n]); };
            pairs[i][0] = b(0) | ((b(1) & 0x0f) << 8);
            pairs[i][1] = (b(1) >> 4) | (b(2) << 4);
        }
        const bool left = which == Side::Left;
        const int *above = pairs[left ? 0 : 2];
        const int *center = pairs[left ? 1 : 0];
        const int *below = pairs[left ? 2 : 1];
        (left ? m_leftCalibration : m_rightCalibration) = {center[0], center[1], below[0], below[1], above[0], above[1]};
        qCDebug(lcJoyCon).noquote() << name() << (user ? "user" : "factory") << (left ? "left" : "right")
                                    << "stick calibration: centre" << center[0]
                                    << center[1] << "below" << below[0] << below[1] << "above" << above[0] << above[1];
    });
}

JoyCon::Stick JoyCon::stick(Side which) const
{
    const bool left = which == Side::Left;
    const Calibration &c = left ? m_leftCalibration : m_rightCalibration;
    const double x = axis(left ? m_state.leftX : m_state.rightX, c.centerX, c.belowX, c.aboveX);
    const double y = axis(left ? m_state.leftY : m_state.rightY, c.centerY, c.belowY, c.aboveY);
    const double length = std::hypot(x, y);
    if (length < DeadZone)
        return {};
    // Past the dead zone the position starts from zero again, up to 1 at full reach.
    const double scale = qMin(1.0, (length - DeadZone) / (1.0 - DeadZone)) / length;
    return {x * scale, y * scale};
}

void JoyCon::setPlayerLights(quint8 lit, quint8 flashing)
{
    m_lights = quint8((lit & 0x0f) | ((flashing & 0x0f) << 4));
    m_lightChecks = 0;
    applyLights();
}

void JoyCon::applyLights()
{
    send(SetPlayerLights, QByteArray(1, char(m_lights)), [this](const QByteArray &) { m_lightsTimer.start(); });
}

// A Joy-Con acknowledges the lights and still sometimes ends up dark right after a
// connection, so what it shows is asked for and corrected.
void JoyCon::checkLights()
{
    send(GetPlayerLights, {}, [this](const QByteArray &data) {
        if (data.isEmpty())
            return;
        const quint8 shown = quint8(data[0]);
        if (shown == m_lights) {
            qCDebug(lcJoyCon).noquote() << name() << "lights confirmed:" << Qt::hex << shown;
        } else if (++m_lightChecks <= MaxLightChecks) {
            qCInfo(lcJoyCon).noquote() << name() << "lights are" << Qt::hex << shown << "and should be" << m_lights
                                       << "- setting them again";
            applyLights();
        }
    });
}

void JoyCon::readReports()
{
    char buffer[512];
    for (;;) {
        const ssize_t size = ::read(m_fd, buffer, sizeof buffer);
        if (size > 0) {
            handleReport(QByteArray::fromRawData(buffer, size));
            continue;
        }
        if (size < 0 && (errno == EAGAIN || errno == EINTR))
            return;
        // The node is gone: the Joy-Con disconnected.
        qCInfo(lcJoyCon).noquote() << name() << m_address << "lost:" << (size < 0 ? std::strerror(errno) : "end of file")
                                   << (m_sinceReport.isValid() ? QStringLiteral("- last report %1 ms ago").arg(m_sinceReport.elapsed())
                                                               : QString());
        m_notifier->setEnabled(false);
        m_replyTimer.stop();
        m_lightsTimer.stop();
        m_quietTimer.stop();
        emit lost();
        return;
    }
}

void JoyCon::handleReport(const QByteArray &report)
{
    const quint8 type = quint8(report[0]);
    // Only once full reports are on; before that a Joy-Con is silent between button presses.
    if (m_ready) {
        if (m_quiet) {
            m_quiet = false;
            qCInfo(lcJoyCon).noquote() << name() << m_address << "is back after" << m_sinceReport.elapsed() << "ms";
            emit quietChanged();
        }
        if (m_sinceReport.isValid())
            m_gaps[m_gapIndex++ % m_gaps.size()] = quint16(qMin<qint64>(m_sinceReport.elapsed(), 0xffff));
        m_sinceReport.start();
        m_quietTimer.start();
    }
    if ((type != InputFull && type != InputSubcommandReply) || report.size() < RightStickOffset + 3)
        return;

    State state = m_state;
    const quint8 battery = quint8(report[BatteryOffset]) >> 4;
    state.battery = battery >> 1;
    state.charging = battery & 1;
    state.buttons = quint8(report[ButtonsOffset]) | (quint8(report[ButtonsOffset + 1]) << 8)
                    | (quint8(report[ButtonsOffset + 2]) << 16);
    unpackStick(report, LeftStickOffset, state.leftX, state.leftY);
    unpackStick(report, RightStickOffset, state.rightX, state.rightY);

    if (state != m_state) {
        const quint32 before = m_state.buttons;
        m_state = state;
        if (state.buttons != before)
            emit buttonsChanged(state.buttons & ~before, before & ~state.buttons);
        emit stateChanged();
    }

    const bool ours = type == InputSubcommandReply && m_waiting && report.size() > ReplyDataOffset
                      && quint8(report[ReplyIdOffset]) == m_queue.first().id;
    if (type == InputSubcommandReply && !ours && report.size() > ReplyDataOffset) {
        // Someone else's subcommand was answered: the kernel's driver also talks to the Joy-Con.
        qCDebug(lcJoyCon).noquote() << name() << "answered a subcommand that isn't ours:" << Qt::hex
                                    << quint8(report[ReplyIdOffset]) << "ack" << quint8(report[ReplyAckOffset])
                                    << "data" << report.mid(ReplyDataOffset, 6).toHex(' ');
    }
    if (ours) {
        m_replyTimer.stop();
        m_waiting = false;
        m_answered = true;
        const Pending done = m_queue.takeFirst();
        if (!(quint8(report[ReplyAckOffset]) & 0x80))
            qCWarning(lcJoyCon).noquote() << name() << "refused subcommand" << Qt::hex << done.id;
        else if (done.done)
            done.done(report.mid(ReplyDataOffset));
        sendNext();
    }
}

void JoyCon::send(quint8 subcommand, const QByteArray &args, Reply done)
{
    m_queue.append({subcommand, args, std::move(done)});
    if (!m_waiting)
        sendNext();
}

void JoyCon::sendNext()
{
    // Also reached from a reply's callback that queued something: one at a time.
    if (m_waiting || m_queue.isEmpty() || m_fd < 0)
        return;
    Pending &next = m_queue.first();
    QByteArray packet;
    packet.append(char(OutputSubcommand));
    packet.append(char(m_counter++ & 0x0f));
    packet.append(reinterpret_cast<const char *>(RumbleOff), sizeof RumbleOff);
    packet.append(char(next.id));
    packet.append(next.args);
    ++next.tries;
    qCDebug(lcJoyCon).noquote() << name() << "subcommand" << Qt::hex << next.id << next.args.toHex(' ') << "try"
                                << next.tries;
    if (::write(m_fd, packet.constData(), packet.size()) < 0)
        qCWarning(lcJoyCon).noquote() << name() << "write failed:" << std::strerror(errno);
    m_waiting = true;
    m_replyTimer.start();
}

void JoyCon::replyTimedOut()
{
    m_waiting = false;
    if (m_queue.first().tries >= (m_answered ? MaxTries : MaxFirstTries)) {
        qCWarning(lcJoyCon).noquote() << name() << "no reply to subcommand" << Qt::hex << m_queue.first().id;
        m_queue.removeFirst();
        if (!m_answered) {
            // It never answered at all: its node outlived the connection. Let go of it.
            m_queue.clear();
            m_notifier->setEnabled(false);
            emit lost();
            return;
        }
    }
    sendNext();
}
