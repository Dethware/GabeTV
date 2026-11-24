#include "NetworkAccess.h"

#include <QNetworkDiskCache>
#include <QNetworkRequest>
#include <QStandardPaths>

namespace {

constexpr qint64 CacheBytes = 2LL * 1024 * 1024 * 1024; // 2 GiB of thumbnails and photos

} // namespace

QList<NetworkAccessManager::RequestRule> &NetworkAccessManager::rules()
{
    static QList<RequestRule> all;
    return all;
}

void NetworkAccessManager::addRequestRule(RequestRule rule)
{
    rules().append(std::move(rule));
}

NetworkAccessManager::NetworkAccessManager(QObject *parent)
    : QNetworkAccessManager(parent)
{
    auto *cache = new QNetworkDiskCache(this);
    cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
                             + QStringLiteral("/tvshell/network"));
    cache->setMaximumCacheSize(CacheBytes);
    setCache(cache);
}

QNetworkReply *NetworkAccessManager::createRequest(Operation op, const QNetworkRequest &request, QIODevice *data)
{
    QNetworkRequest adjusted(request);
    for (const RequestRule &rule : std::as_const(rules()))
        rule(adjusted);
    return QNetworkAccessManager::createRequest(op, adjusted, data);
}

QNetworkAccessManager *NetworkAccessManagerFactory::create(QObject *parent)
{
    return new NetworkAccessManager(parent);
}
