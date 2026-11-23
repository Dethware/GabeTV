#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <array>
#include <functional>

class QSocketNotifier;

// One physical Joy-Con, reached through its raw HID node. This is the only place that
// reads from or writes to a Joy-Con: it keeps the last known buttons, stick and battery,
// and sends subcommands (lights now; motion, rumble and the Ring-Con later) one at a time,
// each waiting for its reply.
class JoyCon : public QObject
{
    Q_OBJECT

public:
    enum class Side { Left, Right };

    struct State
    {
        quint32 buttons = 0; // JoyConProtocol::Button bits
        quint16 stickX = 0; // 12 bits, uncalibrated
        quint16 stickY = 0;
        int battery = 0; // 0 (empty) to 4 (full)
        bool charging = false;

        bool operator==(const State &) const = default;
    };

    // The stick's position: -1 to 1, right and up positive, calibrated, with a dead zone.
    struct Stick
    {
        double x = 0;
        double y = 0;
    };

    JoyCon(const QString &node, Side side, const QString &address, QObject *parent = nullptr);
    ~JoyCon() override;

    // Opens the node and starts talking. False, with errno set, when the node can't be
    // opened (yet): udev sets its group a moment after it appears.
    bool open();

    QString node() const { return m_node; }
    Side side() const { return m_side; }
    QString name() const { return m_side == Side::Left ? QStringLiteral("Joy-Con (L)") : QStringLiteral("Joy-Con (R)"); }
    // The Bluetooth address, the Joy-Con's identity across connections.
    QString address() const { return m_address; }
    QString firmware() const { return m_firmware; }
    bool isReady() const { return m_ready; }
    bool isQuiet() const { return m_quiet; }
    const State &state() const { return m_state; }
    Stick stick() const;

    // The four player lights: bit 0 is the first. A light in `flashing` flashes.
    void setPlayerLights(quint8 lit, quint8 flashing = 0);

signals:
    // The device answered: firmware known, full reports on.
    void ready();
    void stateChanged();
    void buttonsChanged(quint32 pressed, quint32 released);
    // Reading failed: the Joy-Con disconnected or went to sleep.
    void lost();
    // No report for a while although it sends 66 a second: it was switched off, or it is
    // out of reach and may come back. Which one can't be told, so the connection is left
    // alone; Bluetooth's own timeout ends it if the Joy-Con stays away.
    void quietChanged();
    // It went quiet from a perfectly regular stream of reports: that is what a Joy-Con
    // being switched off looks like (it says nothing when it does). One that drifts out
    // of reach has late and missing reports first.
    void switchedOff();

private:
    using Reply = std::function<void(const QByteArray &data)>;
    struct Pending
    {
        quint8 id;
        QByteArray args;
        Reply done;
        int tries = 0;
    };

    void readReports();
    void handleReport(const QByteArray &report);
    void send(quint8 subcommand, const QByteArray &args = {}, Reply done = {});
    void sendNext();
    void replyTimedOut();
    void applyLights();
    void checkLights();
    void readCalibration(quint32 address, bool user);

    // The stick's raw values at rest and how far it reaches from there in each direction.
    struct Calibration
    {
        int centerX = 2048;
        int centerY = 2048;
        int belowX = 1400;
        int belowY = 1400;
        int aboveX = 1400;
        int aboveY = 1400;
    };

    QString m_node;
    Side m_side;
    QString m_address;
    QString m_firmware;
    State m_state;
    int m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
    QList<Pending> m_queue;
    QTimer m_replyTimer;
    quint8 m_counter = 0;
    bool m_waiting = false;
    bool m_ready = false;
    // It has answered something: its node is alive.
    bool m_answered = false;
    Calibration m_calibration;
    // The lights as they should be, checked against the Joy-Con a moment after setting them.
    quint8 m_lights = 0;
    int m_lightChecks = 0;
    QTimer m_lightsTimer;
    QTimer m_quietTimer;
    QElapsedTimer m_sinceReport;
    bool m_quiet = false;
    // For telling a Joy-Con that was switched off (an abrupt stop) from one drifting out
    // of reach (growing gaps): the gaps between the last reports, about a second's worth.
    std::array<quint16, 64> m_gaps{};
    int m_gapIndex = 0;
};
