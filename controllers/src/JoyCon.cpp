#include "JoyCon.h"

#include "JoyConProtocol.h"

#include <QLoggingCategory>
#include <QtEndian>
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
// How often the rail is asked what is attached, and how many reports in a row without the
// ring's id mean it was taken off (150 ms).
constexpr int RingLookMs = 1000;
// How long a probe waits for the rail's answer (it came within two seconds when tried).
constexpr int RingProbeMs = 5000;
constexpr int RingGoneReports = 10;

// CRC-8, polynomial 7, starting from 0: the second processor's checksum.
quint8 crc8(const QByteArray &data, int from, int count)
{
    quint8 crc = 0;
    for (int i = from; i < from + count; ++i) {
        crc ^= quint8(data[i]);
        for (int bit = 0; bit < 8; ++bit)
            crc = crc & 0x80 ? quint8((crc << 1) ^ 0x07) : quint8(crc << 1);
    }
    return crc;
}
// Up to this many lost reports in a row are filled in (120 ms).
constexpr int MostFilledReports = 8;
// Following the gyroscope's error at rest: see trackRest().
constexpr qint64 RestWindowUs = 1000000;
constexpr float RestTurningSpread = 0.6f; // degrees a second, lowest to highest
constexpr float RestStrengthSpread = 0.01f; // g
constexpr float RestTurningMost = 1.0f; // degrees a second
constexpr float RestFollow = 0.2f; // of the way, each second at rest
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
// Rumble is written only while something wants it (setRumbleWanted), which keeps the
// connection out of sniff mode (main.cpp). Written right after a report, at most this
// often (SDL's figure), again this often while it vibrates (SDL: the actuator stops
// without, within 50 ms), and "off" this many times, since nothing confirms a write.
constexpr int RumbleWriteMs = 30;
constexpr int RumbleRefreshMs = 40;
constexpr int RumbleStops = 3;
// After the last one lets go of the rumble, the connection may go back to sniff mode once
// the last "off" is surely out.
constexpr int RumbleReleaseMs = 500;

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
    m_probeTimer.setSingleShot(true);
    m_probeTimer.setInterval(RingProbeMs);
    connect(&m_probeTimer, &QTimer::timeout, this, [this] { setRingWanted(RingUser::Probe, false); });
    m_ringTimer.setInterval(RingLookMs);
    connect(&m_ringTimer, &QTimer::timeout, this, &JoyCon::lookForRing);
    m_lightsTimer.setSingleShot(true);
    m_lightsTimer.setInterval(LightsCheckMs);
    connect(&m_lightsTimer, &QTimer::timeout, this, &JoyCon::checkLights);
    m_rumbleRelease.setSingleShot(true);
    m_rumbleRelease.setInterval(RumbleReleaseMs);
    connect(&m_rumbleRelease, &QTimer::timeout, this, [this] {
        if (m_rumbleUsers == 0)
            emit activeLinkWanted(false);
    });
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
        stopRumble();
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
    readMotionCalibration(FactoryMotion, false);
    readMotionCalibration(UserMotion, true);
    send(EnableMotion, QByteArray(1, char(0x01)));
    send(EnableVibration, QByteArray(1, char(0x01)));
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

void JoyCon::readMotionCalibration(quint32 address, bool user)
{
    const int size = MotionCalibrationSize + (user ? 2 : 0);
    QByteArray args;
    for (int i = 0; i < 4; ++i)
        args.append(char(address >> (8 * i)));
    args.append(char(size));
    send(ReadMemory, args, [this, size, user](const QByteArray &reply) {
        QByteArray data = reply.mid(5, size);
        if (data.size() < size)
            return;
        if (user) {
            if (quint8(data[0]) != UserCalibrationMagic[0] || quint8(data[1]) != UserCalibrationMagic[1])
                return;
            data = data.mid(2);
        }
        const auto value = [&](int index) { return int(qFromLittleEndian<qint16>(data.constData() + 2 * index)); };
        StoredMotionCalibration &c = m_motionCalibration;
        for (int axis = 0; axis < 3; ++axis) {
            c.accelOrigin[axis] = value(axis);
            c.accelSensitivity[axis] = value(3 + axis);
            c.gyroOrigin[axis] = value(6 + axis);
            c.gyroSensitivity[axis] = value(9 + axis);
        }
        qCDebug(lcJoyCon).noquote() << name() << (user ? "user" : "factory") << "motion calibration: accelerometer origin"
                                    << c.accelOrigin[0] << c.accelOrigin[1] << c.accelOrigin[2] << "sensitivity"
                                    << c.accelSensitivity[0] << c.accelSensitivity[1] << c.accelSensitivity[2]
                                    << "gyroscope origin" << c.gyroOrigin[0] << c.gyroOrigin[1] << c.gyroOrigin[2]
                                    << "sensitivity" << c.gyroSensitivity[0] << c.gyroSensitivity[1] << c.gyroSensitivity[2];
    });
}

