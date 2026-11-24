#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

struct pa_context;
struct pa_threaded_mainloop;
struct pa_server_info;
struct pa_sink_info;

// The volume of the default audio output, followed through PipeWire's PulseAudio interface.
// Steam handles the volume up/down keys, so the shell only shows the volume; nothing handles
// Mute, so the shell does (toggleMute()).
class SystemVolume : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // 1.0 = 100 %.
    Q_PROPERTY(qreal volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY volumeChanged)
    // The window whose volume up/down key presses are reported (keyPressed()).
    Q_PROPERTY(QWindow *window READ window WRITE setWindow NOTIFY windowChanged)

public:
    explicit SystemVolume(QObject *parent = nullptr);
    ~SystemVolume() override;

    qreal volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    QWindow *window() const { return m_window; }
    void setWindow(QWindow *window);

    Q_INVOKABLE void toggleMute();

signals:
    void volumeChanged();
    // The volume or mute of the current output changed (not: first reading, another output).
    void adjusted();
    // Volume up or down was pressed in the window (whether or not the volume could change).
    void keyPressed();
    void windowChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void start();
    void stop();
    // Called from the PulseAudio thread; hands over to the object's thread.
    void connectionLost();
    void sinkRead(const QString &sink, qreal volume, bool muted);

    static void contextStateChanged(pa_context *context, void *self);
    static void serverInfoRead(pa_context *context, const pa_server_info *info, void *self);
    static void sinkInfoRead(pa_context *context, const pa_sink_info *info, int eol, void *self);

    pa_threaded_mainloop *m_loop = nullptr;
    pa_context *m_context = nullptr;
    QTimer m_reconnect;
    QPointer<QWindow> m_window;
    QString m_sink;
    qreal m_volume = 0;
    bool m_muted = false;
};
