#include "SystemVolume.h"

#include <QLoggingCategory>

#include <pulse/pulseaudio.h>

#include <cmath>

Q_LOGGING_CATEGORY(lcVolume, "tvshell.volume")

namespace {

constexpr int ReconnectMs = 5000;

void unref(pa_operation *operation)
{
    if (operation)
        pa_operation_unref(operation);
}

} // namespace

SystemVolume::SystemVolume(QObject *parent)
    : QObject(parent)
{
    m_reconnect.setSingleShot(true);
    m_reconnect.setInterval(ReconnectMs);
    connect(&m_reconnect, &QTimer::timeout, this, &SystemVolume::start);
    start();
}

SystemVolume::~SystemVolume()
{
    stop();
}

void SystemVolume::start()
{
    m_loop = pa_threaded_mainloop_new();
    m_context = pa_context_new(pa_threaded_mainloop_get_api(m_loop), "tvshell");
    pa_context_set_state_callback(m_context, contextStateChanged, this);
    // NOFAIL: wait for the sound server if it isn't up yet.
    if (pa_context_connect(m_context, nullptr, PA_CONTEXT_NOFAIL, nullptr) < 0
        || pa_threaded_mainloop_start(m_loop) < 0) {
        qCWarning(lcVolume) << "can't connect to the sound server:" << pa_strerror(pa_context_errno(m_context));
        stop();
        m_reconnect.start();
    }
}

void SystemVolume::stop()
{
    if (!m_loop)
        return;
    pa_threaded_mainloop_stop(m_loop);
    pa_context_disconnect(m_context);
    pa_context_unref(m_context);
    pa_threaded_mainloop_free(m_loop);
    m_context = nullptr;
    m_loop = nullptr;
}

void SystemVolume::connectionLost()
{
    QMetaObject::invokeMethod(this, [this] {
        qCWarning(lcVolume) << "lost the sound server, trying again";
        stop();
        m_reconnect.start();
    }, Qt::QueuedConnection);
}

void SystemVolume::contextStateChanged(pa_context *context, void *self)
{
    auto *volume = static_cast<SystemVolume *>(self);
    switch (pa_context_get_state(context)) {
    case PA_CONTEXT_READY:
        // Any change of an output or of the default output: read the default output again.
        pa_context_set_subscribe_callback(
            context,
            [](pa_context *c, pa_subscription_event_type_t, uint32_t, void *s) {
                unref(pa_context_get_server_info(c, serverInfoRead, s));
            },
            self);
        unref(pa_context_subscribe(
            context, static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER),
            nullptr, nullptr));
        unref(pa_context_get_server_info(context, serverInfoRead, self));
        break;
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        volume->connectionLost();
        break;
    default:
        break;
    }
}

void SystemVolume::serverInfoRead(pa_context *context, const pa_server_info *info, void *self)
{
    if (info && info->default_sink_name)
        unref(pa_context_get_sink_info_by_name(context, info->default_sink_name, sinkInfoRead, self));
}

void SystemVolume::sinkInfoRead(pa_context *, const pa_sink_info *info, int eol, void *self)
{
    if (eol > 0 || !info)
        return;
    auto *volume = static_cast<SystemVolume *>(self);
    const QString sink = QString::fromUtf8(info->name);
    const qreal level = qreal(pa_cvolume_avg(&info->volume)) / PA_VOLUME_NORM;
    const bool muted = info->mute != 0;
    QMetaObject::invokeMethod(volume, [volume, sink, level, muted] { volume->sinkRead(sink, level, muted); },
                              Qt::QueuedConnection);
}

void SystemVolume::sinkRead(const QString &sink, qreal volume, bool muted)
{
    const bool sameSink = sink == m_sink;
    if (sameSink && std::abs(volume - m_volume) < 0.004 && muted == m_muted)
        return;
    if (!sameSink)
        qCInfo(lcVolume) << "output" << sink << "volume" << qRound(volume * 100) << "%" << (muted ? "muted" : "");
    m_sink = sink;
    m_volume = volume;
    m_muted = muted;
    emit volumeChanged();
    if (sameSink)
        emit adjusted();
}
