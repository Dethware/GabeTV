#pragma once

#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantMap>

// Answers bluez's questions during a pairing the service started. A Joy-Con pairs without
// a code, so there is nothing to ask anyone; anything about another device is refused.
// It is this service's own agent, not the system's default one.
class PairingAgent : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")

public:
    PairingAgent(const QSet<QString> *pairing, QObject *parent);

public slots:
    Q_SCRIPTABLE void Release() {}
    Q_SCRIPTABLE void Cancel() {}
    Q_SCRIPTABLE void RequestAuthorization(const QDBusObjectPath &device) { allow(device); }
    Q_SCRIPTABLE void RequestConfirmation(const QDBusObjectPath &device, uint) { allow(device); }
    Q_SCRIPTABLE void AuthorizeService(const QDBusObjectPath &device, const QString &) { allow(device); }

private:
    void allow(const QDBusObjectPath &device);

    const QSet<QString> *m_pairing;
};

// The Bluetooth side of a Joy-Con, through bluez on the system D-Bus: searching for
// Joy-Cons held in sync mode and pairing them, forgetting one, and dropping a connection.
// The connection itself and the pairing keys stay bluez's business; a connected Joy-Con
// reaches the service as a HID node (DeviceWatcher).
class Bluetooth : public QObject
{
    Q_OBJECT

public:
    explicit Bluetooth(QObject *parent = nullptr);

    bool searching() const { return m_searching; }

    // Looks for Joy-Cons in sync mode (sync button held until the lights sweep) and pairs,
    // trusts and connects each one found. Ends by itself after two minutes.
    void startSearch();
    void stopSearch();
    // Disconnects it. Also used to clear a connection whose Joy-Con was switched off.
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
    void interfacesAdded(const QDBusMessage &message);

private:
    void setSearching(bool searching);
    void consider(const QString &path, const QVariantMap &device);
    void pairingDone(const QString &path, const QString &name, const QString &error);

    bool m_searching = false;
    QTimer m_searchTimeout;
    QSet<QString> m_followed; // object paths
    QSet<QString> m_pairing; // object paths
    PairingAgent *m_agent;
};
