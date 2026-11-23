#pragma once

#include "JoyCon.h"

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>

// Finds the Joy-Cons that are connected: watches /dev for raw HID nodes coming and going
// and looks each one up in sysfs. Bluetooth itself (pairing, the connection) stays with
// bluez; a connected Joy-Con simply appears here as a node, and disappears when it leaves.
class DeviceWatcher : public QObject
{
    Q_OBJECT

public:
    explicit DeviceWatcher(QObject *parent = nullptr);

    QList<JoyCon *> joyCons() const { return m_joyCons.values(); }

signals:
    // Open and answering.
    void joyConAdded(JoyCon *joyCon);
    // About to be deleted.
    void joyConRemoved(JoyCon *joyCon);
    // JoyCon::switchedOff() of any of them.
    void joyConSwitchedOff(const QString &address);

private:
    void scan();
    void remove(const QString &node);

    // A Joy-Con's node that isn't open yet.
    struct Pending
    {
        int waited = 0; // scans spent waiting for the kernel's driver
        bool settled = false;
        int refused = 0; // failed opens
    };

    QFileSystemWatcher m_watcher;
    // A node shows up before udev has set its group and before the kernel's own driver has
    // finished with the Joy-Con; both are waited for by scanning again.
    QTimer m_retry;
    QHash<QString, Pending> m_pending;
    QHash<QString, JoyCon *> m_joyCons;
};
