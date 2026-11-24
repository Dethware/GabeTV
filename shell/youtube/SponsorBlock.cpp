#include "SponsorBlock.h"

#include "NetworkAccess.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(lcSponsorBlock, "tvshell.sponsorblock")

namespace {

const QString Api = QStringLiteral("https://sponsor.ajay.app/api/skipSegments/");

QString jsonList(const QStringList &values)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(values)).toJson(QJsonDocument::Compact));
}

} // namespace

SponsorBlock::SponsorBlock(QObject *parent)
    : QObject(parent)
    , m_network(new NetworkAccessManager(this))
{
}

void SponsorBlock::setVideoId(const QString &videoId)
{
    if (m_videoId == videoId)
        return;
    m_videoId = videoId;
    emit videoIdChanged();
    load();
}

void SponsorBlock::setCategories(const QStringList &categories)
{
    if (m_categories == categories)
        return;
    m_categories = categories;
    emit categoriesChanged();
    load();
}

void SponsorBlock::setSegments(const QVariantList &segments)
{
    if (m_segments.isEmpty() && segments.isEmpty())
        return;
    m_segments = segments;
    emit segmentsChanged();
}

void SponsorBlock::load()
{
    const int generation = ++m_generation;
    setSegments({});
    if (m_videoId.isEmpty() || m_categories.isEmpty())
        return;

    const QString videoId = m_videoId;
    const QByteArray hash = QCryptographicHash::hash(videoId.toUtf8(), QCryptographicHash::Sha256).toHex();
    QUrl url(Api + QString::fromLatin1(hash.left(4)));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("categories"), jsonList(m_categories));
    query.addQueryItem(QStringLiteral("actionTypes"), jsonList({QStringLiteral("skip")}));
    url.setQuery(query);

    QNetworkRequest request(url);
    // Segments get added and voted on: always ask, never keep.
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, videoId] {
        reply->deleteLater();
        if (generation != m_generation)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) { // nothing for any video with this hash prefix
            qCInfo(lcSponsorBlock) << "video" << videoId << "has no segments";
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(lcSponsorBlock) << "can't ask SponsorBlock:" << status << reply->errorString();
            return;
        }
        QVariantList segments;
        const QJsonArray videos = QJsonDocument::fromJson(reply->readAll()).array();
        for (const QJsonValue &value : videos) {
            const QJsonObject video = value.toObject();
            if (video.value(QStringLiteral("videoID")).toString() != videoId)
                continue;
            for (const QJsonValue &entry : video.value(QStringLiteral("segments")).toArray()) {
                const QJsonObject segment = entry.toObject();
                const QJsonArray span = segment.value(QStringLiteral("segment")).toArray();
                const double start = span.at(0).toDouble();
                const double end = span.at(1).toDouble();
                if (span.size() != 2 || end <= start)
                    continue;
                segments.append(QVariantMap{
                    {QStringLiteral("start"), start},
                    {QStringLiteral("end"), end},
                    {QStringLiteral("category"), segment.value(QStringLiteral("category")).toString()},
                });
            }
        }
        qCInfo(lcSponsorBlock) << "video" << videoId << "has" << segments.size() << "segments to skip";
        setSegments(segments);
    });
}
