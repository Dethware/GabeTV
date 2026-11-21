#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

// How the photo viewer moves between photos and what it shows beside them, saved in
// ~/.config/tvshell/tvshell.conf, [viewer] group. Setting a property saves it at once.
class ViewerSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // "slide", "fade" or "none".
    Q_PROPERTY(QString transition READ transition WRITE setTransition NOTIFY changed)
    // "blur", "gradient" or "black" (as in ScreensaverSettings).
    Q_PROPERTY(QString background READ background WRITE setBackground NOTIFY changed)

public:
    explicit ViewerSettings(QObject *parent = nullptr);

    QString transition() const { return m_transition; }
    void setTransition(const QString &transition);
    QString background() const { return m_background; }
    void setBackground(const QString &background);

signals:
    void changed();

private:
    void save(const QString &key, const QVariant &value);

    QString m_transition = QStringLiteral("slide");
    QString m_background = QStringLiteral("blur");
};
