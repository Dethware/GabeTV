#include "YouTubeSettings.h"

#include <QSettings>
#include <QStandardPaths>

namespace {

QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/tvshell/tvshell.conf");
}

const QStringList Categories = {
    QStringLiteral("sponsor"), QStringLiteral("selfpromo"),      QStringLiteral("interaction"), QStringLiteral("intro"),
    QStringLiteral("outro"),   QStringLiteral("preview"),        QStringLiteral("music_offtopic"), QStringLiteral("filler"),
};

} // namespace

YouTubeSettings::YouTubeSettings(QObject *parent)
    : QObject(parent)
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    const QVariant saved = s.value(QStringLiteral("youtube/skipCategories"));
    if (!saved.isValid())
        return;
    m_skipCategories.clear();
    for (const QString &category : saved.toStringList()) {
        if (Categories.contains(category) && !m_skipCategories.contains(category))
            m_skipCategories << category;
    }
}

void YouTubeSettings::toggleSkip(const QString &category)
{
    if (!Categories.contains(category))
        return;
    if (!m_skipCategories.removeOne(category))
        m_skipCategories << category;
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("youtube/skipCategories"), m_skipCategories);
    emit changed();
}
