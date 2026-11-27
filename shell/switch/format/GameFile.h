#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>

namespace SwitchFormat {

class Keys;

// What the shell shows about a game, read from its XCI or NSP.
struct GameInfo
{
    quint64 titleId = 0;
    QString name;
    QString publisher;
    QString version;
    // A 256×256 JPEG, in the first of the preferred languages the game has an icon for.
    QByteArray icon;
};

// Reads a game file: finds the control NCA among the game's NCAs, opens its RomFS and reads
// control.nacp and the icon. `languages` are names as in languageNames(), most wanted first.
// NSP tickets are added to `keys`. nullopt with `error` set on failure.
std::optional<GameInfo> readGame(const QString &path, Keys &keys, const QStringList &languages,
                                 QString *error);

} // namespace SwitchFormat
