#pragma once

#include <QObject>
#include <QQuaternion>
#include <QTimer>
#include <QUdpSocket>
#include <QVector3D>
#include <QtQml/qqmlregistration.h>

// One pad of the motion server: its latest readings, and the orientation worked out from
// them. Axes as the protocol has them, which are also Qt Quick 3D's for a controller
// lying flat with its face up and its top away from the viewer: x to the right, y up out
// of the face, z towards the viewer.
class MotionPad : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("pads belong to a MotionClient")
    // Readings arrived within the last second.
    Q_PROPERTY(bool active READ active NOTIFY changed)
    // Readings in the last full second.
    Q_PROPERTY(int rate READ rate NOTIFY changed)
    // In g. At rest: 1 g pointing away from the ground.
    Q_PROPERTY(QVector3D acceleration READ acceleration NOTIFY changed)
    // In degrees a second, about x (pitch), y (yaw) and z (roll).
    Q_PROPERTY(QVector3D rotation READ rotation NOTIFY changed)
    // From the controller's own axes to the room's. Tilt is held true by gravity; the
    // turn about the vertical is only added up from the gyroscope and drifts.
    Q_PROPERTY(QQuaternion orientation READ orientation NOTIFY changed)
    // How hard the controller is being pushed, in g: the acceleration with gravity taken
    // out, so nothing at rest. Smoothed a little, for showing.
    Q_PROPERTY(float push READ push NOTIFY changed)
    // The strongest push lately; it falls back by 1 g a second.
    Q_PROPERTY(float pushPeak READ pushPeak NOTIFY changed)
    // The push as a displacement from the middle of a box, in the room's axes: so far
    // for each g, within `reach` each way. Not where the controller is.
    Q_PROPERTY(QVector3D position READ position NOTIFY changed)
    Q_PROPERTY(float reach READ reach CONSTANT)
    // How strong the accelerometer's reading is, in g as the controller's calibration has
    // it. Lying still it should be 1.
    Q_PROPERTY(float strength READ strength NOTIFY changed)
    // Degrees turned about the vertical since the last recenter, counted up: a full turn
    // flat on the table should read 360.
    Q_PROPERTY(float turned READ turned NOTIFY changed)

public:
    explicit MotionPad(QObject *parent = nullptr);

    bool active() const { return m_active; }
    int rate() const { return m_rate; }
    QVector3D acceleration() const { return m_acceleration; }
    QVector3D rotation() const { return m_rotation; }
    QQuaternion orientation() const { return m_orientation; }
    float push() const { return m_push.length(); }
    float pushPeak() const { return m_pushPeak; }
    QVector3D position() const { return m_position; }
    float reach() const;
    float strength() const { return m_acceleration.length(); }
    float turned() const { return m_turned; }

    // With the controller lying still, flat, its top away from the viewer: takes that as
    // level and as the direction it points in.
    Q_INVOKABLE void recenter();

    void add(const QVector3D &acceleration, const QVector3D &rotation, float seconds);
    // Once a second.
    void countSecond();
    // Tells QML about the readings since the last time, if any.
    void publish();

signals:
    void changed();

private:
    bool m_active = false;
    bool m_started = false;
    bool m_dirty = false;
    int m_rate = 0;
    int m_count = 0;
    QVector3D m_acceleration;
    QVector3D m_rotation;
    QQuaternion m_orientation;
    QVector3D m_push;
    float m_pushPeak = 0;
    QVector3D m_position;
    QVector3D m_averageSensor;
    float m_turned = 0;
};

// Reads the controllers' motion the way an emulator would: as a client of the controller
// service's cemuhook (DSU) server on localhost. Used by the motion test, which is thereby
// a test of that server as well.
class MotionClient : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)

public:
    explicit MotionClient(QObject *parent = nullptr);

    bool running() const { return m_running; }
    void setRunning(bool running);

    // 0 to 3; see MotionServer in ../controllers for which controller is which pad.
    Q_INVOKABLE MotionPad *pad(int index) const;

signals:
    void runningChanged();

private:
    void subscribe();
    void read();

    bool m_running = false;
    QUdpSocket m_socket;
    QTimer m_subscribeTimer;
    QTimer m_publishTimer;
    QTimer m_secondTimer;
    MotionPad *m_pads[4];
    quint64 m_lastTime[4] = {};
};
