#include "RemoteAssistant.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QLoggingCategory>

#include <cmath>

Q_LOGGING_CATEGORY(lcAssistant, "tvshell.assistant")

namespace {

using InterfaceMap = QMap<QString, QVariantMap>;
using ObjectMap = QMap<QDBusObjectPath, InterfaceMap>;

const QString Bluez = QStringLiteral("org.bluez");
const QString Characteristic = QStringLiteral("org.bluez.GattCharacteristic1");
const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");

// Android TV Voice service characteristics.
const QString VoiceCommandUuid = QStringLiteral("ab5e0002-5a21-4f05-bc7d-af01f617b664");
const QString VoiceAudioUuid = QStringLiteral("ab5e0003-5a21-4f05-bc7d-af01f617b664");
const QString VoiceControlUuid = QStringLiteral("ab5e0004-5a21-4f05-bc7d-af01f617b664");

constexpr quint8 ControlAudioStart = 0x04;
constexpr quint8 ControlAudioStop = 0x00;
constexpr quint8 ControlCapsResponse = 0x0B;

// GET_CAPS: protocol 1.0, codecs ADPCM 8 + 16 kHz, interaction model 3 = hold-to-talk
// (0 = on request, 1 = press-to-talk; other values make the remote ignore the button).
// Layout from Google's reference remote firmware (AOSP hardware/telink/atv/refDesignRcu,
// application/audio/gl_audio.c). The Chromecast remote answers `0b 01 00 02 03 00 a0 01 00 1a`,
// where 03 confirms the model.
const QByteArray GetCapsHoldToTalk = QByteArray::fromHex("0a01000003" "03");

constexpr int RetryMs = 5000;
constexpr int DecayMs = 50;
constexpr int SampleRate = 16000;
// Decoded RMS (of a 20 ms packet) that counts as "loud" for the indicator.
constexpr double LoudRms = 800;
constexpr double MaxPlaybackGain = 20;

} // namespace

RemoteAssistant::RemoteAssistant(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<InterfaceMap>();
    qDBusRegisterMetaType<ObjectMap>();

    // Remotes come and go (sleep, out of range): keep trying until subscribed.
    m_retry.setInterval(RetryMs);
    connect(&m_retry, &QTimer::timeout, this, &RemoteAssistant::subscribe);
    m_retry.start();

    // Let the level fall back when audio stops arriving.
    m_decay.setInterval(DecayMs);
    connect(&m_decay, &QTimer::timeout, this, &RemoteAssistant::decay);

    subscribe();
}

void RemoteAssistant::subscribe()
{
    if (connected())
        return;
    auto call = QDBusMessage::createMethodCall(Bluez, QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                               QStringLiteral("GetManagedObjects"));
    const QDBusReply<ObjectMap> reply = QDBusConnection::systemBus().call(call);
    if (!reply.isValid())
        return;
    const ObjectMap objects = reply.value();

    QString command;
    QString control;
    QString audio;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const QVariantMap characteristic = it.value().value(Characteristic);
        if (characteristic.isEmpty())
            continue;
        const QString uuid = characteristic.value(QStringLiteral("UUID")).toString();
        if (uuid == VoiceControlUuid)
            control = it.key().path();
        else if (uuid == VoiceAudioUuid)
            audio = it.key().path();
        else if (uuid == VoiceCommandUuid)
            command = it.key().path();
    }
    if (command.isEmpty() || control.isEmpty() || audio.isEmpty())
        return;
    // .../dev_XX_XX_XX_XX_XX_XX/serviceNNNN/charNNNN → the device.
    const QString device = control.section(u'/', 0, -3);
    const QVariantMap deviceProperties = objects.value(QDBusObjectPath(device)).value(QStringLiteral("org.bluez.Device1"));
    if (!deviceProperties.value(QStringLiteral("Connected")).toBool())
        return;

    // An empty path in connect() doesn't match all objects: watch each one explicitly.
    for (const QString &path : {device, control, audio}) {
        QDBusConnection::systemBus().connect(Bluez, path, Properties, QStringLiteral("PropertiesChanged"), this,
                                             SLOT(propertiesChanged(QDBusMessage)));
    }
    for (const QString &path : {control, audio}) {
        const QDBusMessage result = QDBusConnection::systemBus().call(
            QDBusMessage::createMethodCall(Bluez, path, Characteristic, QStringLiteral("StartNotify")));
        if (result.type() == QDBusMessage::ErrorMessage) {
            qCWarning(lcAssistant) << "StartNotify on" << path << "failed:" << result.errorMessage();
            return;
        }
    }
    m_devicePath = device;
    m_controlPath = control;
    m_audioPath = audio;
    qCInfo(lcAssistant) << "listening to the voice service of"
                        << deviceProperties.value(QStringLiteral("Name")).toString();

    // Ask for hold-to-talk: press → AUDIO_START + audio, release → AUDIO_STOP, no host replies.
    auto write = QDBusMessage::createMethodCall(Bluez, command, Characteristic, QStringLiteral("WriteValue"));
    write << GetCapsHoldToTalk << QVariantMap{{QStringLiteral("type"), QStringLiteral("command")}};
    QDBusConnection::systemBus().asyncCall(write);
    emit connectedChanged();
}

