#pragma once

#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

// Follows Steam's webhelper log for UI events Steam doesn't expose any other way.
// Quick Access gives no X11 signal at all; its log line is the only trace
// (see docs/gabecube-findings.md). Depends on Steam's log wording: if Valve changes it,
// the signal just stops firing.
class SteamLog : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit SteamLog(QObject *parent = nullptr);

signals:
    void quickAccessOpened();
    void quickAccessClosed();

private:
    void poll();
    void handleLine(QByteArrayView line);

    QString m_path;
    qint64 m_offset = -1;
    QByteArray m_partial;
    QTimer m_timer;
};
