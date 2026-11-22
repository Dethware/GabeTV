#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

// YouTube options, saved in ~/.config/tvshell/tvshell.conf, [youtube] group. Changes are saved
// at once.
class YouTubeSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // SponsorBlock categories to skip ("sponsor", "selfpromo", "interaction", "intro", "outro",
    // "preview", "music_offtopic", "filler"). Empty: SponsorBlock isn't asked at all.
    Q_PROPERTY(QStringList skipCategories READ skipCategories NOTIFY changed)

public:
    explicit YouTubeSettings(QObject *parent = nullptr);

    QStringList skipCategories() const { return m_skipCategories; }
    Q_INVOKABLE void toggleSkip(const QString &category);

signals:
    void changed();

private:
    QStringList m_skipCategories = {QStringLiteral("sponsor")};
};