void RemoteAssistant::unsubscribe()
{
    if (!connected())
        return;
    qCInfo(lcAssistant) << "remote gone";
    for (const QString &path : {m_devicePath, m_controlPath, m_audioPath}) {
        QDBusConnection::systemBus().disconnect(Bluez, path, Properties, QStringLiteral("PropertiesChanged"), this,
                                                SLOT(propertiesChanged(QDBusMessage)));
    }
    m_devicePath.clear();
    m_controlPath.clear();
    m_audioPath.clear();
    setListening(false);
    emit connectedChanged();
}

void RemoteAssistant::propertiesChanged(const QDBusMessage &message)
{
    const QList<QVariant> args = message.arguments();
    if (args.size() < 2)
        return;
    const QString path = message.path();
    const QVariantMap changed = qdbus_cast<QVariantMap>(args.at(1));

    if (path == m_devicePath && changed.contains(QStringLiteral("Connected"))
        && !changed.value(QStringLiteral("Connected")).toBool()) {
        unsubscribe();
        return;
    }
    if (!changed.contains(QStringLiteral("Value")))
        return;
    const QByteArray value = changed.value(QStringLiteral("Value")).toByteArray();
    if (path == m_controlPath)
        handleControl(value);
    else if (path == m_audioPath)
        handleAudio(value);
}

void RemoteAssistant::handleControl(const QByteArray &value)
{
    if (value.isEmpty())
        return;
    const auto type = quint8(value.at(0));
    if (type == ControlAudioStart) {
        setListening(true);
    } else if (type == ControlAudioStop) {
        setListening(false);
    } else if (type == ControlCapsResponse) {
        const bool holdToTalk = value.size() > 4 && quint8(value.at(4)) == 0x03;
        qCInfo(lcAssistant) << "remote capabilities" << value.toHex(' ')
                            << (holdToTalk ? "(hold-to-talk)" : "(NOT hold-to-talk)");
    } else {
        qCInfo(lcAssistant) << "voice control message" << value.toHex(' ');
    }
}

void RemoteAssistant::handleAudio(const QByteArray &value)
{
    if (!m_listening || value.isEmpty())
        return;
    const qsizetype first = m_recording.size();
    m_decoder.decode(value, m_recording);
    // Loudness of this packet (20 ms).
    double sum = 0;
    for (qsizetype i = first; i < m_recording.size(); ++i)
        sum += double(m_recording[i]) * m_recording[i];
    const double rms = std::sqrt(sum / qMax<qsizetype>(1, m_recording.size() - first));
    const qreal target = qBound(0.0, rms / LoudRms, 1.0);
    // Rise fast, fall slowly.
    m_level = target > m_level ? target : m_level * 0.7 + target * 0.3;
    emit levelChanged();
}

void RemoteAssistant::setListening(bool listening)
{
    if (m_listening == listening)
        return;
    m_listening = listening;
    if (listening) {
        qCInfo(lcAssistant) << "Assistant pressed";
        m_decoder.reset();
        m_recording.clear();
        m_decay.start();
    } else {
        qCInfo(lcAssistant) << "Assistant released," << m_recording.size() / double(SampleRate) << "s recorded";
        if (m_playBack)
            play();
    }
    emit listeningChanged();
}

void RemoteAssistant::play()
{
    if (m_recording.isEmpty())
        return;
    // The remote's microphone is quiet (peaks ~7% of full scale): bring the peak to 70%.
    int peak = 1;
    for (qint16 sample : std::as_const(m_recording))
        peak = qMax(peak, qAbs(int(sample)));
    const double gain = qMin(MaxPlaybackGain, 0.7 * 32767 / peak);
    QList<qint16> clip(m_recording.size());
    for (qsizetype i = 0; i < m_recording.size(); ++i)
        clip[i] = qint16(qBound(-32768.0, m_recording[i] * gain, 32767.0));

    qCInfo(lcAssistant) << "playing back with gain" << gain;
    m_audio.play(clip, SampleRate);
    const int durationMs = int(clip.size() * 1000 / SampleRate);
    QTimer::singleShot(durationMs + 500, this, [this] { m_audio.stopIfDone(); });
}

void RemoteAssistant::decay()
{
    if (m_level < 0.01) {
        m_level = 0;
        if (!m_listening)
            m_decay.stop();
    } else {
        m_level *= 0.85;
    }
    emit levelChanged();
}
