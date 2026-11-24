#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QNetworkAccessManager;

// The segments of a YouTube video to skip, from the SponsorBlock database (sponsor.ajay.app).
// Asks by the first four characters of the SHA-256 of the video ID, so the service doesn't
// learn which video it is, and picks the video's entry from the answer.
class SponsorBlock : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // The video playing ("" = none).
    Q_PROPERTY(QString videoId READ videoId WRITE setVideoId NOTIFY videoIdChanged)
    // SponsorBlock categories to ask for; empty = don't ask.
    Q_PROPERTY(QStringList categories READ categories WRITE setCategories NOTIFY categoriesChanged)
    // [{start, end, category}] in seconds, for videoId. Empty until the answer arrives.
    Q_PROPERTY(QVariantList segments READ segments NOTIFY segmentsChanged)

public:
    explicit SponsorBlock(QObject *parent = nullptr);

    QString videoId() const { return m_videoId; }
    void setVideoId(const QString &videoId);
    QStringList categories() const { return m_categories; }
    void setCategories(const QStringList &categories);
    QVariantList segments() const { return m_segments; }

signals:
    void videoIdChanged();
    void categoriesChanged();
    void segmentsChanged();

private:
    void load();
    void setSegments(const QVariantList &segments);

    QNetworkAccessManager *m_network;
    QString m_videoId;
    QStringList m_categories;
    QVariantList m_segments;
    int m_generation = 0;
};
