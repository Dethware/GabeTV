#include "Bluetooth.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcBluetooth, "tvcontrollers.bluetooth")

namespace {

// bluez names a device's object after its adapter and address. The machine has one adapter.
QString devicePath(const QString &address)
{
    return QStringLiteral("/org/bluez/hci0/dev_") + QString(address).replace(QLatin1Char(':'), QLatin1Char('_'));
}

} // namespace

Bluetooth::Bluetooth(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(QStringLiteral("org.bluez"), QString(), QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("PropertiesChanged"), this, SLOT(propertiesChanged(QDBusMessage)));
    bus.connect(QStringLiteral("org.bluez"), QString(), QStringLiteral("org.bluez.Device1"),
                QStringLiteral("Disconnected"), this, SLOT(deviceDisconnected(QDBusMessage)));
}

void Bluetooth::follow(const QString &address)
{
    m_followed.insert(devicePath(address));
}

void Bluetooth::propertiesChanged(const QDBusMessage &message)
{
    if (!m_followed.contains(message.path()) || message.arguments().size() < 2
        || message.arguments().at(0).toString() != QLatin1String("org.bluez.Device1"))
        return;
    const QVariantMap changed = qdbus_cast<QVariantMap>(message.arguments().at(1));
    const auto connected = changed.constFind(QStringLiteral("Connected"));
    if (connected != changed.constEnd())
        qCInfo(lcBluetooth).noquote() << "bluez:" << message.path().section(QLatin1Char('/'), -1)
                                      << (connected->toBool() ? "connected" : "disconnected");
}

void Bluetooth::deviceDisconnected(const QDBusMessage &message)
{
    if (!m_followed.contains(message.path()))
        return;
    QStringList reason;
    const QVariantList arguments = message.arguments();
    for (const QVariant &argument : arguments)
        reason.append(argument.toString());
    qCInfo(lcBluetooth).noquote() << "bluez:" << message.path().section(QLatin1Char('/'), -1)
                                  << "disconnect reason:" << reason.join(QStringLiteral(", "));
}

void Bluetooth::startSearch()
{
    // TODO: org.bluez.Adapter1.StartDiscovery, watch InterfacesAdded for Joy-Cons,
    // then Device1.Pair and Trusted = true.
}

void Bluetooth::stopSearch()
{
    // TODO: org.bluez.Adapter1.StopDiscovery.
}

void Bluetooth::switchOff(const QString &address)
{
    if (address.isEmpty())
        return;
    qCInfo(lcBluetooth).noquote() << "disconnecting" << address;
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), devicePath(address),
                                                       QStringLiteral("org.bluez.Device1"), QStringLiteral("Disconnect"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [address](QDBusPendingCallWatcher *finished) {
        const QDBusPendingReply<> reply = *finished;
        if (reply.isError())
            qCWarning(lcBluetooth).noquote() << "disconnecting" << address << "failed:" << reply.error().message();
        finished->deleteLater();
    });
}

void Bluetooth::forget(const QString &)
{
    // TODO: org.bluez.Adapter1.RemoveDevice.
}
