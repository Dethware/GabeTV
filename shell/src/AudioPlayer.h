#pragma once

#include <QByteArray>
#include <QList>
#include <QMutex>
#include <memory>

struct ma_device;

// Plays 16-bit mono PCM clips on the default output (miniaudio, in-process).
// The device is opened on first use and stops when the clip is done.
class AudioPlayer
{
public:
    AudioPlayer();
    ~AudioPlayer();

    // Replaces whatever is playing.
    void play(const QList<qint16> &samples, int sampleRate);
    // Stops the output stream once the clip has been played (call after its duration).
    void stopIfDone();

private:
    static void callback(ma_device *device, void *output, const void *input, unsigned int frames);
    void stop();

    std::unique_ptr<ma_device> m_device;
    int m_deviceRate = 0;

    QMutex m_mutex; // guards the clip, read on miniaudio's thread
    QList<qint16> m_clip;
    qsizetype m_position = 0;
};
