#include "ControllerService.h"

#include "Bluetooth.h"
#include "JoyCon.h"
#include "MotionCalibration.h"
#include "Unit.h"
#include "UnitManager.h"

#include <QDBusConnection>
#include <QDBusError>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcService, "tvcontrollers.service")

namespace {

const QString BusName = QStringLiteral("tvcontrollers");
constexpr int RetryIntervalMs = 3000;

QJsonObject describe(const JoyCon *joyCon, const MotionCalibration *calibration)
{
    return {
        {QStringLiteral("motionCalibrated"), calibration->isCalibrated(joyCon->address())},
        {QStringLiteral("lostReports"), joyCon->lostReports()},
        {QStringLiteral("address"), joyCon->address()},
        {QStringLiteral("name"), joyCon->name()},
        {QStringLiteral("side"), joyCon->side() == JoyCon::Side::Left    ? QStringLiteral("left")
                                 : joyCon->side() == JoyCon::Side::Right ? QStringLiteral("right")
                                                                         : QStringLiteral("both")},
        {QStringLiteral("battery"), joyCon->state().battery},
        {QStringLiteral("charging"), joyCon->state().charging},
    };
}

} // namespace

ControllerService::ControllerService(UnitManager *units, Bluetooth *bluetooth, MotionCalibration *calibration,
                                     QObject *parent)
    : QObject(parent)
    , m_units(units)
    , m_bluetooth(bluetooth)
    , m_calibration(calibration)
{
    connect(calibration, &MotionCalibration::finished, this, &ControllerService::MotionCalibrated);
    connect(calibration, &MotionCalibration::finished, this, &ControllerService::Changed);
    connect(units, &UnitManager::changed, this, &ControllerService::Changed);
    connect(bluetooth, &Bluetooth::searchingChanged, this, &ControllerService::Changed);
    connect(units, &UnitManager::homePressed, this, &ControllerService::HomePressed);
    m_retry.setInterval(RetryIntervalMs);
    connect(&m_retry, &QTimer::timeout, this, &ControllerService::connectToBus);
    connectToBus();
}

void ControllerService::connectToBus()
{
    QDBusConnection::disconnectFromBus(BusName);
    QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, BusName);
    const bool registered = bus.isConnected()
                            && bus.registerObject(QStringLiteral("/local/tvcontrollers"), this,
                                                  QDBusConnection::ExportScriptableContents)
                            && bus.registerService(QStringLiteral("local.tvcontrollers"));
    if (registered) {
        qCInfo(lcService) << "reachable on the session bus";
        m_retry.stop();
    } else if (!m_retry.isActive()) {
        qCInfo(lcService) << "no session bus yet, retrying:" << bus.lastError().message();
        m_retry.start();
    }
}

QString ControllerService::Describe() const
{
    QJsonArray units;
    QList<Unit *> formed = m_units->units();
    std::sort(formed.begin(), formed.end(), [](const Unit *a, const Unit *b) { return a->player() < b->player(); });
    for (const Unit *unit : std::as_const(formed)) {
        QJsonArray members;
        const QList<JoyCon *> joyCons = unit->joyCons();
        for (const JoyCon *joyCon : joyCons)
            members.append(describe(joyCon, m_calibration));
        units.append(QJsonObject{
            {QStringLiteral("player"), unit->player()},
            {QStringLiteral("kind"), unit->kind() == Unit::Kind::Pair       ? QStringLiteral("pair")
                                     : unit->kind() == Unit::Kind::Sideways ? QStringLiteral("sideways")
                                                                            : QStringLiteral("pro")},
            {QStringLiteral("joyCons"), members},
        });
    }
    QJsonArray unassigned;
    const QList<JoyCon *> waiting = m_units->unassigned();
    for (const JoyCon *joyCon : waiting)
        unassigned.append(describe(joyCon, m_calibration));
    const QJsonObject all{
        {QStringLiteral("units"), units},
        {QStringLiteral("unassigned"), unassigned},
        {QStringLiteral("searching"), m_bluetooth->searching()},
    };
    return QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact));
}

void ControllerService::StartSearch()
{
    m_bluetooth->startSearch();
}

void ControllerService::StopSearch()
{
    m_bluetooth->stopSearch();
}

void ControllerService::SwitchOff(const QString &address)
{
    m_bluetooth->switchOff(address);
}

void ControllerService::CalibrateMotion(const QString &address)
{
    m_calibration->calibrate(address);
}

void ControllerService::SwapPlayers(int player, int other)
{
    m_units->swap(player, other);
}

void ControllerService::EndUnit(int player)
{
    m_units->end(player);
}

void ControllerService::Forget(const QString &address)
{
    m_bluetooth->forget(address);
}
