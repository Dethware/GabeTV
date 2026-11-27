#include "GameCache.h"

#include "format/GameFile.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

// What identifies the version of a file that was read: its size and modification time.
QJsonObject fileStamp(const QFileInfo &file)
{
    return {
        {QStringLiteral("fileSize"), QString::number(file.size())},
        {QStringLiteral("fileModified"), QString::number(file.lastModified().toMSecsSinceEpoch())},
    };
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QSaveFile out(path);
    return out.open(QIODevice::WriteOnly) && out.write(data) == data.size() && out.commit();
}

} // namespace

GameCache::GameCache(const QString &folder)
    : m_folder(folder.isEmpty()
                   ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/tvshell/switch")
                   : folder)
{
}

std::optional<GameCache::Game> GameCache::find(const QFileInfo &file) const
{
    const QString folder = entryFolder(file);
    QFile json(folder + QStringLiteral("/game.json"));
    if (!json.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonObject entry = QJsonDocument::fromJson(json.readAll()).object();
    const QJsonObject stamp = fileStamp(file);
    for (auto it = stamp.begin(); it != stamp.end(); ++it) {
        if (entry.value(it.key()) != it.value())
            return std::nullopt;
    }
    const QString icon = folder + QStringLiteral("/icon.jpg");
    return Game{entry.value(QStringLiteral("titleId")).toString(), entry.value(QStringLiteral("name")).toString(),
                entry.value(QStringLiteral("publisher")).toString(), entry.value(QStringLiteral("version")).toString(),
                QFile::exists(icon) ? QUrl::fromLocalFile(icon) : QUrl()};
}

GameCache::Game GameCache::store(const QFileInfo &file, const SwitchFormat::GameInfo &info)
{
    Game game{QStringLiteral("%1").arg(info.titleId, 16, 16, QLatin1Char('0')).toUpper(), info.name,
              info.publisher, info.version, {}};

    // Without a cache the game is still shown, only read again next time.
    const QString folder = entryFolder(file);
    if (!QDir().mkpath(folder))
        return game;
    const QString icon = folder + QStringLiteral("/icon.jpg");
    if (info.icon.isEmpty())
        QFile::remove(icon);
    else if (writeFile(icon, info.icon))
        game.icon = QUrl::fromLocalFile(icon);

    QJsonObject entry = fileStamp(file);
    entry.insert(QStringLiteral("path"), file.absoluteFilePath());
    entry.insert(QStringLiteral("titleId"), game.titleId);
    entry.insert(QStringLiteral("name"), game.name);
    entry.insert(QStringLiteral("publisher"), game.publisher);
    entry.insert(QStringLiteral("version"), game.version);
    // Written last: an entry counts only once its icon is in place.
    writeFile(folder + QStringLiteral("/game.json"), QJsonDocument(entry).toJson());
    return game;
}

QString GameCache::entryFolder(const QFileInfo &file) const
{
    const QByteArray hash = QCryptographicHash::hash(file.absoluteFilePath().toUtf8(), QCryptographicHash::Sha1);
    return m_folder + u'/' + QString::fromLatin1(hash.toHex().left(16));
}
