#pragma once

#include "JoyCon.h"

#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QQueue>
#include <QTimer>
#include <QUdpSocket>

class UnitManager;

// Gives the controllers' motion to other programs, as a server of the cemuhook (DSU)
// protocol on localhost: the way emulators take motion from outside. A client asks which
// pads there are and subscribes; from then on it gets a packet for every motion reading,
// until it stops asking.
//
// A server has four pads. Each Joy-Con is its own pad, because a pair has two motion
// sensors and a game may use both (Ring Fit: the ring and the leg):
//   pad 0  player 1, its left Joy-Con, or its only controller
//   pad 1  player 1, the right Joy-Con of a pair
//   pad 2  player 2, likewise
//   pad 3
// Players 3 and 4 are not served yet.
//
// Only motion, the battery and the identity are filled in. Buttons and sticks reach
// programs through the virtual controller.
class MotionServer : public QObject
{
    Q_OBJECT

public:
    explicit MotionServer(UnitManager *units, QObject *parent = nullptr);

    static constexpr quint16 Port = 26760;
    static constexpr int Pads = 4;

private:
    struct Client
    {
        QHostAddress address;
        quint16 port = 0;
        // Which pads it asked for; all of them if it named none.
        bool pads[Pads] = {};
        qint64 lastAsked = 0; // ms
        quint32 packets = 0;
    };

    void assignPads();
    void readRequests();
    void sendMotion(int pad, const JoyCon::Motion &sample);
    QByteArray padInfo(int pad) const;
    void reply(const QHostAddress &address, quint16 port, quint32 type, const QByteArray &payload);

    UnitManager *m_units;
    QUdpSocket m_socket;
    quint32 m_id;
    JoyCon *m_pads[Pads] = {};
    // Readings not yet sent, per pad, and the timer that sends them.
    QQueue<JoyCon::Motion> m_waiting[Pads];
    QTimer m_pace;
    QList<QMetaObject::Connection> m_connections;
    QHash<QString, Client> m_clients; // by "address:port"
};
