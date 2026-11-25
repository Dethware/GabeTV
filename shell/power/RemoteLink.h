#pragma once

#include <QDBusMessage>
#include <QObject>
#include <QtQml/qqmlregistration.h>

// Whether the remote is connected right now, followed through bluez on the system bus
// (no root). The remote is the paired device that offers the Android TV Voice service
// (ab5e0001-…, in its Device1.UUIDs).
//
// What the idle chain needs it for (../docs/gabecube-findings.md, "Two ways of going to
// sleep"): the remote can wake the machine only if it let go of its connection by itself
// before the machine went to sleep, about 30 minutes after it was last used. Connected at
// sleep, it can't. And after a wake it connects within a fraction of a second if it was the
// one that woke the machine.
//
// Changes arrive as bluez's PropertiesChanged on the device, so a connection is seen as it
// happens; a device paired later (InterfacesAdded) is picked up too.
class RemoteLink : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // A remote is paired.
    Q_PROPERTY(bool paired READ paired NOTIFY pairedChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)

public:
    explicit RemoteLink(QObject *parent = nullptr);

    bool paired() const { return !m_devicePath.isEmpty(); }
    bool connected() const { return m_connected; }

signals:
    void pairedChanged();
    void connectedChanged();

private slots:
    void propertiesChanged(const QDBusMessage &message);
    void interfacesAdded(const QDBusMessage &message);
    void interfacesRemoved(const QDBusMessage &message);

private:
    // Looks through bluez's objects for the remote and reads whether it is connected.
    void find();
    void setConnected(bool connected);

    QString m_devicePath;
    bool m_connected = false;
};
