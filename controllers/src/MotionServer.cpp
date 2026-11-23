#include "MotionServer.h"

#include "Unit.h"
#include "UnitManager.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QNetworkDatagram>
#include <QRandomGenerator>
#include <QTimer>
#include <QtEndian>

#include <cstring>

Q_LOGGING_CATEGORY(lcMotion, "tvcontrollers.motion")

// The protocol, from the unofficial reference at https://v1993.github.io/cemuhook-protocol/.
// All numbers little endian.
namespace {

constexpr quint16 ProtocolVersion = 1001;
constexpr int HeaderSize = 16; // magic, version, length, CRC, sender id
constexpr quint32 TypeVersion = 0x100000;
constexpr quint32 TypeInfo = 0x100001;
constexpr quint32 TypeData = 0x100002;
// A client that hasn't asked for this long is gone; there is no goodbye.
constexpr int ClientTimeoutMs = 5000;
// A report's three readings are 5 ms apart; they go out at that pace, because a client
// may take the time between packets as the time between readings.
constexpr int SampleIntervalMs = 5;
// More readings than this waiting to go out for a pad, two reports' worth, is a backlog.
constexpr int MostWaiting = 6;

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

template<typename T>
void put(QByteArray &packet, int offset, T value)
{
    qToLittleEndian<T>(value, packet.data() + offset);
}

void putFloat(QByteArray &packet, int offset, float value)
{
    quint32 bits;
    std::memcpy(&bits, &value, sizeof bits);
    put<quint32>(packet, offset, bits);
}

// 0 (empty) to 4 (full), as the protocol's "dying" to "full".
quint8 batteryCode(int level)
{
    return quint8(qBound(0, level, 4) + 1);
}

} // namespace

MotionServer::MotionServer(UnitManager *units, QObject *parent)
    : QObject(parent)
    , m_units(units)
    , m_id(QRandomGenerator::global()->generate())
{
    if (!m_socket.bind(QHostAddress::LocalHost, Port))
        qCWarning(lcMotion).noquote() << "cannot serve motion on port" << Port << ":" << m_socket.errorString();
    else
        qCInfo(lcMotion) << "serving motion (cemuhook) on localhost port" << Port;
    connect(&m_socket, &QUdpSocket::readyRead, this, &MotionServer::readRequests);
    connect(units, &UnitManager::changed, this, &MotionServer::assignPads);
    // The readings go out one every 5 ms, in order. Reports come from the radio unevenly,
    // sometimes several at once; sent as they come, their readings would arrive in
    // bursts, and sent by a timer each, out of order.
    m_pace.setTimerType(Qt::PreciseTimer);
    m_pace.setInterval(SampleIntervalMs);
    connect(&m_pace, &QTimer::timeout, this, [this] {
        bool more = false;
        for (int pad = 0; pad < Pads; ++pad) {
            QQueue<JoyCon::Motion> &waiting = m_waiting[pad];
            // Fallen behind (a burst): two at a time until it is caught up.
            for (int n = waiting.size() > MostWaiting ? 2 : 1; n > 0 && !waiting.isEmpty(); --n)
                sendMotion(pad, waiting.dequeue());
            more = more || !waiting.isEmpty();
        }
        if (!more)
            m_pace.stop();
    });
    assignPads();
}

