#pragma once

#include "AudioPlayer.h"
#include "ImaAdpcm.h"

#include <QDBusMessage>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

// The remote's Assistant button and microphone, through its Android TV Voice service
// (GATT service ab5e0001-…), read over bluez D-Bus without root.
//
// On subscribing, the shell sends GET_CAPS asking for the hold-to-talk model. Then holding
// Assistant gives: control `04 03 02 <stream>` (audio start), audio packets (160 bytes, ~40/s),
// and on release control `00 02` (audio stop). Without a valid GET_CAPS the remote's behaviour
// depends on what the last host asked for (an invalid model makes it ignore the button).
//
// Audio: IMA ADPCM, 16 kHz mono, 160-byte packets without headers, high nibble first (see
// ImaAdpcm.h). Decoded while held; `level` is the loudness of the latest packet. On release
// the recording is played back (AudioPlayer, miniaudio), for now, until something like speech
// recognition uses it.
class RemoteAssistant : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)
    Q_PROPERTY(qreal level READ level NOTIFY levelChanged)
    // Play the recording back on release.
    Q_PROPERTY(bool playBack MEMBER m_playBack NOTIFY playBackChanged)

public:
    explicit RemoteAssistant(QObject *parent = nullptr);

    // Subscribed to a remote's voice service.
    bool connected() const { return !m_controlPath.isEmpty(); }
    bool listening() const { return m_listening; }
    // 0..1, smoothed.
    qreal level() const { return m_level; }

signals:
    void connectedChanged();
    void listeningChanged();
    void levelChanged();
    void playBackChanged();

private slots:
    void propertiesChanged(const QDBusMessage &message);

private:
    void subscribe();
    void unsubscribe();
    void handleControl(const QByteArray &value);
    void handleAudio(const QByteArray &value);
    void setListening(bool listening);
    void decay();
    void play();

    QString m_devicePath;
    QString m_controlPath;
    QString m_audioPath;
    bool m_listening = false;
    qreal m_level = 0;
    QTimer m_retry;
    QTimer m_decay;

    ImaAdpcmDecoder m_decoder;
    QList<qint16> m_recording;
    bool m_playBack = true;
    AudioPlayer m_audio;
};
