#include "ImmichClient.h"

#include "NetworkAccess.h"

#include <QCollator>
#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QUrlQuery>

#include <algorithm>

Q_LOGGING_CATEGORY(lcImmich, "tvshell.immich")

namespace {

QVariantMap asset(const QString &id, double ratio, bool isImage)
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("ratio"), ratio}, {QStringLiteral("isImage"), isImage}};
}

} // namespace

ImmichClient::ImmichClient(QObject *parent)
    : QObject(parent)
    , m_config(ImmichConfig::load())
    , m_network(new NetworkAccessManager(this))
{
    if (!configured())
        qCInfo(lcImmich) << "not configured (~/.config/tvshell/immich.conf)";
}

void ImmichClient::get(const QString &path, const QList<std::pair<QString, QString>> &query, JsonHandler handler)
{
    QUrl url = m_config.url;
    url.setPath(url.path() + QStringLiteral("/api") + path);
    QUrlQuery q;
    for (const auto &[key, value] : query)
        q.addQueryItem(key, value);
    url.setQuery(q);
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, handler] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qCWarning(lcImmich) << "GET" << path << "failed:" << status << reply->errorString();
            setError(status == 401 || status == 403 ? tr("Immich rejected the API key (%1).").arg(status)
                                                    : tr("Can't reach Immich: %1").arg(reply->errorString()));
            return;
        }
        handler(reply->readAll());
    });
}

void ImmichClient::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

void ImmichClient::refresh()
{
    if (!configured())
        return;
    setError(QString());

    get(QStringLiteral("/albums"), {}, [this](const QByteArray &json) {
        QVariantList albums;
        const QJsonArray array = QJsonDocument::fromJson(json).array();
        for (const QJsonValue &value : array) {
            const QJsonObject album = value.toObject();
            if (album.value(QStringLiteral("assetCount")).toInt() == 0)
                continue;
            albums.append(QVariantMap{
                {QStringLiteral("id"), album.value(QStringLiteral("id")).toString()},
                {QStringLiteral("name"), album.value(QStringLiteral("albumName")).toString()},
                {QStringLiteral("count"), album.value(QStringLiteral("assetCount")).toInt()},
                {QStringLiteral("cover"), album.value(QStringLiteral("albumThumbnailAssetId")).toString()},
            });
        }
        // Stable positions: by name.
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::sort(albums.begin(), albums.end(), [&collator](const QVariant &a, const QVariant &b) {
            return collator.compare(a.toMap().value(QStringLiteral("name")).toString(),
                                    b.toMap().value(QStringLiteral("name")).toString()) < 0;
        });
        qCInfo(lcImmich) << albums.size() << "albums";
        m_albums = albums;
        emit albumsChanged();
    });

    const QString today = QDate::currentDate().toString(Qt::ISODate);
    get(QStringLiteral("/memories"), {{QStringLiteral("for"), today}}, [this](const QByteArray &json) {
        QVariantList memories;
        const QJsonArray array = QJsonDocument::fromJson(json).array();
        const int thisYear = QDate::currentDate().year();
        for (const QJsonValue &value : array) {
            const QJsonObject memory = value.toObject();
            QVariantList assets;
            for (const QJsonValue &a : memory.value(QStringLiteral("assets")).toArray()) {
                const QJsonObject o = a.toObject();
                const double width = o.value(QStringLiteral("width")).toDouble();
                const double height = o.value(QStringLiteral("height")).toDouble();
                assets.append(asset(o.value(QStringLiteral("id")).toString(), height > 0 ? width / height : 1.5,
                                    o.value(QStringLiteral("type")).toString() == QLatin1String("IMAGE")));
            }
            if (assets.isEmpty())
                continue;
            const int year = memory.value(QStringLiteral("data")).toObject().value(QStringLiteral("year")).toInt();
            const int yearsAgo = thisYear - year;
            const QString title = year <= 0 ? tr("Memory") : yearsAgo == 1 ? tr("1 year ago") : tr("%1 years ago").arg(yearsAgo);
            memories.append(QVariantMap{
                {QStringLiteral("id"), memory.value(QStringLiteral("id")).toString()},
                {QStringLiteral("title"), title},
                {QStringLiteral("assets"), assets},
            });
        }
        qCInfo(lcImmich) << memories.size() << "memories for today";
        m_memories = memories;
        emit memoriesChanged();
    });
}

void ImmichClient::loadAlbum(const QString &albumId)
{
    if (!configured())
        return;
    m_albumId = albumId;
    m_albumAssets.clear();
    m_albumLoading = true;
    emit albumAssetsChanged();
    get(QStringLiteral("/timeline/buckets"), {{QStringLiteral("albumId"), albumId}}, [this, albumId](const QByteArray &json) {
        if (albumId != m_albumId)
            return;
        QStringList buckets;
        for (const QJsonValue &value : QJsonDocument::fromJson(json).array())
            buckets.append(value.toObject().value(QStringLiteral("timeBucket")).toString());
        loadAlbumBuckets(albumId, buckets);
    });
}

// One month bucket at a time, in order; the grid fills as they arrive.
void ImmichClient::loadAlbumBuckets(const QString &albumId, QStringList buckets)
{
    if (albumId != m_albumId)
        return;
    if (buckets.isEmpty()) {
        m_albumLoading = false;
        qCInfo(lcImmich) << "album" << albumId << "has" << m_albumAssets.size() << "assets";
        emit albumAssetsChanged();
        return;
    }
    const QString bucket = buckets.takeFirst();
    get(QStringLiteral("/timeline/bucket"), {{QStringLiteral("albumId"), albumId}, {QStringLiteral("timeBucket"), bucket}},
        [this, albumId, buckets](const QByteArray &json) {
            if (albumId != m_albumId)
                return;
            // Columns: id[], ratio[], isImage[], ...
            const QJsonObject columns = QJsonDocument::fromJson(json).object();
            const QJsonArray ids = columns.value(QStringLiteral("id")).toArray();
            const QJsonArray ratios = columns.value(QStringLiteral("ratio")).toArray();
            const QJsonArray images = columns.value(QStringLiteral("isImage")).toArray();
            for (qsizetype i = 0; i < ids.size(); ++i)
                m_albumAssets.append(asset(ids.at(i).toString(), ratios.at(i).toDouble(1.5), images.at(i).toBool(true)));
            emit albumAssetsChanged();
            loadAlbumBuckets(albumId, buckets);
        });
}

QUrl ImmichClient::imageUrl(const QString &assetId, const QString &size) const
{
    if (!configured() || assetId.isEmpty())
        return {};
    QUrl url = m_config.url;
    url.setPath(url.path() + QStringLiteral("/api/assets/%1/thumbnail").arg(assetId));
    // edited=true: show rotations and crops made in Immich (the API defaults to the unedited image).
    url.setQuery(QStringLiteral("size=%1&edited=true").arg(size));
    return url;
}