void MotionServer::assignPads()
{
    for (const QMetaObject::Connection &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    for (JoyCon *&pad : m_pads)
        pad = nullptr;
    for (QQueue<JoyCon::Motion> &waiting : m_waiting)
        waiting.clear();

    const QList<Unit *> units = m_units->units();
    for (const Unit *unit : units) {
        const int first = 2 * (unit->player() - 1);
        const QList<JoyCon *> joyCons = unit->joyCons();
        for (int i = 0; i < joyCons.size() && first + i < Pads && first >= 0; ++i)
            m_pads[first + i] = joyCons.at(i);
    }

    for (int pad = 0; pad < Pads; ++pad) {
        JoyCon *joyCon = m_pads[pad];
        if (!joyCon)
            continue;
        m_connections.append(connect(joyCon, &JoyCon::motion, this, [this, pad](const JoyCon::MotionReport &samples) {
            if (m_clients.isEmpty())
                return;
            for (const JoyCon::Motion &sample : samples)
                m_waiting[pad].enqueue(sample);
            if (!m_pace.isActive())
                m_pace.start();
        }));
    }
}

// The eleven bytes that describe a pad, in an answer about it and in front of its data.
QByteArray MotionServer::padInfo(int pad) const
{
    QByteArray info(11, '\0');
    info[0] = char(pad);
    const JoyCon *joyCon = m_pads[pad];
    if (!joyCon)
        return info;
    info[1] = 2; // connected
    info[2] = 2; // full gyro
    info[3] = 2; // Bluetooth
    const QByteArray mac = QByteArray::fromHex(joyCon->address().remove(QLatin1Char(':')).toLatin1());
    for (int i = 0; i < 6 && i < mac.size(); ++i)
        info[4 + i] = mac[i];
    info[10] = char(batteryCode(joyCon->state().battery));
    return info;
}

void MotionServer::reply(const QHostAddress &address, quint16 port, quint32 type, const QByteArray &payload)
{
    QByteArray packet(HeaderSize + 4, '\0');
    std::memcpy(packet.data(), "DSUS", 4);
    put<quint16>(packet, 4, ProtocolVersion);
    put<quint16>(packet, 6, quint16(4 + payload.size())); // the length counts the type
    put<quint32>(packet, 12, m_id);
    put<quint32>(packet, 16, type);
    packet.append(payload);
    put<quint32>(packet, 8, crc32(packet)); // computed with its own field still zero
    m_socket.writeDatagram(packet, address, port);
}

void MotionServer::readRequests()
{
    while (m_socket.hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket.receiveDatagram();
        const QByteArray packet = datagram.data();
        if (packet.size() < HeaderSize + 4 || !packet.startsWith("DSUC"))
            continue;
        const quint32 type = qFromLittleEndian<quint32>(packet.constData() + 16);
        const QByteArray payload = packet.mid(HeaderSize + 4);
        const QHostAddress address = datagram.senderAddress();
        const quint16 port = quint16(datagram.senderPort());

        if (type == TypeVersion) {
            QByteArray version(2, '\0');
            put<quint16>(version, 0, ProtocolVersion);
            reply(address, port, TypeVersion, version);
        } else if (type == TypeInfo && payload.size() >= 4) {
            const int count = qBound(0, qFromLittleEndian<qint32>(payload.constData()), Pads);
            for (int i = 0; i < count && 4 + i < payload.size(); ++i) {
                const int pad = quint8(payload[4 + i]);
                if (pad < Pads)
                    reply(address, port, TypeInfo, padInfo(pad) + QByteArray(1, '\0'));
            }
        } else if (type == TypeData && payload.size() >= 8) {
            const QString key = address.toString() + QLatin1Char(':') + QString::number(port);
            const bool known = m_clients.contains(key);
            Client &client = m_clients[key];
            client.address = address;
            client.port = port;
            client.lastAsked = QDateTime::currentMSecsSinceEpoch();
            // By pad number, or everything. Asking by address is answered with everything too.
            const bool byPad = quint8(payload[0]) & 1;
            const int wanted = quint8(payload[1]);
            for (int pad = 0; pad < Pads; ++pad)
                client.pads[pad] = client.pads[pad] || !byPad || pad == wanted;
            if (!known)
                qCInfo(lcMotion).noquote() << "a program at" << key << "reads motion";
        }
    }
}

void MotionServer::sendMotion(int pad, const JoyCon::Motion &sample)
{
    const JoyCon *joyCon = m_pads[pad];
    if (!joyCon)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_clients.begin(); it != m_clients.end();) {
        Client &client = it.value();
        if (now - client.lastAsked > ClientTimeoutMs) {
            qCInfo(lcMotion).noquote() << "the program at" << it.key() << "stopped reading motion";
            it = m_clients.erase(it);
            continue;
        }
        if (client.pads[pad]) {
            QByteArray data(80, '\0');
            data.replace(0, 11, padInfo(pad));
            data[11] = 1; // connected
            put<quint32>(data, 12, client.packets++);
            // Buttons stay released and the sticks centred; see the header.
            for (int stick = 20; stick < 24; ++stick)
                data[stick] = char(128);
            // The reading's own time, not when it is sent: see JoyCon::Motion.
            put<quint64>(data, 48, quint64(sample.time));
            // The controller's axes (x to its top, y to its left, z out of its face) as the
            // protocol's. Its reference doesn't state the directions, so they are set by
            // what Eden makes of them: Eden's cemuhook client (udp_client.cpp, OnPadData)
            // must end up with what Eden's own Joy-Con driver (joycon_protocol/poller.cpp)
            // gives for the same movement. In our axes that driver's result is
            // acceleration (y, -x, -z) and rotation (-y, x, z) for every controller, and
            // the client takes acceleration as (x, -z, y) and rotation as (pitch, roll,
            // -yaw). So a controller lying face up shows -1 on the protocol's y.
            putFloat(data, 56, sample.accel[1]);
            putFloat(data, 60, -sample.accel[2]);
            putFloat(data, 64, sample.accel[0]);
            putFloat(data, 68, -sample.gyro[1]); // pitch
            putFloat(data, 72, -sample.gyro[2]); // yaw
            putFloat(data, 76, sample.gyro[0]); // roll
            reply(client.address, client.port, TypeData, data);
        }
        ++it;
    }
}
