#include "RemoteLink.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcRemoteLink, "tvshell.remote")

namespace {

using InterfaceMap = QMap<QString, QVariantMap>;
using ObjectMap = QMap<QDBusObjectPath, InterfaceMap>;

const QString Bluez = QStringLiteral("org.bluez");
const QString Device = QStringLiteral("org.bluez.Device1");
const QString ObjectManager = QStringLiteral("org.freedesktop.DBus.ObjectManager");
const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");

// Android TV Voice, the service that makes a remote a TV remote here.
const QString VoiceServiceUuid = QStringLiteral("ab5e0001-5a21-4f05-bc7d-af01f617b664");

bool isRemote(const QVariantMap &device)
{
    return device.value(QStringLiteral("Paired")).toBool()
        && device.value(QStringLiteral("UUIDs")).toStringList().contains(VoiceServiceUuid);
}

} // namespace

RemoteLink::RemoteLink(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<InterfaceMap>();
    qDBusRegisterMetaType<ObjectMap>();

    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(Bluez, QStringLiteral("/"), ObjectManager, QStringLiteral("InterfacesAdded"), this,
                SLOT(interfacesAdded(QDBusMessage)));
    bus.connect(Bluez, QStringLiteral("/"), ObjectManager, QStringLiteral("InterfacesRemoved"), this,
                SLOT(interfacesRemoved(QDBusMessage)));
    find();
}

void RemoteLink::find()
{
    auto call = QDBusMessage::createMethodCall(Bluez, QStringLiteral("/"), ObjectManager, QStringLiteral("GetManagedObjects"));
    const QDBusReply<ObjectMap> reply = QDBusConnection::systemBus().call(call);
    if (!reply.isValid()) {
        qCWarning(lcRemoteLink) << "bluez:" << reply.error().message();
        return;
    }
    const ObjectMap objects = reply.value();
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const QVariantMap device = it.value().value(Device);
        if (device.isEmpty() || !isRemote(device))
            continue;
        const QString path = it.key().path();
        if (path != m_devicePath) {
            if (!m_devicePath.isEmpty())
                QDBusConnection::systemBus().disconnect(Bluez, m_devicePath, Properties, QStringLiteral("PropertiesChanged"),
                                                        this, SLOT(propertiesChanged(QDBusMessage)));
            m_devicePath = path;
            // An empty path in connect() doesn't match all objects: watch the device itself.
            QDBusConnection::systemBus().connect(Bluez, path, Properties, QStringLiteral("PropertiesChanged"), this,
                                                 SLOT(propertiesChanged(QDBusMessage)));
            qCInfo(lcRemoteLink) << "the remote is" << device.value(QStringLiteral("Name")).toString() << "at" << path;
            emit pairedChanged();
        }
        setConnected(device.value(QStringLiteral("Connected")).toBool());
        return;
    }
}

void RemoteLink::propertiesChanged(const QDBusMessage &message)
{
    const QList<QVariant> args = message.arguments();
    if (args.size() < 2 || message.path() != m_devicePath || args.at(0).toString() != Device)
        return;
    const QVariantMap changed = qdbus_cast<QVariantMap>(args.at(1));
    if (changed.contains(QStringLiteral("Connected")))
        setConnected(changed.value(QStringLiteral("Connected")).toBool());
}

void RemoteLink::interfacesAdded(const QDBusMessage &message)
{
    // While no remote is known: a device appearing, or a device's services appearing as it
    // connects (which a newly paired remote does).
    if (!m_devicePath.isEmpty())
        return;
    const QList<QVariant> args = message.arguments();
    if (args.size() < 2)
        return;
    const InterfaceMap interfaces = qdbus_cast<InterfaceMap>(args.at(1));
    if (interfaces.contains(Device) || interfaces.contains(QStringLiteral("org.bluez.GattService1")))
        find();
}

void RemoteLink::interfacesRemoved(const QDBusMessage &message)
{
    const QList<QVariant> args = message.arguments();
    if (args.size() < 2 || qdbus_cast<QDBusObjectPath>(args.at(0)).path() != m_devicePath)
        return;
    if (!qdbus_cast<QStringList>(args.at(1)).contains(Device))
        return;
    qCInfo(lcRemoteLink) << "the remote was removed";
    QDBusConnection::systemBus().disconnect(Bluez, m_devicePath, Properties, QStringLiteral("PropertiesChanged"), this,
                                            SLOT(propertiesChanged(QDBusMessage)));
    m_devicePath.clear();
    setConnected(false);
    emit pairedChanged();
}

void RemoteLink::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    qCInfo(lcRemoteLink) << (connected ? "remote connected" : "remote let go");
    emit connectedChanged();
}
