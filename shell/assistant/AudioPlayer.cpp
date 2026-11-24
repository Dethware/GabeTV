#include "AudioPlayer.h"

#include <QLoggingCategory>
#include <QMutexLocker>

#include <cstring>

#include <miniaudio.h>

Q_LOGGING_CATEGORY(lcAudio, "tvshell.audio")

AudioPlayer::AudioPlayer() = default;

AudioPlayer::~AudioPlayer()
{
    stop();
}

void AudioPlayer::stop()
{
    if (!m_device)
        return;
    ma_device_uninit(m_device.get());
    m_device.reset();
}

void AudioPlayer::play(const QList<qint16> &samples, int sampleRate)
{
    {
        QMutexLocker lock(&m_mutex);
        m_clip = samples;
        m_position = 0;
    }
    if (m_device && m_deviceRate != sampleRate)
        stop();
    if (!m_device) {
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_s16;
        config.playback.channels = 1;
        config.sampleRate = ma_uint32(sampleRate);
        config.dataCallback = &AudioPlayer::callback;
        config.pUserData = this;
        m_device = std::make_unique<ma_device>();
        if (ma_device_init(nullptr, &config, m_device.get()) != MA_SUCCESS) {
            qCWarning(lcAudio) << "can't open the audio output";
            m_device.reset();
            return;
        }
        m_deviceRate = sampleRate;
        qCInfo(lcAudio) << "audio output:" << m_device->playback.name << "backend"
                        << ma_get_backend_name(m_device->pContext->backend);
    }
    if (ma_device_start(m_device.get()) != MA_SUCCESS)
        qCWarning(lcAudio) << "can't start the audio output";
}

void AudioPlayer::stopIfDone()
{
    if (!m_device)
        return;
    {
        QMutexLocker lock(&m_mutex);
        if (m_position < m_clip.size())
            return;
    }
    ma_device_stop(m_device.get());
}

void AudioPlayer::callback(ma_device *device, void *output, const void *, unsigned int frames)
{
    auto *self = static_cast<AudioPlayer *>(device->pUserData);
    QMutexLocker lock(&self->m_mutex);
    const qsizetype available = self->m_clip.size() - self->m_position;
    const qsizetype count = qMin<qsizetype>(frames, available);
    if (count > 0) {
        std::memcpy(output, self->m_clip.constData() + self->m_position, size_t(count) * sizeof(qint16));
        self->m_position += count;
    }
    // miniaudio pre-silences the output buffer; nothing to do for the rest.
}
