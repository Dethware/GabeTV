#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

// What the power dialog can do: suspend or power off the machine (logind, allowed for the
// active session without root) and switch off controllers.
//
// Controllers:
// - Steam Controllers on a wireless puck (28de:1304 / 28de:1305): the classic Steam Controller
//   "turn off" feature report (0x9F "off!") on each of the puck's HID interfaces, through
//   hidraw. `deck` may open those (uaccess ACL), alongside Steam. Interfaces without a
//   controller refuse it (EPIPE).
// - Bluetooth gamepads: disconnected through bluez, which makes most of them switch off.
//   The remote (a Bluetooth keyboard) is left alone.
class PowerActions : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit PowerActions(QObject *parent = nullptr);

    Q_INVOKABLE void suspend();
    Q_INVOKABLE void powerOff();
    // Returns how many controllers were told to switch off.
    Q_INVOKABLE int turnOffControllers();

private:
    int turnOffSteamControllers();
    int turnOffBluetoothControllers();
};
