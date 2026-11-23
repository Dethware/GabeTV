#include "Bluetooth.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QLoggingCategory>

#include <functional>

Q_LOGGING_CATEGORY(lcBluetooth, "tvcontrollers.bluetooth")

namespace {

const QString Bluez = QStringLiteral("org.bluez");
// The machine has one adapter.
const QString AdapterPath = QStringLiteral("/org/bluez/hci0");
const QString AdapterInterface = QStringLiteral("org.bluez.Adapter1");
const QString DeviceInterface = QStringLiteral("org.bluez.Device1");
const QString AgentPath = QStringLiteral("/local/tvcontrollers/agent");

constexpr int SearchTimeoutMs = 120000;
// Pairing waits for the Joy-Con; bluez gives up by itself well inside this.
constexpr int PairTimeoutMs = 60000;

// bluez names a device's object after its adapter and address.
QString devicePath(const QString &address)
{
    return AdapterPath + QStringLiteral("/dev_") + QString(address).replace(QLatin1Char(':'), QLatin1Char('_'));
}

QString addressOf(const QString &path)
{
    return path.section(QStringLiteral("/dev_"), -1).replace(QLatin1Char('_'), QLatin1Char(':'));
}

using Done = std::function<void(const QString &error)>;

// A call to bluez; `done` gets an empty string or the error's message.
void call(QObject *context, const QString &path, const QString &interface, const QString &method,
          const QVariantList &arguments = {}, Done done = {}, int timeoutMs = -1)
{
    QDBusMessage message = QDBusMessage::createMethodCall(Bluez, path, interface, method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, timeoutMs), context);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context,
                     [method, done = std::move(done)](QDBusPendingCallWatcher *finished) {
                         const QDBusPendingReply<> reply = *finished;
                         const QString error = reply.isError() ? reply.error().message() : QString();
                         if (done)
                             done(error);
                         else if (!error.isEmpty())
                             qCWarning(lcBluetooth).noquote() << method << "failed:" << error;
                         finished->deleteLater();
                     });
}

} // namespace

PairingAgent::PairingAgent(const QSet<QString> *pairing, QObject *parent)
    : QObject(parent)
    , m_pairing(pairing)
{
}

void PairingAgent::allow(const QDBusObjectPath &device)
{
    if (m_pairing->contains(device.path()))
        return;
    qCWarning(lcBluetooth).noquote() << "refused a pairing question about" << device.path();
    sendErrorReply(QStringLiteral("org.bluez.Error.Rejected"), QStringLiteral("not a device this service is pairing"));
}

Bluetooth::Bluetooth(QObject *parent)
    : QObject(parent)
    , m_agent(new PairingAgent(&m_pairing, this))
{
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(Bluez, QString(), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"),
                this, SLOT(propertiesChanged(QDBusMessage)));
    bus.connect(Bluez, QString(), DeviceInterface, QStringLiteral("Disconnected"), this,
                SLOT(deviceDisconnected(QDBusMessage)));
    bus.connect(Bluez, QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                QStringLiteral("InterfacesAdded"), this, SLOT(interfacesAdded(QDBusMessage)));

    // The agent bluez asks during pairings this service starts.
    if (!bus.registerObject(AgentPath, m_agent, QDBusConnection::ExportScriptableSlots))
        qCWarning(lcBluetooth) << "cannot export the pairing agent";
    call(this, QStringLiteral("/org/bluez"), QStringLiteral("org.bluez.AgentManager1"), QStringLiteral("RegisterAgent"),
         {QVariant::fromValue(QDBusObjectPath(AgentPath)), QStringLiteral("NoInputNoOutput")});

    m_searchTimeout.setSingleShot(true);
    m_searchTimeout.setInterval(SearchTimeoutMs);
    connect(&m_searchTimeout, &QTimer::timeout, this, &Bluetooth::stopSearch);
}

void Bluetooth::startSearch()
{
    m_searchTimeout.start();
    if (m_searching)
        return;
    // Joy-Cons use classic Bluetooth; leave the low-energy devices out of the search.
    call(this, AdapterPath, AdapterInterface, QStringLiteral("SetDiscoveryFilter"),
         {QVariantMap{{QStringLiteral("Transport"), QStringLiteral("bredr")}}});
    call(this, AdapterPath, AdapterInterface, QStringLiteral("StartDiscovery"), {}, [this](const QString &error) {
        if (!error.isEmpty()) {
            qCWarning(lcBluetooth).noquote() << "cannot search:" << error;
            m_searchTimeout.stop();
            return;
        }
        qCInfo(lcBluetooth) << "searching for Joy-Cons in sync mode";
        setSearching(true);
        // One that bluez has already seen doesn't get announced again.
        QDBusMessage objects = QDBusMessage::createMethodCall(Bluez, QStringLiteral("/"),
                                                              QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                                              QStringLiteral("GetManagedObjects"));
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(objects), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
            const QDBusMessage reply = finished->reply();
            finished->deleteLater();
            if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
                return;
            QMap<QDBusObjectPath, QMap<QString, QVariantMap>> all;
            reply.arguments().at(0).value<QDBusArgument>() >> all;
            for (auto it = all.cbegin(); it != all.cend(); ++it) {
                const auto device = it.value().constFind(DeviceInterface);
                if (device != it.value().cend())
                    consider(it.key().path(), device.value());
            }
        });
    });
}

