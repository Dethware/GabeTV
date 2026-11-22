#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

struct pa_context;
struct pa_threaded_mainloop;
struct pa_server_info;
struct pa_sink_info;

// The volume of the default audio output, followed through PipeWire's PulseAudio interface.
// The shell doesn't change the volume (Steam handles the volume keys); it only shows it.
class SystemVolume : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // 1.0 = 100 %.
    Q_PROPERTY(qreal volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY volumeChanged)

public:
    explicit SystemVolume(QObject *parent = nullptr);
    ~SystemVolume() override;

    qreal volume() const { return m_volume; }
    bool muted() const { return m_muted; }

signals:
    void volumeChanged();
    // The volume or mute of the current output changed (not: first reading, another output).
    void adjusted();

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
    QString m_sink;
    qreal m_volume = 0;
    bool m_muted = false;
};