void JoyCon::setRingWanted(RingUser who, bool wanted)
{
    m_ringUsers = wanted ? m_ringUsers | int(who) : m_ringUsers & ~int(who);
    applyRingWanted(m_ringUsers != 0);
}

void JoyCon::applyRingWanted(bool wanted)
{
    if (m_side != Side::Right || m_ringWanted == wanted)
        return;
    m_ringWanted = wanted;
    if (wanted) {
        // The second processor on and in standby; then ask the rail now and again.
        send(SetMcuState, QByteArray(1, char(0x01)));
        QByteArray config(McuConfigSize, '\0');
        config[0] = char(0x21); // configure
        config[1] = char(0x01); // set the device mode
        config[2] = char(0x01); // standby
        config[McuConfigSize - 1] = char(crc8(config, 1, McuConfigSize - 2));
        send(SetMcuConfig, config, [this](const QByteArray &) {
            if (m_ringWanted && !m_ring)
                m_ringTimer.start();
        });
    } else {
        m_ringTimer.stop();
        if (m_ring)
            send(DisableExternalPolling);
        send(SetMcuState, QByteArray(1, char(0x00)));
        setRing(false);
    }
}

void JoyCon::probeRing()
{
    // While the ring is read anyway, that tells.
    if (m_side != Side::Right || m_ringUsers != 0)
        return;
    setRingWanted(RingUser::Probe, true);
    m_probeTimer.start();
}

void JoyCon::setInRing(bool inRing)
{
    if (m_inRing == inRing)
        return;
    m_inRing = inRing;
    qCInfo(lcJoyCon).noquote() << name() << m_address << (inRing ? "sits in a Ring-Con" : "is not in a Ring-Con");
    emit inRingChanged();
}

void JoyCon::lookForRing()
{
    send(GetExternalDevice, {}, [this](const QByteArray &data) {
        if (!m_ringWanted || m_ring || data.size() < 2)
            return;
        const quint16 device = qFromLittleEndian<quint16>(data.constData());
        qCDebug(lcJoyCon).noquote() << name() << "has on its rail:" << Qt::hex << device;
        setInRing(device == ExternalRingCon);
        // A probe only wanted to know.
        if (m_ringUsers == int(RingUser::Probe)) {
            m_probeTimer.stop();
            setRingWanted(RingUser::Probe, false);
            return;
        }
        if (device != ExternalRingCon)
            return;
        m_ringTimer.stop();
        send(SetExternalFormat, QByteArray(reinterpret_cast<const char *>(RingFormat), sizeof RingFormat));
        send(EnableExternalPolling, QByteArray(reinterpret_cast<const char *>(RingPolling), sizeof RingPolling),
             [this](const QByteArray &) { setRing(true); });
    });
}

void JoyCon::setRing(bool ring)
{
    if (m_ring == ring)
        return;
    m_ring = ring;
    m_ringSilent = 0;
    m_ringFlex = NoRingFlex;
    qCInfo(lcJoyCon).noquote() << name() << m_address << (ring ? "has a Ring-Con attached" : "has no Ring-Con any more");
    if (ring)
        setInRing(true);
    emit ringChanged();
    if (!ring && m_ringWanted)
        m_ringTimer.start();
}

