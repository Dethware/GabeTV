#pragma once

#include <QDBusMessage>
#include <QObject>
#include <QSet>
#include <QString>

// The Bluetooth side of a Joy-Con, through bluez on the system D-Bus: searching for a
// Joy-Con held in sync mode, pairing and trusting it, forgetting it, and switching a
// connected one off. The connection itself and the pairing keys stay bluez's business; a
// connected Joy-Con reaches the service as a HID node (DeviceWatcher).
//
// SCAFFOLD: disconnecting and following a device's events work; searching, pairing and
// forgetting are the interface only.
class Bluetooth : public QObject
{
    Q_OBJECT

public:
    explicit Bluetooth(QObject *parent = nullptr);

    bool searching() const { return m_searching; }

    // Looks for Joy-Cons in sync mode and pairs each one found, until stopped.
    void startSearch();
    void stopSearch();
    // Disconnects it; a Joy-Con switches itself off then.
    void switchOff(const QString &address);
    // Removes the pairing.
    void forget(const QString &address);
    // Logs what bluez reports about this device from now on: connected, disconnected and why.
    void follow(const QString &address);

signals:
    void searchingChanged();
    // A new Joy-Con was paired.
    void paired(const QString &address, const QString &name);

private slots:
    void propertiesChanged(const QDBusMessage &message);
    void deviceDisconnected(const QDBusMessage &message);

private:
    bool m_searching = false;
    QSet<QString> m_followed; // object paths
};
