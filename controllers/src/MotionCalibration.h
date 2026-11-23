#pragma once

#include "JoyCon.h"

#include <QHash>
#include <QObject>
#include <QSettings>
#include <QTimer>

class DeviceWatcher;

// The motion calibration done here, like a Switch's "calibrate motion controls": with the
// controller lying still on a table, what its sensors show is recorded. What the
// gyroscope shows then is its error, and what the accelerometer shows is 1 g, whatever the
// calibration stored in the controller says (which leaves the strength at rest up to 7 %
// off). Both are kept per controller, by its Bluetooth address, and applied to everything
// the service passes on, so games get the corrected motion.
//
// Kept in ~/.config/tvcontrollers/motion.ini.
class MotionCalibration : public QObject
{
    Q_OBJECT

public:
    explicit MotionCalibration(DeviceWatcher *watcher, QObject *parent = nullptr);

    bool isCalibrated(const QString &address) const;
    // Measures for two seconds and reports with finished(). The controller must lie still.
    void calibrate(const QString &address);

signals:
    // strength: what the accelerometer showed, in g by the controller's own calibration.
    // drift: what the gyroscope showed, in degrees a second.
    void finished(const QString &address, bool ok, double strength, double drift);

private:
    struct Run
    {
        JoyCon *joyCon = nullptr;
        QMetaObject::Connection connection;
        int count = 0;
        double accel[3] = {};
        double gyro[3] = {};
        float gyroLow[3] = {};
        float gyroHigh[3] = {};
        float accelLow[3] = {};
        float accelHigh[3] = {};
    };

    void apply(JoyCon *joyCon);
    void add(const QString &address, const JoyCon::MotionReport &samples);
    void end(const QString &address);

    DeviceWatcher *m_watcher;
    QSettings m_settings;
    QHash<QString, Run> m_runs;
};