// Follows the gyroscope's error as it creeps (it does, by a tenth of a degree a second or
// so as the controller warms up), the way a Switch does: while the controller is at rest,
// what the gyroscope still shows is error, and the correction is moved towards it.
//
// At rest means a full second in which no gyroscope axis varied by more than 0.6°/s, the
// acceleration's strength by no more than 0.01 g, and the gyroscope's mean stayed under
// 1°/s. Measured lying still: 0.18 to 0.31°/s and 0.002 to 0.003 g. A hand holding it
// shakes it far beyond that, so it is not followed while held. What this does swallow is
// a turn slower than 1°/s made by something steadier than a hand; that is the price.
void JoyCon::trackRest(const MotionReport &samples, bool gap)
{
    if (!m_restTracking)
        return;
    if (gap)
        m_restWindow = {};
    RestWindow &w = m_restWindow;
    for (const Motion &sample : samples) {
        const float strength = std::hypot(sample.accel[0], sample.accel[1], sample.accel[2]);
        for (int axis = 0; axis < 3; ++axis) {
            if (w.count == 0)
                w.low[axis] = w.high[axis] = sample.gyro[axis];
            w.low[axis] = qMin(w.low[axis], sample.gyro[axis]);
            w.high[axis] = qMax(w.high[axis], sample.gyro[axis]);
            w.sum[axis] += sample.gyro[axis];
        }
        if (w.count == 0) {
            w.strengthLow = w.strengthHigh = strength;
            w.start = sample.time;
        }
        w.strengthLow = qMin(w.strengthLow, strength);
        w.strengthHigh = qMax(w.strengthHigh, strength);
        ++w.count;
    }
    if (w.count == 0 || samples[MotionSamples - 1].time - w.start < RestWindowUs)
        return;
    bool still = w.strengthHigh - w.strengthLow <= RestStrengthSpread;
    for (int axis = 0; axis < 3; ++axis) {
        still = still && w.high[axis] - w.low[axis] <= RestTurningSpread
                && std::abs(w.sum[axis] / w.count) <= RestTurningMost;
    }
    if (still) {
        for (int axis = 0; axis < 3; ++axis)
            m_restCorrection.gyro[axis] += float(w.sum[axis] / w.count) * RestFollow;
    }
    m_restWindow = {};
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

void JoyCon::setRumbleWanted(RumbleUser who, bool wanted)
{
    const int users = wanted ? m_rumbleUsers | int(who) : m_rumbleUsers & ~int(who);
    const bool before = m_rumbleUsers != 0;
    m_rumbleUsers = users;
    if (before == (users != 0))
        return;
    if (users != 0) {
        m_rumbleRelease.stop();
        emit activeLinkWanted(true);
    } else {
        stopRumble();
        m_rumbleRelease.start();
    }
}

void JoyCon::setRumble(Motor motor, const Vibration &vibration)
{
    // Written to while its connection may sleep, the link would flap (setRumbleWanted).
    if (m_rumbleUsers == 0)
        return;
    const int index = m_side == Side::Both && motor == Motor::Right ? 1 : 0;
    m_rumble[index] = vibration;
    Vibration &next = m_rumbleNext[index];
    if (vibration.lowAmplitude >= next.lowAmplitude) {
        next.lowAmplitude = vibration.lowAmplitude;
        next.lowFrequency = vibration.lowFrequency;
    }
    if (vibration.highAmplitude >= next.highAmplitude) {
        next.highAmplitude = vibration.highAmplitude;
        next.highFrequency = vibration.highFrequency;
    }
    m_rumbleChanged = true;
    traceRumble(vibration);
}

void JoyCon::stopRumble()
{
    m_rumble = {};
    m_rumbleNext = {};
    m_rumbleChanged = m_rumbleChanged || m_rumbling;
}

// After every report: whatever is due and allowed goes out. Right after a report is when
// the Joy-Con listens best (the kernel's hid_nintendo does the same).
void JoyCon::writeRumble()
{
    if (m_fd < 0 || !m_ready || m_quiet)
        return;
    const qint64 since = m_sinceWrite.isValid() ? m_sinceWrite.elapsed() : RumbleWriteMs;
    const bool due = m_rumbleChanged || m_stopsLeft > 0 || (m_rumbling && since >= RumbleRefreshMs);
    if (since < RumbleWriteMs || !due)
        return;
    QByteArray packet;
    packet.append(char(OutputRumble));
    packet.append(char(m_counter++ & 0x0f));
    packet.append(takeRumbleData());
    if (::write(m_fd, packet.constData(), packet.size()) < 0)
        qCWarning(lcJoyCon).noquote() << name() << "write failed:" << std::strerror(errno);
}

// The rumble data for an output report about to be written, which then counts as written.
QByteArray JoyCon::takeRumbleData()
{
    QByteArray data(2 * RumbleSize, Qt::Uninitialized);
    for (int half = 0; half < 2; ++half) {
        // A Joy-Con alone has its actuator in its own half; it is given the same in both.
        const Vibration &v = m_rumbleNext[m_side == Side::Both ? half : 0];
        encodeRumble(v.lowAmplitude, v.lowFrequency, v.highAmplitude, v.highFrequency,
                     reinterpret_cast<quint8 *>(data.data()) + half * RumbleSize);
    }
    const bool rumbling = std::memcmp(data.constData(), RumbleOff, RumbleSize) != 0
                          || std::memcmp(data.constData() + RumbleSize, RumbleOff, RumbleSize) != 0;
    if (rumbling != m_rumbling)
        qCDebug(lcJoyCon).noquote() << name() << (rumbling ? "rumbles" : "stops rumbling");
    m_stopsLeft = rumbling ? 0 : m_rumbling ? RumbleStops - 1 : qMax(0, m_stopsLeft - 1);
    m_rumbling = rumbling;
    RumbleTrace &t = m_rumbleTrace;
    if (t.clock.isValid()) {
        ++t.writes;
        const bool stopped = std::all_of(m_rumble.begin(), m_rumble.end(), [](const Vibration &v) {
            return v.lowAmplitude <= 0 && v.highAmplitude <= 0;
        });
        if (!rumbling && stopped) {
            const auto hz = [](const float (&r)[2]) {
                return r[0] > r[1] ? QStringLiteral("-") : QStringLiteral("%1-%2 Hz").arg(r[0], 0, 'f', 0).arg(r[1], 0, 'f', 0);
            };
            qCDebug(lcJoyCon).noquote() << name() << "rumble of" << t.clock.elapsed() << "ms:" << t.values
                                        << "values, shortest gap" << t.shortestGapUs / 1000.0 << "ms," << t.writes
                                        << "writes; low up to" << t.lowMost << hz(t.lowFrequency) << "high up to"
                                        << t.highMost << hz(t.highFrequency) << "; ms:low/high" << t.shape;
            t = {};
        }
    }
    m_rumbleNext = m_rumble;
    m_rumbleChanged = false;
    m_sinceWrite.start();
    return data;
}

// For the log (debug level): what the game sends during one rumble.
void JoyCon::traceRumble(const Vibration &v)
{
    if (!lcJoyCon().isDebugEnabled())
        return;
    RumbleTrace &t = m_rumbleTrace;
    if (!t.clock.isValid()) {
        if (v.lowAmplitude <= 0 && v.highAmplitude <= 0)
            return;
        t = {};
        t.clock.start();
        t.lowFrequency[0] = t.highFrequency[0] = 1e6f;
    }
    const qint64 us = t.clock.nsecsElapsed() / 1000;
    if (t.values > 0 && (t.shortestGapUs < 0 || us - t.lastUs < t.shortestGapUs))
        t.shortestGapUs = us - t.lastUs;
    t.lastUs = us;
    ++t.values;
    t.lowMost = std::max(t.lowMost, v.lowAmplitude);
    t.highMost = std::max(t.highMost, v.highAmplitude);
    const auto range = [](float (&r)[2], float amplitude, float frequency) {
        if (amplitude > 0) {
            r[0] = std::min(r[0], frequency);
            r[1] = std::max(r[1], frequency);
        }
    };
    range(t.lowFrequency, v.lowAmplitude, v.lowFrequency);
    range(t.highFrequency, v.highAmplitude, v.highFrequency);
    if (t.values <= 40)
        t.shape += QStringLiteral(" %1:%2/%3").arg(us / 1000).arg(v.lowAmplitude, 0, 'f', 2).arg(v.highAmplitude, 0, 'f', 2);
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
        if (m_lostReports > 0)
            qCInfo(lcJoyCon).noquote() << name() << m_address << "had" << m_lostReports << "reports lost on the radio";
        qCInfo(lcJoyCon).noquote() << name() << m_address << "lost:" << (size < 0 ? std::strerror(errno) : "end of file")
                                   << (m_sinceReport.isValid() ? QStringLiteral("- last report %1 ms ago").arg(m_sinceReport.elapsed())
                                                               : QString());
        m_notifier->setEnabled(false);
        m_replyTimer.stop();
        m_lightsTimer.stop();
        m_quietTimer.stop();
        m_ringTimer.stop();
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
        // Written in step with the reports, which also stop while it is out of reach.
        writeRumble();
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

    if (type == InputFull && m_ready && report.size() >= MotionOffset + MotionSamples * MotionSampleSize) {
        const StoredMotionCalibration &c = m_motionCalibration;
        // The sensor in a right Joy-Con is mounted the other way round: its y and z point
        // the opposite way, so they are turned to match the others.
        const float flip = m_side == Side::Right ? -1.0f : 1.0f;
        const float sign[3] = {1.0f, flip, flip};
        MotionReport samples;
        // The readings' times. The report's own timer (its second byte) counts 5 ms ticks
        // and wraps every 1.28 s. A report carries three readings taken since the one
        // before, spread evenly over the ticks between them: 3 ticks (15 ms, 67 reports a
        // second) while the connection is in sniff mode, 2 or 3 (89 reports a second)
        // while it is kept active for rumble (measured). So the readings' times come from
        // the timer; when a report arrives says little, the radio delivers them unevenly.
        // A step well beyond the usual one means reports were lost; a silence longer than
        // a second is taken from our own clock instead.
        if (!m_motionClock.isValid())
            m_motionClock.start();
        // A second ahead, so that the very first readings' times, set back from the
        // arrival, aren't negative.
        const qint64 arrived = m_motionClock.nsecsElapsed() / 1000 + 1000000;
        const int timer = quint8(report[1]);
        qint64 last;
        // The time this report's own readings cover.
        qint64 span = MotionSamples * MotionIntervalUs;
        int lost = 0;
        const qint64 before = m_motionTime;
        if (m_motionTime == 0) {
            last = arrived;
        } else {
            qint64 ticks = (timer - m_lastTimer) & 0xff;
            if (arrived - m_lastArrived > 1000000)
                ticks = (arrived - m_lastArrived + MotionIntervalUs / 2) / MotionIntervalUs;
            ticks = qMax<qint64>(1, ticks);
            const qint64 reports = qMax<qint64>(1, qRound(ticks / m_usualTicks));
            lost = int(reports - 1);
            m_lostReports += lost;
            // The usual step follows the connection's rhythm, which changes with its mode.
            if (lost == 0)
                m_usualTicks += (ticks - m_usualTicks) / 32;
            last = m_motionTime + ticks * MotionIntervalUs;
            span = ticks * MotionIntervalUs / reports;
        }
        const qint64 spacing = span / MotionSamples;
        const qint64 first = last - (MotionSamples - 1) * spacing;
        m_lastTimer = timer;
        m_lastArrived = arrived;
        m_motionTime = last;
        // While the Joy-Con polls its rail (for the Ring-Con), what the rail says takes
        // the place of the third reading's accelerometer: 0, the ring's flex and the
        // ring's id, or all zeros with nothing attached (measured). The gyroscope half
        // stays the Joy-Con's own. The data tells: no accelerometer reads exactly that.
        const char *third = report.constData() + MotionOffset + (MotionSamples - 1) * MotionSampleSize;
        const quint16 railId = qFromLittleEndian<quint16>(third + 4);
        const int railValue = qFromLittleEndian<qint16>(third + 2);
        const bool rail = m_ringWanted && qFromLittleEndian<qint16>(third) == 0
                          && (railId == ExternalRingCon || (railId == 0 && railValue == 0));
        for (int n = 0; n < MotionSamples; ++n) {
            samples[n].time = first + n * spacing;
            const char *raw = report.constData() + MotionOffset + n * MotionSampleSize;
            for (int axis = 0; axis < 3; ++axis) {
                const int accel = qFromLittleEndian<qint16>(raw + 2 * axis);
                const int gyro = qFromLittleEndian<qint16>(raw + 6 + 2 * axis);
                // The sensitivity is the reading at 4 g, and at 936 degrees a second.
                const int accelSpan = c.accelSensitivity[axis] - c.accelOrigin[axis];
                const int gyroSpan = c.gyroSensitivity[axis] - c.gyroOrigin[axis];
                samples[n].accel[axis] = accelSpan != 0 ? sign[axis] * accel * 4.0f / accelSpan : 0.0f;
                samples[n].gyro[axis] = gyroSpan != 0 ? sign[axis] * (gyro - c.gyroOrigin[axis]) * 936.0f / gyroSpan : 0.0f;
                samples[n].accel[axis] *= m_restCorrection.accelScale;
                samples[n].gyro[axis] -= m_restCorrection.gyro[axis];
            }
        }
        // The third accelerometer reading is then the second once more.
        if (rail) {
            for (int axis = 0; axis < 3; ++axis)
                samples[MotionSamples - 1].accel[axis] = samples[MotionSamples - 2].accel[axis];
        }
        // The readings of lost reports, filled in on a straight line from the last reading
        // before the gap to the first after it, so that whoever adds the motion up gets a
        // complete stream. They go out now, late but in order. Not for a long silence:
        // nothing sensible can be said about that.
        if (lost > 0 && lost <= MostFilledReports) {
            const int missing = lost * MotionSamples;
            MotionReport filled;
            for (int j = 0; j < missing; ++j) {
                const float share = float(j + 1) / float(missing + 1);
                Motion &reading = filled[j % MotionSamples];
                reading.time = before + (j + 1) * (first - before) / (missing + 1);
                for (int axis = 0; axis < 3; ++axis) {
                    reading.accel[axis] = m_lastMotion.accel[axis] + (samples[0].accel[axis] - m_lastMotion.accel[axis]) * share;
                    reading.gyro[axis] = m_lastMotion.gyro[axis] + (samples[0].gyro[axis] - m_lastMotion.gyro[axis]) * share;
                }
                if (j % MotionSamples == MotionSamples - 1)
                    emit motion(filled);
            }
        }
        if (m_ring && rail) {
            // Taken off the rail, the id is gone at once; a few reports in a row, in
            // case one is ever wrong.
            m_ringSilent = railId == ExternalRingCon ? 0 : m_ringSilent + 1;
            if (m_ringSilent >= RingGoneReports) {
                setRing(false);
                setInRing(false);
            }
            else if (railId == ExternalRingCon) {
                m_ringFlex = railValue;
                emit ringFlex(railValue);
            }
        }
        m_lastMotion = samples[MotionSamples - 1];
        // A gap spoils the stretch of stillness being watched, filled in or not.
        trackRest(samples, lost > 0);
        emit motion(samples);
    }

    bool ours = type == InputSubcommandReply && m_waiting && report.size() > ReplyDataOffset
                && quint8(report[ReplyIdOffset]) == m_queue.first().id;
    // A memory read is answered with its address and length in front. They must be the
    // ones asked for: a Joy-Con that is slow to answer (seen when the service starts with
    // two of them already connected) answers a read after the next one has gone out, and
    // taken for that one's answer it gave both Joy-Cons a calibration of -1 and 255.
    if (ours && m_queue.first().id == ReadMemory
        && report.mid(ReplyDataOffset, 5) != m_queue.first().args.left(5))
        ours = false;
    if (type == InputSubcommandReply && !ours && report.size() > ReplyDataOffset) {
        // A late answer to an earlier try or an earlier read.
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
    // The rumble goes on through a subcommand: one with "no vibration" would stop it.
    packet.append(takeRumbleData());
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
