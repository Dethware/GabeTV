#include "MotionCalibration.h"

#include "DeviceWatcher.h"

#include <QDateTime>
#include <QLoggingCategory>

#include <cmath>

Q_LOGGING_CATEGORY(lcCalibration, "tvcontrollers.calibration")

namespace {

constexpr int MeasureMs = 2000;
// Lying still, the readings stay within this: more, and it was moved or touched.
constexpr float StillTurning = 4.0f; // degrees a second, lowest to highest
constexpr float StillAccel = 0.05f; // g, lowest to highest
// A sensible result. Beyond this the controller wasn't at rest or something is broken.
constexpr double LeastStrength = 0.85;
constexpr double MostStrength = 1.2;
constexpr double MostDrift = 6.0;

QString key(const QString &address)
{
    return QString(address).replace(QLatin1Char(':'), QLatin1Char('-'));
}

} // namespace

MotionCalibration::MotionCalibration(DeviceWatcher *watcher, QObject *parent)
    : QObject(parent)
    , m_watcher(watcher)
    , m_settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("tvcontrollers"), QStringLiteral("motion"))
{
    connect(watcher, &DeviceWatcher::joyConAdded, this, &MotionCalibration::apply);
    connect(watcher, &DeviceWatcher::joyConRemoved, this, [this](JoyCon *joyCon) {
        if (m_runs.contains(joyCon->address()))
            end(joyCon->address());
    });
}

bool MotionCalibration::isCalibrated(const QString &address) const
{
    return m_settings.contains(key(address) + QStringLiteral("/strength"));
}

// Gives a controller the correction recorded for it, if any.
void MotionCalibration::apply(JoyCon *joyCon)
{
    const QString group = key(joyCon->address());
    if (!m_settings.contains(group + QStringLiteral("/strength")))
        return;
    JoyCon::RestCorrection correction;
    correction.accelScale = float(1.0 / m_settings.value(group + QStringLiteral("/strength")).toDouble());
    correction.gyro[0] = m_settings.value(group + QStringLiteral("/gyroX")).toFloat();
    correction.gyro[1] = m_settings.value(group + QStringLiteral("/gyroY")).toFloat();
    correction.gyro[2] = m_settings.value(group + QStringLiteral("/gyroZ")).toFloat();
    joyCon->setRestCorrection(correction);
    qCInfo(lcCalibration).noquote() << joyCon->name() << joyCon->address() << "uses its motion calibration of"
                                    << m_settings.value(group + QStringLiteral("/when")).toString();
}

void MotionCalibration::calibrate(const QString &address)
{
    if (m_runs.contains(address))
        return;
    JoyCon *joyCon = nullptr;
    const QList<JoyCon *> joyCons = m_watcher->joyCons();
    for (JoyCon *candidate : joyCons) {
        if (candidate->address() == address && candidate->isReady())
            joyCon = candidate;
    }
    if (!joyCon) {
        emit finished(address, false, 0, 0);
        return;
    }
    qCInfo(lcCalibration).noquote() << "measuring" << joyCon->name() << address << "at rest";
    // Measured without the old correction, since the new one replaces it, and without
    // the correction following the error meanwhile.
    joyCon->setRestTracking(false);
    joyCon->setRestCorrection({});
    Run &run = m_runs[address];
    run.joyCon = joyCon;
    run.connection = connect(joyCon, &JoyCon::motion, this,
                             [this, address](const JoyCon::MotionReport &samples) { add(address, samples); });
    QTimer::singleShot(MeasureMs, this, [this, address] { end(address); });
}

void MotionCalibration::add(const QString &address, const JoyCon::MotionReport &samples)
{
    const auto it = m_runs.find(address);
    if (it == m_runs.end())
        return;
    Run &run = it.value();
    for (const JoyCon::Motion &sample : samples) {
        for (int axis = 0; axis < 3; ++axis) {
            run.accel[axis] += sample.accel[axis];
            run.gyro[axis] += sample.gyro[axis];
            if (run.count == 0) {
                run.accelLow[axis] = run.accelHigh[axis] = sample.accel[axis];
                run.gyroLow[axis] = run.gyroHigh[axis] = sample.gyro[axis];
            }
            run.accelLow[axis] = qMin(run.accelLow[axis], sample.accel[axis]);
            run.accelHigh[axis] = qMax(run.accelHigh[axis], sample.accel[axis]);
            run.gyroLow[axis] = qMin(run.gyroLow[axis], sample.gyro[axis]);
            run.gyroHigh[axis] = qMax(run.gyroHigh[axis], sample.gyro[axis]);
        }
        ++run.count;
    }
}

void MotionCalibration::end(const QString &address)
{
    const auto it = m_runs.find(address);
    if (it == m_runs.end())
        return;
    const Run run = it.value();
    m_runs.erase(it);
    disconnect(run.connection);

    bool still = run.count > 100;
    double accel[3];
    double gyro[3];
    for (int axis = 0; axis < 3 && run.count > 0; ++axis) {
        accel[axis] = run.accel[axis] / run.count;
        gyro[axis] = run.gyro[axis] / run.count;
        still = still && run.gyroHigh[axis] - run.gyroLow[axis] <= StillTurning
                && run.accelHigh[axis] - run.accelLow[axis] <= StillAccel;
    }
    const double strength = run.count > 0 ? std::hypot(accel[0], accel[1], accel[2]) : 0;
    const double drift = run.count > 0 ? std::hypot(gyro[0], gyro[1], gyro[2]) : 0;
    const bool ok = still && strength >= LeastStrength && strength <= MostStrength && drift <= MostDrift;

    if (ok) {
        const QString group = key(address);
        m_settings.setValue(group + QStringLiteral("/strength"), strength);
        m_settings.setValue(group + QStringLiteral("/gyroX"), gyro[0]);
        m_settings.setValue(group + QStringLiteral("/gyroY"), gyro[1]);
        m_settings.setValue(group + QStringLiteral("/gyroZ"), gyro[2]);
        m_settings.setValue(group + QStringLiteral("/name"), run.joyCon->name());
        m_settings.setValue(group + QStringLiteral("/when"), QDateTime::currentDateTime().toString(Qt::ISODate));
        m_settings.sync();
        qCInfo(lcCalibration).noquote() << "calibrated" << run.joyCon->name() << address << ": at rest it showed"
                                        << strength << "g and turned" << drift << "degrees a second";
    } else {
        qCInfo(lcCalibration).noquote() << "not calibrated" << address << ":" << (still ? "the readings make no sense:" : "it moved:")
                                        << run.count << "readings," << strength << "g," << drift << "degrees a second";
    }
    // The new correction, or the old one again.
    if (m_watcher->joyCons().contains(run.joyCon)) {
        apply(run.joyCon);
        run.joyCon->setRestTracking(true);
    }
    emit finished(address, ok, strength, drift);
}
