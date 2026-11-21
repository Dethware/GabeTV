#include "NetworkAccess.h"

#include <QNetworkDiskCache>
#include <QNetworkRequest>
#include <QStandardPaths>

namespace {

constexpr qint64 CacheBytes = 2LL * 1024 * 1024 * 1024; // 2 GiB of thumbnails and photos

} // namespace

NetworkAccessManager::NetworkAccessManager(QObject *parent)
    : QNetworkAccessManager(parent)
    , m_immich(ImmichConfig::load())
{
    auto *cache = new QNetworkDiskCache(this);
    cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
                             + QStringLiteral("/tvshell/network"));
    cache->setMaximumCacheSize(CacheBytes);
    setCache(cache);
}

QNetworkReply *NetworkAccessManager::createRequest(Operation op, const QNetworkRequest &request, QIODevice *data)
{
    if (!m_immich.isServerUrl(request.url()))
        return QNetworkAccessManager::createRequest(op, request, data);
    QNetworkRequest withKey(request);
    withKey.setRawHeader("x-api-key", m_immich.apiKey.toUtf8());
    // Images of an asset don't change: serve them from the cache when we have them.
    // (Lists like albums and memories do change; those follow the server's cache headers.)
    const QString path = request.url().path();
    if (path.endsWith(QLatin1String("/thumbnail")) || path.endsWith(QLatin1String("/original")))
        withKey.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    return QNetworkAccessManager::createRequest(op, withKey, data);
}

QNetworkAccessManager *NetworkAccessManagerFactory::create(QObject *parent)
{
    return new NetworkAccessManager(parent);
}
