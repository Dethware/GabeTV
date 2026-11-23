#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class Bluetooth;
class UnitManager;

// What the shell sees of the service, on the session bus:
//
//   busctl --user introspect local.tvcontrollers /local/tvcontrollers
//
// Describe() gives the connected Joy-Cons and units as JSON; Changed says it is out of
// date; HomePressed is Home on any Joy-Con. The search and switch-off calls are for the
// shell's controllers screen.
//
// The service is a system unit that may start before the user's session bus exists, so
// the connection is retried until it works.
class ControllerService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "local.tvcontrollers")

public:
    ControllerService(UnitManager *units, Bluetooth *bluetooth, QObject *parent = nullptr);

public slots:
    Q_SCRIPTABLE QString Describe() const;
    Q_SCRIPTABLE void StartSearch();
    Q_SCRIPTABLE void StopSearch();
    Q_SCRIPTABLE void SwitchOff(const QString &address);
    // Ends the controller with this player number; its Joy-Cons stay connected.
    Q_SCRIPTABLE void EndUnit(int player);
    // The controllers with these two player numbers trade them.
    Q_SCRIPTABLE void SwapPlayers(int player, int other);
    Q_SCRIPTABLE void Forget(const QString &address);

signals:
    Q_SCRIPTABLE void Changed();
    Q_SCRIPTABLE void HomePressed();

private:
    void connectToBus();

    UnitManager *m_units;
    Bluetooth *m_bluetooth;
    QTimer m_retry;
};
