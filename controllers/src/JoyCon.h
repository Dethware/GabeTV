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

// One physical controller, reached through its raw HID node: a Joy-Con, or a Pro
// Controller, which speaks the same protocol and is both halves in one piece. This is the only place that
// reads from or writes to a Joy-Con: it keeps the last known buttons, stick and battery,
// and sends subcommands (lights now; motion, rumble and the Ring-Con later) one at a time,
// each waiting for its reply.
class JoyCon : public QObject
{
    Q_OBJECT

public:
    // Which half of a controller it is. Both: a Pro Controller, the two halves in one
    // piece, speaking the same protocol.
    enum class Side { Left, Right, Both };

    struct State
    {
        quint32 buttons = 0; // JoyConProtocol::Button bits
        // 12 bits, uncalibrated. A Joy-Con only has the stick of its side.
        quint16 leftX = 0;
        quint16 leftY = 0;
        quint16 rightX = 0;
        quint16 rightY = 0;
        int battery = 0; // 0 (empty) to 4 (full)
        bool charging = false;

        bool operator==(const State &) const = default;
    };

    // One reading of the motion sensor, calibrated: acceleration in g (at rest it shows
    // 1 g pointing away from the ground), rotation in degrees a second. The axes are the
    // same for every controller, as it is held upright: x towards its top (the shoulder
    // buttons), y to its left, z out of its face.
    struct Motion
    {
        float accel[3] = {};
        float gyro[3] = {};
        // When it was taken, in microseconds on a clock of this controller's own: exactly
        // 5 ms after the reading before, unless reports were lost in between. To be used
        // for the time between readings; when a reading arrives says little about that.
        qint64 time = 0;
    };
    // The three readings of one report, oldest first, 5 ms apart.
    using MotionReport = std::array<Motion, 3>;

    // What this controller's sensors showed lying still, as a correction on top of its
    // stored calibration (MotionCalibration): the acceleration is multiplied, so that rest
    // is 1 g, and the gyroscope's reading at rest is taken off.
    struct RestCorrection
    {
        float accelScale = 1.0f;
        float gyro[3] = {};
    };
    void setRestCorrection(const RestCorrection &correction) { m_restCorrection = correction; }
    // Whether the gyroscope's part of that correction follows its error while the
    // controller is at rest. Off while the error itself is being measured.
    void setRestTracking(bool tracking)
    {
        m_restTracking = tracking;
        m_restWindow = {};
    }
    // Reports lost on the radio since it connected; their readings were filled in.
    int lostReports() const { return m_lostReports; }

    // Whether to look for a Ring-Con on this (right) Joy-Con's rail and read it. It
    // keeps the Joy-Con's second processor running, which costs battery, so only while
    // something wants it.
    void setRingWanted(bool wanted);
    bool hasRing() const { return m_ring; }

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
    QString name() const;
    // The Bluetooth address, the Joy-Con's identity across connections.
    QString address() const { return m_address; }
    QString firmware() const { return m_firmware; }
    bool isReady() const { return m_ready; }
    bool isQuiet() const { return m_quiet; }
    const State &state() const { return m_state; }
    // A Joy-Con's own stick.
    Stick stick() const { return stick(m_side == Side::Right ? Side::Right : Side::Left); }
    // The left or the right stick.
    Stick stick(Side which) const;

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
    // Every 15 ms while it is connected.
    void motion(const JoyCon::MotionReport &samples);
    // A Ring-Con was found on the rail, or is gone.
    void ringChanged();
    // The ring's flex as the Joy-Con reports it, every 15 ms while a ring is attached.
    void ringFlex(int raw);
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
    void readCalibration(quint32 address, bool user, Side which);
    void readMotionCalibration(quint32 address, bool user);
    void trackRest(const MotionReport &samples, bool gap);
    void lookForRing();
    void setRing(bool ring);

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
    // The sensors' readings at rest and at a known acceleration or rotation; the defaults
    // are the nominal ones.
    struct StoredMotionCalibration
    {
        int accelOrigin[3] = {0, 0, 0};
        int accelSensitivity[3] = {16384, 16384, 16384};
        int gyroOrigin[3] = {0, 0, 0};
        int gyroSensitivity[3] = {13371, 13371, 13371};
    };
    StoredMotionCalibration m_motionCalibration;
    RestCorrection m_restCorrection;
    bool m_restTracking = true;
    // The stretch being watched for stillness: each gyroscope axis' lowest, highest and
    // sum, and the acceleration's strength, lowest and highest.
    struct RestWindow
    {
        int count = 0;
        float low[3] = {};
        float high[3] = {};
        double sum[3] = {};
        float strengthLow = 0;
        float strengthHigh = 0;
    };
    RestWindow m_restWindow;
    Motion m_lastMotion;
    bool m_ringWanted = false;
    bool m_ring = false;
    // Asks the rail what is attached, while a ring is wanted and none is there.
    QTimer m_ringTimer;
    // Reports in a row without the ring's id: the ring was taken off.
    int m_ringSilent = 0;
    // The time of the last motion reading (Motion::time), and the clock it is held to.
    qint64 m_motionTime = 0;
    int m_lastTimer = 0;
    qint64 m_lastArrived = 0;
    int m_lostReports = 0;
    QElapsedTimer m_motionClock;
    Calibration m_leftCalibration;
    Calibration m_rightCalibration;
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
