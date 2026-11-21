#include "ImmichClient.h"

#include "NetworkAccess.h"

#include <QCollator>
#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QDateTime>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QUrlQuery>

#include <algorithm>

Q_LOGGING_CATEGORY(lcImmich, "tvshell.immich")

namespace {

QVariantMap asset(const QString &id, double ratio, bool isImage)
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("ratio"), ratio}, {QStringLiteral("isImage"), isImage}};
}

// "City, Country", leaving out what's missing.
QString placeName(const QString &city, const QString &country)
{
    QStringList parts;
    for (const QString &part : {city, country}) {
        if (!part.isEmpty())
            parts << part;
    }
    return parts.join(QStringLiteral(", "));
}

// The assets of a timeline bucket (columns: id[], ratio[], isImage[], fileCreatedAt[],
// localOffsetHours[], city[], country[], ...) with their local date ("yyyy-MM-dd") and place.
QVariantList bucketAssets(const QJsonObject &columns)
{
    const QJsonArray ids = columns.value(QStringLiteral("id")).toArray();
    const QJsonArray ratios = columns.value(QStringLiteral("ratio")).toArray();
    const QJsonArray images = columns.value(QStringLiteral("isImage")).toArray();
    const QJsonArray created = columns.value(QStringLiteral("fileCreatedAt")).toArray();
    const QJsonArray offsets = columns.value(QStringLiteral("localOffsetHours")).toArray();
    const QJsonArray cities = columns.value(QStringLiteral("city")).toArray();
    const QJsonArray countries = columns.value(QStringLiteral("country")).toArray();
    QVariantList assets;
    for (qsizetype i = 0; i < ids.size(); ++i) {
        QVariantMap a = asset(ids.at(i).toString(), ratios.at(i).toDouble(1.5), images.at(i).toBool(true));
        const QDateTime when = QDateTime::fromString(created.at(i).toString(), Qt::ISODateWithMs);
        if (when.isValid())
            a.insert(QStringLiteral("date"),
                     when.toUTC().addSecs(qint64(offsets.at(i).toDouble() * 3600)).date().toString(Qt::ISODate));
        a.insert(QStringLiteral("place"), placeName(cities.at(i).toString(), countries.at(i).toString()));
        assets.append(a);
    }
    return assets;
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
            for (const QJsonValue &entry : memory.value(QStringLiteral("assets")).toArray()) {
                const QJsonObject o = entry.toObject();
                const double width = o.value(QStringLiteral("width")).toDouble();
                const double height = o.value(QStringLiteral("height")).toDouble();
                QVariantMap a = asset(o.value(QStringLiteral("id")).toString(), height > 0 ? width / height : 1.5,
                                      o.value(QStringLiteral("type")).toString() == QLatin1String("IMAGE"));
                // localDateTime is the local time written as if it were UTC.
                const QString local = o.value(QStringLiteral("localDateTime")).toString();
                if (local.size() >= 10)
                    a.insert(QStringLiteral("date"), local.left(10));
                const QJsonObject exif = o.value(QStringLiteral("exifInfo")).toObject();
                a.insert(QStringLiteral("place"), placeName(exif.value(QStringLiteral("city")).toString(),
                                                            exif.value(QStringLiteral("country")).toString()));
                assets.append(a);
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
            m_albumAssets.append(bucketAssets(QJsonDocument::fromJson(json).object()));
            emit albumAssetsChanged();
            loadAlbumBuckets(albumId, buckets);
        });
}

void ImmichClient::loadScreensaver(const QStringList &albumIds)
{
    const int generation = ++m_screensaverGeneration;
    m_screensaverCollected.clear();
    m_screensaverIds.clear();
    m_screensaverAssets.clear();
    m_screensaverPending = 0;
    if (!configured() || albumIds.isEmpty()) {
        emit screensaverChanged();
        return;
    }
    for (const QString &albumId : albumIds) {
        ++m_screensaverPending;
        get(QStringLiteral("/timeline/buckets"), {{QStringLiteral("albumId"), albumId}},
            [this, albumId, generation](const QByteArray &json) {
                if (generation != m_screensaverGeneration)
                    return;
                for (const QJsonValue &value : QJsonDocument::fromJson(json).array()) {
                    const QString bucket = value.toObject().value(QStringLiteral("timeBucket")).toString();
                    ++m_screensaverPending;
                    get(QStringLiteral("/timeline/bucket"),
                        {{QStringLiteral("albumId"), albumId}, {QStringLiteral("timeBucket"), bucket}},
                        [this, generation](const QByteArray &json) {
                            if (generation != m_screensaverGeneration)
                                return;
                            for (const QVariant &v : bucketAssets(QJsonDocument::fromJson(json).object())) {
                                const QVariantMap a = v.toMap();
                                const QString id = a.value(QStringLiteral("id")).toString();
                                if (!a.value(QStringLiteral("isImage")).toBool() || m_screensaverIds.contains(id))
                                    continue;
                                m_screensaverIds.insert(id);
                                m_screensaverCollected.append(a);
                            }
                            screensaverRequestDone(generation);
                        });
                }
                screensaverRequestDone(generation);
            });
    }
    emit screensaverChanged();
}

void ImmichClient::screensaverRequestDone(int generation)
{
    if (generation != m_screensaverGeneration || --m_screensaverPending > 0)
        return;
    // The order for this whole screensaver run, decided once.
    std::shuffle(m_screensaverCollected.begin(), m_screensaverCollected.end(), *QRandomGenerator::global());
    m_screensaverAssets = m_screensaverCollected;
    m_screensaverCollected.clear();
    qCInfo(lcImmich) << "screensaver:" << m_screensaverAssets.size() << "photos, shuffled";
    emit screensaverChanged();
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
