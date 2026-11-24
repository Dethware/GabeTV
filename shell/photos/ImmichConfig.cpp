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

std::function<void(QNetworkRequest &)> ImmichConfig::requestRule()
{
    return [config = load()](QNetworkRequest &request) {
        if (!config.isServerUrl(request.url()))
            return;
        request.setRawHeader("x-api-key", config.apiKey.toUtf8());
        const QString path = request.url().path();
        if (path.endsWith(QLatin1String("/thumbnail")) || path.endsWith(QLatin1String("/original")))
            request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    };
}
