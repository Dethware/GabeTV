#pragma once

#include <QFileInfo>
#include <QString>
#include <QUrl>

#include <optional>

namespace SwitchFormat {
struct GameInfo;
}

// What has been read from each game file, so that a file is read only once:
// ~/.cache/tvshell/switch/<hash of the file's path>/ with game.json (title id, name,
// publisher, version, and the file's size and date when it was read) and icon.jpg. An entry
// whose file has changed since counts as missing.
class GameCache
{
public:
    struct Game {
        QString titleId; // 16 hex digits
        QString name;
        QString publisher;
        QString version;
        QUrl icon; // the cached icon.jpg, or empty
    };

    // The default folder is ~/.cache/tvshell/switch.
    explicit GameCache(const QString &folder = {});

    std::optional<Game> find(const QFileInfo &file) const;
    Game store(const QFileInfo &file, const SwitchFormat::GameInfo &info);

private:
    QString entryFolder(const QFileInfo &file) const;

    QString m_folder;
};
