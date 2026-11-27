#include "Keys.h"

#include <QDir>
#include <QFile>
#include <QVersionNumber>

#include <cctype>
#include <functional>

namespace SwitchFormat {

namespace {

// The "name = hex value" lines of a key file, by name. Lines that don't fit are skipped.
// False if the file can't be read.
bool readKeyFile(const QString &path, const std::function<void(const QString &, const QByteArray &)> &add)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        const qsizetype equals = line.indexOf('=');
        if (equals < 0)
            continue;
        const QString name = QString::fromLatin1(line.left(equals).trimmed()).toLower();
        const QByteArray hex = line.mid(equals + 1).trimmed();
        if (name.isEmpty() || hex.isEmpty() || hex.size() % 2 != 0)
            continue;
        bool isHex = true;
        for (const char c : hex)
            isHex = isHex && std::isxdigit(static_cast<unsigned char>(c));
        if (isHex)
            add(name, QByteArray::fromHex(hex));
    }
    return true;
}

} // namespace

QString Keys::newestFolder(const QString &root)
{
    const QDir dir(root);
    QString newest;
    QVersionNumber newestVersion;
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QVersionNumber version = QVersionNumber::fromString(name);
        if (!version.isNull() && version > newestVersion) {
            newestVersion = version;
            newest = dir.filePath(name);
        }
    }
    return newest;
}

bool Keys::load(const QString &folder, QString *error)
{
    const QString prodKeys = QDir(folder).filePath(QStringLiteral("prod.keys"));
    if (!readKeyFile(prodKeys, [this](const QString &name, const QByteArray &value) { m_keys.insert(name, value); })) {
        if (error)
            *error = prodKeys + QStringLiteral(": can't be read");
        return false;
    }
    // Optional: a game whose NCAs have a rights id finds its title key here or in its own ticket.
    readKeyFile(QDir(folder).filePath(QStringLiteral("title.keys")), [this](const QString &name, const QByteArray &value) {
        if (name.size() == 32 && value.size() == 16)
            m_titleKeys.insert(QByteArray::fromHex(name.toLatin1()), value);
    });
    return true;
}

QByteArray Keys::headerKey() const
{
    return m_keys.value(QStringLiteral("header_key"));
}

QByteArray Keys::keyAreaKey(int generation) const
{
    return m_keys.value(QStringLiteral("key_area_key_application_%1").arg(keyIndex(generation), 2, 16, QLatin1Char('0')));
}

QByteArray Keys::titleKek(int generation) const
{
    return m_keys.value(QStringLiteral("titlekek_%1").arg(keyIndex(generation), 2, 16, QLatin1Char('0')));
}

QByteArray Keys::titleKey(const QByteArray &rightsId) const
{
    return m_titleKeys.value(rightsId);
}

void Keys::addTitleKey(const QByteArray &rightsId, const QByteArray &encryptedKey)
{
    m_titleKeys.insert(rightsId, encryptedKey);
}

int Keys::keyIndex(int generation)
{
    return generation > 0 ? generation - 1 : 0;
}

} // namespace SwitchFormat
