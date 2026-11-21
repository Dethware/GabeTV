#include "ImmichConfig.h"

#include <QSettings>
#include <QStandardPaths>

bool ImmichConfig::isServerUrl(const QUrl &request) const
{
    return isValid() && request.scheme() == url.scheme() && request.host() == url.host()
        && request.port(-1) == url.port(-1);
}

ImmichConfig ImmichConfig::load()
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                           + QStringLiteral("/tvshell/immich.conf"),
                       QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("immich"));
    ImmichConfig config;
    QString url = settings.value(QStringLiteral("url")).toString();
    while (url.endsWith(u'/'))
        url.chop(1);
    config.url = QUrl(url);
    config.apiKey = settings.value(QStringLiteral("apiKey")).toString();
    return config;
}
