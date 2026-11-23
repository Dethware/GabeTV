#include "MotionClient.h"

#include <QHostAddress>
#include <QLoggingCategory>
#include <QNetworkDatagram>
#include <QtEndian>

#include <cstring>

Q_LOGGING_CATEGORY(lcMotion, "tvshell.motion")

// The cemuhook (DSU) protocol, https://v1993.github.io/cemuhook-protocol/. Little endian.
namespace {

constexpr quint16 Port = 26760;
constexpr quint16 ProtocolVersion = 1001;
constexpr quint32 TypeData = 0x100002;
constexpr int HeaderSize = 20; // with the message type
constexpr int DataSize = 80;
// How much of the way each reading pulls the tilt towards what gravity says. At 200
// readings a second this settles in about half a second and still rides out a shake.
constexpr float GravityGain = 0.01f;
const QVector3D Up(0, 1, 0);
constexpr float AverageTime = 0.5f; // seconds
// The shape's place in its box is the push itself, as a displacement: so far for each g.
// It is not where the controller is. Adding the push up to a speed and a place was tried
// and dropped: from this sensor alone every small error keeps growing, and it wandered
// off within seconds.
constexpr float MetresPerG = 0.15f;
// How far from the middle the movement may go, in metres.
constexpr float Reach = 0.3f;

quint32 crc32(const QByteArray &data)
{
    static quint32 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int bit = 0; bit < 8; ++bit)
                c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    quint32 crc = 0xffffffffu;
    for (const char byte : data)
        crc = table[(crc ^ quint8(byte)) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

float getFloat(const QByteArray &data, int offset)
{
    const quint32 bits = qFromLittleEndian<quint32>(data.constData() + offset);
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

} // namespace

MotionPad::MotionPad(QObject *parent)
    : QObject(parent)
{
}

void MotionPad::add(const QVector3D &sensor, const QVector3D &turning, float seconds)
{
    m_acceleration = sensor;
    ++m_count;
    m_dirty = true;
    // What the accelerometer showed over the last half second or so, for recenter(): one
    // reading has enough noise in it to set the level a fraction of a degree off.
    const float share = qMin(1.0f, seconds / AverageTime);
    m_averageSensor += (sensor - m_averageSensor) * share;

    const QVector3D acceleration = sensor;
    const QVector3D rotation = turning;
    m_rotation = rotation;

    const float g = acceleration.length();
    const bool steady = g > 0.8f && g < 1.2f;
    if (!m_started) {
        // Start level with gravity, pointing away from the viewer.
        if (!steady)
            return;
        m_orientation = QQuaternion::rotationTo(acceleration / g, Up);
        m_started = true;
        return;
    }
    // The gyroscope says how far the controller turned, about its own axes.
    const float speed = rotation.length();
    if (speed > 0.01f)
        m_orientation = (m_orientation * QQuaternion::fromAxisAndAngle(rotation / speed, speed * seconds)).normalized();
    // The turn about the vertical, counted up: a full turn on the table should read 360.
    m_turned += m_orientation.rotatedVector(rotation).y() * seconds;
    // At rest the accelerometer points up; pull the tilt a little towards that. Not while
    // it is being shaken or swung, when it shows more than gravity.
    if (steady) {
        const QVector3D measured = m_orientation.rotatedVector(acceleration / g);
        const QQuaternion fix = QQuaternion::slerp(QQuaternion(), QQuaternion::rotationTo(measured, Up), GravityGain);
        m_orientation = (fix * m_orientation).normalized();
    }
    // What is left of the acceleration, seen from the room, once gravity is taken out.
    const QVector3D push = m_orientation.rotatedVector(acceleration) - Up;
    m_push = m_push * 0.8f + push * 0.2f;
    m_pushPeak = qMax(m_push.length(), m_pushPeak - seconds);

    for (int axis = 0; axis < 3; ++axis)
        m_position[axis] = qBound(-Reach, m_push[axis] * MetresPerG, Reach);
}

float MotionPad::reach() const
{
    return Reach;
}

// Takes the controller as level and pointing away from the viewer, as it lies now; from
// the sensor's average over the last half second, so best a moment after it was put down.
void MotionPad::recenter()
{
    const float g = m_averageSensor.length();
    if (g > 0.8f && g < 1.25f)
        m_orientation = QQuaternion::rotationTo(m_averageSensor / g, Up);
    m_turned = 0;
    emit changed();
}

void MotionPad::countSecond()
{
    const bool active = m_count > 0;
    if (m_rate != m_count || m_active != active) {
        m_rate = m_count;
        m_active = active;
        if (!active)
            m_started = false;
        emit changed();
    }
    m_count = 0;
}

void MotionPad::publish()
{
    if (!m_dirty)
        return;
    m_dirty = false;
    emit changed();
}

MotionClient::MotionClient(QObject *parent)
    : QObject(parent)
{
    for (MotionPad *&pad : m_pads)
        pad = new MotionPad(this);
    connect(&m_socket, &QUdpSocket::readyRead, this, &MotionClient::read);
    // The server drops a client that stops asking.
    m_subscribeTimer.setInterval(1000);
    connect(&m_subscribeTimer, &QTimer::timeout, this, &MotionClient::subscribe);
    // Readings come 200 times a second; the screen needs them 60 times.
    m_publishTimer.setInterval(16);
    connect(&m_publishTimer, &QTimer::timeout, this, [this] {
        for (MotionPad *pad : m_pads)
            pad->publish();
    });
    m_secondTimer.setInterval(1000);
    connect(&m_secondTimer, &QTimer::timeout, this, [this] {
        for (MotionPad *pad : m_pads)
            pad->countSecond();
    });
}

void MotionClient::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    if (running) {
        if (m_socket.state() != QAbstractSocket::BoundState && !m_socket.bind(QHostAddress::LocalHost, 0))
            qCWarning(lcMotion).noquote() << "cannot open a socket for the motion server:" << m_socket.errorString();
        subscribe();
        m_subscribeTimer.start();
        m_publishTimer.start();
        m_secondTimer.start();
    } else {
        m_subscribeTimer.stop();
        m_publishTimer.stop();
        m_secondTimer.stop();
        for (MotionPad *pad : m_pads)
            pad->countSecond(); // twice: the readings so far, then none
        for (MotionPad *pad : m_pads)
            pad->countSecond();
    }
    emit runningChanged();
}

MotionPad *MotionClient::pad(int index) const
{
    return index >= 0 && index < 4 ? m_pads[index] : nullptr;
}

// Asks for the data of every pad.
void MotionClient::subscribe()
{
    QByteArray packet(HeaderSize + 8, '\0');
    std::memcpy(packet.data(), "DSUC", 4);
    qToLittleEndian<quint16>(ProtocolVersion, packet.data() + 4);
    qToLittleEndian<quint16>(quint16(packet.size() - 16), packet.data() + 6);
    qToLittleEndian<quint32>(0x7673686cu, packet.data() + 12); // any id
    qToLittleEndian<quint32>(TypeData, packet.data() + 16);
    qToLittleEndian<quint32>(crc32(packet), packet.data() + 8);
    m_socket.writeDatagram(packet, QHostAddress::LocalHost, Port);
}

void MotionClient::read()
{
    while (m_socket.hasPendingDatagrams()) {
        const QByteArray packet = m_socket.receiveDatagram().data();
        if (packet.size() < HeaderSize + DataSize || !packet.startsWith("DSUS")
            || qFromLittleEndian<quint32>(packet.constData() + 16) != TypeData)
            continue;
        const QByteArray data = packet.mid(HeaderSize);
        const int index = quint8(data[0]);
        if (index >= 4)
            continue;
        // The time between readings, from the readings' own times, in microseconds. (When
        // the packets arrive says little: they come in uneven bursts.)
        const quint64 time = qFromLittleEndian<quint64>(data.constData() + 48);
        const quint64 elapsed = m_lastTime[index] == 0 ? 5000 : time > m_lastTime[index] ? time - m_lastTime[index] : 0;
        m_lastTime[index] = time;
        m_pads[index]->add(QVector3D(getFloat(data, 56), getFloat(data, 60), getFloat(data, 64)),
                           QVector3D(getFloat(data, 68), getFloat(data, 72), getFloat(data, 76)),
                           qMin(elapsed, quint64(50000)) / 1e6f);
    }
}
