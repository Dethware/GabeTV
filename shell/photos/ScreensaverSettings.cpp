#include "ScreensaverSettings.h"

#include <QSettings>
#include <QStandardPaths>

namespace {

QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/tvshell/tvshell.conf");
}

const QStringList Transitions = {QStringLiteral("slide"), QStringLiteral("fade"), QStringLiteral("none")};
const QStringList Backgrounds = {QStringLiteral("blur"), QStringLiteral("gradient"), QStringLiteral("black")};

} // namespace

ScreensaverSettings::ScreensaverSettings(QObject *parent)
    : QObject(parent)
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("screensaver"));
    m_albums = s.value(QStringLiteral("albums")).toStringList();
    m_interval = qMax(3, s.value(QStringLiteral("interval"), m_interval).toInt());
    const QString transition = s.value(QStringLiteral("transition"), m_transition).toString();
    if (Transitions.contains(transition))
        m_transition = transition;
    const QString background = s.value(QStringLiteral("background"), m_background).toString();
    if (Backgrounds.contains(background))
        m_background = background;
    m_showInfo = s.value(QStringLiteral("showInfo"), m_showInfo).toBool();
}

void ScreensaverSettings::save(const QString &key, const QVariant &value)
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("screensaver/") + key, value);
    emit changed();
}

void ScreensaverSettings::setAlbums(const QStringList &albums)
{
    if (m_albums == albums)
        return;
    m_albums = albums;
    save(QStringLiteral("albums"), m_albums);
}

void ScreensaverSettings::toggleAlbum(const QString &albumId)
{
    QStringList albums = m_albums;
    if (!albums.removeOne(albumId))
        albums.append(albumId);
    setAlbums(albums);
}

void ScreensaverSettings::setInterval(int seconds)
{
    seconds = qMax(3, seconds);
    if (m_interval == seconds)
        return;
    m_interval = seconds;
    save(QStringLiteral("interval"), m_interval);
}

void ScreensaverSettings::setTransition(const QString &transition)
{
    if (m_transition == transition || !Transitions.contains(transition))
        return;
    m_transition = transition;
    save(QStringLiteral("transition"), m_transition);
}

void ScreensaverSettings::setBackground(const QString &background)
{
    if (m_background == background || !Backgrounds.contains(background))
        return;
    m_background = background;
    save(QStringLiteral("background"), m_background);
}

void ScreensaverSettings::setShowInfo(bool show)
{
    if (m_showInfo == show)
        return;
    m_showInfo = show;
    save(QStringLiteral("showInfo"), m_showInfo);
}
