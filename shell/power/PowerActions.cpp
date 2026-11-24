#include "PowerActions.h"

#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcPower, "tvshell.power")

namespace {

using InterfaceMap = QMap<QString, QVariantMap>;
using ObjectMap = QMap<QDBusObjectPath, InterfaceMap>;

void callLogind(const QString &method)
{
    auto call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"),
                                               QStringLiteral("/org/freedesktop/login1"),
                                               QStringLiteral("org.freedesktop.login1.Manager"), method);
    call << false; // not interactive
    QDBusConnection::systemBus().asyncCall(call);
}

} // namespace

PowerActions::PowerActions(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<InterfaceMap>();
    qDBusRegisterMetaType<ObjectMap>();
}

void PowerActions::suspend()
{
    qCInfo(lcPower) << "suspend";
    callLogind(QStringLiteral("Suspend"));
}

void PowerActions::powerOff()
{
    qCInfo(lcPower) << "power off";
    callLogind(QStringLiteral("PowerOff"));
}

int PowerActions::turnOffControllers()
{
    return turnOffSteamControllers() + turnOffBluetoothControllers();
}

int PowerActions::turnOffSteamControllers()
{
    // Report ID 1, ID_TURN_OFF_CONTROLLER (0x9F), payload length 4, "off!"; 64 bytes in all,
    // as SDL sends Steam Controller feature reports.
    std::array<quint8, 64> report{0x01, 0x9F, 0x04, 'o', 'f', 'f', '!'};
    int count = 0;
    const QFileInfoList devices = QDir(QStringLiteral("/sys/class/hidraw")).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &device : devices) {
        QFile uevent(device.filePath() + QStringLiteral("/device/uevent"));
        if (!uevent.open(QIODevice::ReadOnly))
            continue;
        const QByteArray info = uevent.readAll();
        if (!info.contains("HID_ID=0003:000028DE:00001305") && !info.contains("HID_ID=0003:000028DE:00001304"))
            continue;
        const QByteArray node = "/dev/" + device.fileName().toLatin1();
        const int fd = ::open(node.constData(), O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            qCWarning(lcPower) << "can't open" << node << strerror(errno);
            continue;
        }
        if (::ioctl(fd, HIDIOCSFEATURE(report.size()), report.data()) >= 0) {
            qCInfo(lcPower) << "Steam Controller on" << node << "told to switch off";
            ++count;
        }
        ::close(fd);
    }
    return count;
}

int PowerActions::turnOffBluetoothControllers()
{
    auto call = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), QStringLiteral("/"),
                                               QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                               QStringLiteral("GetManagedObjects"));
    const QDBusReply<ObjectMap> reply = QDBusConnection::systemBus().call(call);
    if (!reply.isValid()) {
        qCWarning(lcPower) << "bluez:" << reply.error().message();
        return 0;
    }
    int count = 0;
    const ObjectMap objects = reply.value();
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const QVariantMap device = it.value().value(QStringLiteral("org.bluez.Device1"));
        if (device.isEmpty() || !device.value(QStringLiteral("Connected")).toBool())
            continue;
        // bluez's icon for gamepads; the remote is "input-keyboard".
        if (device.value(QStringLiteral("Icon")).toString() != QLatin1String("input-gaming"))
            continue;
        qCInfo(lcPower) << "disconnecting" << device.value(QStringLiteral("Name")).toString();
        QDBusConnection::systemBus().asyncCall(QDBusMessage::createMethodCall(
            QStringLiteral("org.bluez"), it.key().path(), QStringLiteral("org.bluez.Device1"),
            QStringLiteral("Disconnect")));
        ++count;
    }
    qCInfo(lcPower) << count << "Bluetooth controllers told to switch off";
    return count;
}
