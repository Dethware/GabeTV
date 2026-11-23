#pragma once

#include "../protocol/emulator.h"
#include "JoyCon.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

class DeviceWatcher;
class QSocketNotifier;
class UnitManager;

// The service's connection for an emulator (protocol/emulator.h): a Unix socket on which
// a program gets every physical controller by itself, as the service reads it, and asks
// for what a game asks of a real Joy-Con. It stands next to the virtual pads, which are
// for everything that isn't an emulator of the Switch.
//
// What a program asked for (the ring) ends when its connection does.
class EmulatorServer : public QObject
{
    Q_OBJECT

public:
    EmulatorServer(DeviceWatcher *watcher, UnitManager *units, QObject *parent = nullptr);
    ~EmulatorServer() override;

signals:
    // A connected emulator has a game being played, or none has any more.
    void playingChanged(bool playing);

private:
    struct Client
    {
        QSocketNotifier *notifier = nullptr;
        QString name;
        // Said Hello with our version; nothing is sent to it before.
        bool greeted = false;
        // The Joy-Cons whose ring it asked for.
        QSet<JoyCon *> rings;
        // It has a game being played (not paused).
        bool playing = false;
    };

    void listen();
    void accept();
    void read(int fd);
    void drop(int fd);

    void watch(JoyCon *joyCon);
    void forget(JoyCon *joyCon);
    JoyCon *joyCon(quint8 id) const;

    QByteArray describe() const;
    void announce();
    void adopt(JoyCon *joyCon);
    // Without readings: the neutral state of a controller that went silent.
    void sendInput(JoyCon *joyCon, const JoyCon::MotionReport *motion);
    void send(int fd, const QByteArray &message);
    void wantRing(int fd, JoyCon *joyCon, bool on);
    void updateRing(JoyCon *joyCon);
    void updatePlaying();

    DeviceWatcher *m_watcher;
    UnitManager *m_units;
    QString m_path;
    int m_listening = -1;
    QSocketNotifier *m_acceptNotifier = nullptr;
    // The runtime directory only exists while the user is logged in.
    QTimer m_retry;
    // Battery levels change without anything else happening.
    QTimer m_refresh;
    QHash<int, Client> m_clients;
    // The ids the protocol knows the connected controllers by.
    QHash<JoyCon *, quint8> m_ids;
    // What was announced last, to say it again only when it changed.
    QByteArray m_announced;
    bool m_playing = false;
};
