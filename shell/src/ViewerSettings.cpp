#include "ViewerSettings.h"

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

ViewerSettings::ViewerSettings(QObject *parent)
    : QObject(parent)
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("viewer"));
    const QString transition = s.value(QStringLiteral("transition"), m_transition).toString();
    if (Transitions.contains(transition))
        m_transition = transition;
    const QString background = s.value(QStringLiteral("background"), m_background).toString();
    if (Backgrounds.contains(background))
        m_background = background;
}

void ViewerSettings::save(const QString &key, const QVariant &value)
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("viewer/") + key, value);
    emit changed();
}

void ViewerSettings::setTransition(const QString &transition)
{
    if (m_transition == transition || !Transitions.contains(transition))
        return;
    m_transition = transition;
    save(QStringLiteral("transition"), m_transition);
}

void ViewerSettings::setBackground(const QString &background)
{
    if (m_background == background || !Backgrounds.contains(background))
        return;
    m_background = background;
    save(QStringLiteral("background"), m_background);
}