void Bluetooth::stopSearch()
{
    m_searchTimeout.stop();
    if (!m_searching)
        return;
    call(this, AdapterPath, AdapterInterface, QStringLiteral("StopDiscovery"), {}, [](const QString &) {});
    qCInfo(lcBluetooth) << "search ended";
    setSearching(false);
}

void Bluetooth::setSearching(bool searching)
{
    if (m_searching == searching)
        return;
    m_searching = searching;
    emit searchingChanged();
}

void Bluetooth::consider(const QString &path, const QVariantMap &device)
{
    if (!m_searching || m_pairing.contains(path))
        return;
    const QString name = device.value(QStringLiteral("Name")).toString();
    // "Joy-Con (L)", "Joy-Con (R)" and "Pro Controller".
    if ((!name.startsWith(QLatin1String("Joy-Con")) && name != QLatin1String("Pro Controller"))
        || device.value(QStringLiteral("Paired")).toBool())
        return;

    qCInfo(lcBluetooth).noquote() << "found" << name << addressOf(path) << "- pairing";
    m_pairing.insert(path);
    call(this, path, DeviceInterface, QStringLiteral("Pair"), {},
         [this, path, name](const QString &error) { pairingDone(path, name, error); }, PairTimeoutMs);
}

void Bluetooth::pairingDone(const QString &path, const QString &name, const QString &error)
{
    m_pairing.remove(path);
    if (!error.isEmpty()) {
        // "Already Exists": it was paired after all, and only its name arrived late.
        qCInfo(lcBluetooth).noquote() << "pairing" << name << addressOf(path) << "failed:" << error;
        return;
    }
    qCInfo(lcBluetooth).noquote() << "paired" << name << addressOf(path);
    // Trusted: it may connect by itself from now on, on a button press.
    call(this, path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"),
         {DeviceInterface, QStringLiteral("Trusted"), QVariant::fromValue(QDBusVariant(true))});
    call(this, path, DeviceInterface, QStringLiteral("Connect"), {}, [name](const QString &connectError) {
        if (!connectError.isEmpty())
            qCInfo(lcBluetooth).noquote() << "connecting" << name << "failed:" << connectError
                                          << "- a button press on it connects it";
    });
    m_followed.insert(path);
    emit paired(addressOf(path), name);
}

void Bluetooth::switchOff(const QString &address)
{
    if (address.isEmpty())
        return;
    qCInfo(lcBluetooth).noquote() << "disconnecting" << address;
    call(this, devicePath(address), DeviceInterface, QStringLiteral("Disconnect"));
}

void Bluetooth::forget(const QString &address)
{
    if (address.isEmpty())
        return;
    qCInfo(lcBluetooth).noquote() << "forgetting" << address;
    call(this, AdapterPath, AdapterInterface, QStringLiteral("RemoveDevice"),
         {QVariant::fromValue(QDBusObjectPath(devicePath(address)))});
}

void Bluetooth::follow(const QString &address)
{
    m_followed.insert(devicePath(address));
}

void Bluetooth::interfacesAdded(const QDBusMessage &message)
{
    if (message.arguments().size() < 2)
        return;
    QMap<QString, QVariantMap> interfaces;
    message.arguments().at(1).value<QDBusArgument>() >> interfaces;
    const auto device = interfaces.constFind(DeviceInterface);
    if (device != interfaces.cend())
        consider(message.arguments().at(0).value<QDBusObjectPath>().path(), device.value());
}

void Bluetooth::propertiesChanged(const QDBusMessage &message)
{
    if (message.arguments().size() < 2 || message.arguments().at(0).toString() != DeviceInterface)
        return;
    const QVariantMap changed = qdbus_cast<QVariantMap>(message.arguments().at(1));
    // A device found without its name gets it a moment later.
    if (changed.contains(QStringLiteral("Name")))
        consider(message.path(), changed);
    if (!m_followed.contains(message.path()))
        return;
    const auto connected = changed.constFind(QStringLiteral("Connected"));
    if (connected != changed.constEnd())
        qCInfo(lcBluetooth).noquote() << "bluez:" << addressOf(message.path())
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
    qCInfo(lcBluetooth).noquote() << "bluez:" << addressOf(message.path()) << "disconnect reason:"
                                  << reason.join(QStringLiteral(", "));
}
