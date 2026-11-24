#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

// What the photo screensaver shows and how, saved in ~/.config/tvshell/tvshell.conf,
// [screensaver] group. Setting a property saves it at once.
class ScreensaverSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // Immich album IDs to take photos from; empty = the clock.
    Q_PROPERTY(QStringList albums READ albums WRITE setAlbums NOTIFY changed)
    // Seconds per photo.
    Q_PROPERTY(int interval READ interval WRITE setInterval NOTIFY changed)
    // "slide", "fade" or "none".
    Q_PROPERTY(QString transition READ transition WRITE setTransition NOTIFY changed)
    // Behind the photo (where it doesn't fill the screen): "blur" (the photo, blurred),
    // "gradient" (its colours, smoothed into a gradient) or "black".
    Q_PROPERTY(QString background READ background WRITE setBackground NOTIFY changed)
    // Show the photo's date and place.
    Q_PROPERTY(bool showInfo READ showInfo WRITE setShowInfo NOTIFY changed)

public:
    explicit ScreensaverSettings(QObject *parent = nullptr);

    QStringList albums() const { return m_albums; }
    void setAlbums(const QStringList &albums);
    int interval() const { return m_interval; }
    void setInterval(int seconds);
    QString transition() const { return m_transition; }
    void setTransition(const QString &transition);
    QString background() const { return m_background; }
    void setBackground(const QString &background);
    bool showInfo() const { return m_showInfo; }
    void setShowInfo(bool show);

    Q_INVOKABLE bool hasAlbum(const QString &albumId) const { return m_albums.contains(albumId); }
    Q_INVOKABLE void toggleAlbum(const QString &albumId);

signals:
    void changed();

private:
    void save(const QString &key, const QVariant &value);

    QStringList m_albums;
    int m_interval = 15;
    QString m_transition = QStringLiteral("slide");
    QString m_background = QStringLiteral("blur");
    bool m_showInfo = true;
};
