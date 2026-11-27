#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace SwitchFormat {

// The Switch's languages in the order of the NACP's title entries; also the names in
// icon_<Language>.dat.
const QStringList &languageNames();

// control.nacp: what the Switch shows about a game.
struct Nacp
{
    struct Title {
        QString name;
        QString publisher;
    };

    // By language, in the order of languageNames(); empty entries for languages the game
    // doesn't have.
    QList<Title> titles;
    QString displayVersion;

    // From the file's 0x4000 bytes, with compressed titles (raw deflate) inflated.
    static std::optional<Nacp> parse(const QByteArray &data);

    // The title in the first of `languages` (names as in languageNames()) the game has,
    // else in the first language it has at all.
    Title title(const QStringList &languages) const;
};

} // namespace SwitchFormat
