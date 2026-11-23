#include "DeviceWatcher.h"

#include "JoyCon.h"
#include "JoyConProtocol.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include <cerrno>
#include <cstring>

Q_LOGGING_CATEGORY(lcWatcher, "tvcontrollers.watcher")

namespace {

constexpr int RetryIntervalMs = 250;
constexpr int MaxRefusals = 20;
constexpr int MaxWaits = 20;
constexpr quint16 BusBluetooth = 0x0005;

struct HidInfo
{
    quint16 bus = 0;
    quint16 vendor = 0;
    quint16 product = 0;
    QString address;
};

// sysfs describes the HID device behind a hidraw node: HID_ID=0005:0000057E:00002007 and
// HID_UNIQ=<Bluetooth address>.
HidInfo hidInfo(const QString &name)
{
    HidInfo info;
    QFile file(QStringLiteral("/sys/class/hidraw/%1/device/uevent").arg(name));
    if (!file.open(QIODevice::ReadOnly))
        return info;
    const QList<QByteArray> lines = file.readAll().split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("HID_ID=")) {
            const QList<QByteArray> ids = line.mid(7).split(':');
            if (ids.size() == 3) {
                info.bus = ids[0].toUShort(nullptr, 16);
                info.vendor = quint16(ids[1].toUInt(nullptr, 16));
                info.product = quint16(ids[2].toUInt(nullptr, 16));
            }
        } else if (line.startsWith("HID_UNIQ=")) {
            info.address = QString::fromLatin1(line.mid(9)).toUpper();
        }
    }
    return info;
}

// The kernel's Joy-Con driver (hid_nintendo) is switched off on the TV
// (system/tv-no-hid-nintendo.conf), and a Joy-Con then has the generic driver, which
// writes nothing. Should hid_nintendo be back (a system update that dropped the file), it
// sets the Joy-Con up when it binds, ending with the player light 1, and only then creates
// its input devices: wait for those, or its writes land on top of ours. Even so it keeps
// sending for about a second more, which is why it is switched off.
bool kernelDriverDone(const QString &name)
{
    const QString device = QStringLiteral("/sys/class/hidraw/%1/device").arg(name);
    if (QFileInfo(device + QStringLiteral("/driver")).symLinkTarget().section(QLatin1Char('/'), -1)
        != QLatin1String("nintendo"))
        return true;
    static bool warned = false;
    if (!warned) {
        warned = true;
        qCWarning(lcWatcher) << "the kernel's own Joy-Con driver is active and will interfere;"
                             << "run system/install.sh again";
    }
    return !QDir(device + QStringLiteral("/input")).entryList({QStringLiteral("input*")}, QDir::Dirs).isEmpty();
}

} // namespace

DeviceWatcher::DeviceWatcher(QObject *parent)
    : QObject(parent)
{
    m_watcher.addPath(QStringLiteral("/dev"));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &DeviceWatcher::scan);
    m_retry.setSingleShot(true);
    m_retry.setInterval(RetryIntervalMs);
    connect(&m_retry, &QTimer::timeout, this, &DeviceWatcher::scan);
    // After the constructor, so the first Joy-Cons are announced to whoever connects.
    QTimer::singleShot(0, this, &DeviceWatcher::scan);
}

void DeviceWatcher::scan()
{
    const QStringList names = QDir(QStringLiteral("/dev")).entryList({QStringLiteral("hidraw*")}, QDir::System);

    const QStringList known = m_joyCons.keys();
    for (const QString &node : known) {
        if (!names.contains(node.mid(5)))
            remove(node);
    }

    bool retry = false;
    for (const QString &name : names) {
        const QString node = QStringLiteral("/dev/") + name;
        if (m_joyCons.contains(node))
            continue;
        const HidInfo info = hidInfo(name);
        // Over Bluetooth only. A Pro Controller on a USB cable speaks differently and is
        // left to Steam.
        if (info.vendor != JoyConProtocol::VendorNintendo || info.bus != BusBluetooth)
            continue;
        JoyCon::Side side;
        if (info.product == JoyConProtocol::ProductLeft)
            side = JoyCon::Side::Left;
        else if (info.product == JoyConProtocol::ProductRight)
            side = JoyCon::Side::Right;
        else if (info.product == JoyConProtocol::ProductPro)
            side = JoyCon::Side::Both;
        else
            continue;

        Pending &pending = m_pending[node];
        if (!kernelDriverDone(name)) {
            if (++pending.waited < MaxWaits) {
                retry = true;
                continue;
            }
        } else if (!pending.settled) {
            // One more interval, for the driver's last writes to reach the Joy-Con.
            pending.settled = true;
            retry = true;
            continue;
        }

        auto *joyCon = new JoyCon(node, side, info.address, this);
        if (!joyCon->open()) {
            const int error = errno;
            delete joyCon;
            if (error == EACCES && ++pending.refused < MaxRefusals) {
                retry = true;
            } else if (pending.refused <= MaxRefusals) {
                // Said once. EACCES for good means the udev rule or the group is missing.
                qCWarning(lcWatcher).noquote() << "cannot open" << node << "for a Joy-Con:" << std::strerror(error);
                pending.refused = MaxRefusals + 1;
            }
            continue;
        }
        m_pending.remove(node);
        m_joyCons.insert(node, joyCon);
        connect(joyCon, &JoyCon::switchedOff, this, [this, joyCon] { emit joyConSwitchedOff(joyCon->address()); });
        connect(joyCon, &JoyCon::lost, this, [this, node] { remove(node); });
        connect(joyCon, &JoyCon::ready, this, [this, joyCon] { emit joyConAdded(joyCon); });
    }

    // Forget nodes that are gone, so a reconnect starts fresh.
    for (auto it = m_pending.begin(); it != m_pending.end();)
        it = names.contains(it.key().mid(5)) ? std::next(it) : m_pending.erase(it);

    if (retry)
        m_retry.start();
}

void DeviceWatcher::remove(const QString &node)
{
    JoyCon *joyCon = m_joyCons.take(node);
    if (!joyCon)
        return;
    if (joyCon->isReady())
        emit joyConRemoved(joyCon);
    joyCon->deleteLater();
    // If the node is still there (the Joy-Con only went quiet), it gets another try.
    m_retry.start();
}
