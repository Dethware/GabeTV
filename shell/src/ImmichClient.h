#pragma once

#include "ImmichConfig.h"

#include <functional>

#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Read-only access to the Immich server (API v3.0.3): albums, today's memories, an album's
// photos, and image URLs. Requests go through NetworkAccessManager, which adds the API key.
//
// Lists are plain QVariantLists of maps for now:
//   albums:      { id, name, count, cover (asset id) }
//   memories:    { id, title, assets: [asset] }
//   albumAssets: [asset]
//   asset:       { id, ratio (width / height), isImage }
class ImmichClient : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool configured READ configured CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QVariantList albums READ albums NOTIFY albumsChanged)
    Q_PROPERTY(QVariantList memories READ memories NOTIFY memoriesChanged)
    Q_PROPERTY(QVariantList albumAssets READ albumAssets NOTIFY albumAssetsChanged)
    Q_PROPERTY(bool albumLoading READ albumLoading NOTIFY albumAssetsChanged)
    // Photos (no videos) of the screensaver's albums, shuffled once per loadScreensaver().
    // Assets here also have { date: "yyyy-MM-dd" (local), place: "City, Country" }.
    Q_PROPERTY(QVariantList screensaverAssets READ screensaverAssets NOTIFY screensaverChanged)
    Q_PROPERTY(bool screensaverLoading READ screensaverLoading NOTIFY screensaverChanged)

public:
    explicit ImmichClient(QObject *parent = nullptr);

    bool configured() const { return m_config.isValid(); }
    QString error() const { return m_error; }
    QVariantList albums() const { return m_albums; }
    QVariantList memories() const { return m_memories; }
    QVariantList albumAssets() const { return m_albumAssets; }
    bool albumLoading() const { return m_albumLoading; }
    QVariantList screensaverAssets() const { return m_screensaverAssets; }
    bool screensaverLoading() const { return m_screensaverPending > 0; }

    // Collects all photos of these albums (all month buckets, in parallel), then shuffles them.
    Q_INVOKABLE void loadScreensaver(const QStringList &albumIds);

    // Albums and today's memories.
    Q_INVOKABLE void refresh();
    // Fills albumAssets with the album's photos, newest first.
    Q_INVOKABLE void loadAlbum(const QString &albumId);
    // size: "thumbnail" (grids), "preview" (~1440 px), "fullsize" (original resolution).
    Q_INVOKABLE QUrl imageUrl(const QString &assetId, const QString &size) const;

signals:
    void errorChanged();
    void albumsChanged();
    void memoriesChanged();
    void albumAssetsChanged();
    void screensaverChanged();

private:
    void screensaverRequestDone(int generation);
    using JsonHandler = std::function<void(const QByteArray &)>;
    void get(const QString &path, const QList<std::pair<QString, QString>> &query, JsonHandler handler);
    void setError(const QString &error);
    void loadAlbumBuckets(const QString &albumId, QStringList buckets);

    ImmichConfig m_config;
    QNetworkAccessManager *m_network;
    QString m_error;
    QVariantList m_albums;
    QVariantList m_memories;
    QVariantList m_albumAssets;
    QString m_albumId;
    bool m_albumLoading = false;

    QVariantList m_screensaverAssets;
    QVariantList m_screensaverCollected;
    QSet<QString> m_screensaverIds;
    int m_screensaverPending = 0;
    int m_screensaverGeneration = 0;
};
