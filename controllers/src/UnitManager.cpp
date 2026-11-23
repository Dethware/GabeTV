#include "UnitManager.h"

#include "DeviceWatcher.h"
#include "JoyCon.h"
#include "JoyConProtocol.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcUnits, "tvcontrollers.units")

using namespace JoyConProtocol;

namespace {

constexpr int MaxPlayers = 4;

} // namespace

UnitManager::UnitManager(DeviceWatcher *watcher, QObject *parent)
    : QObject(parent)
{
    connect(watcher, &DeviceWatcher::joyConAdded, this, &UnitManager::add);
    connect(watcher, &DeviceWatcher::joyConRemoved, this, &UnitManager::remove);
}

void UnitManager::add(JoyCon *joyCon)
{
    connect(joyCon, &JoyCon::buttonsChanged, this, [this, joyCon](quint32 pressed) {
        if (pressed)
            qCDebug(lcUnits).noquote() << joyCon->name() << "pressed" << Qt::hex << pressed;
        // In a unit, the unit handles its buttons.
        if (!m_unassigned.contains(joyCon))
            return;
        if (pressed & Home) {
            qCInfo(lcUnits).noquote() << "Home on" << joyCon->name();
            emit homePressed();
        }
        tryForm(joyCon);
    });
    wait(joyCon);
    emit changed();
}

void UnitManager::wait(JoyCon *joyCon)
{
    m_unassigned.append(joyCon);
    // All four lights flashing: connected, waiting to be told how it is held.
    joyCon->setPlayerLights(0, 0x0f);
}

void UnitManager::tryForm(JoyCon *joyCon)
{
    const bool left = joyCon->side() == JoyCon::Side::Left;
    const quint32 buttons = joyCon->state().buttons;

    const quint32 rail = left ? (LeftSL | LeftSR) : (RightSL | RightSR);
    if ((buttons & rail) == rail) {
        form(Unit::Kind::Sideways, {joyCon});
        return;
    }

    if (!(buttons & (left ? L : R)))
        return;
    for (JoyCon *other : std::as_const(m_unassigned)) {
        if (other->side() == joyCon->side() || !(other->state().buttons & (left ? R : L)))
            continue;
        form(Unit::Kind::Pair, left ? QList<JoyCon *>{joyCon, other} : QList<JoyCon *>{other, joyCon});
        return;
    }
}

void UnitManager::form(Unit::Kind kind, const QList<JoyCon *> &joyCons)
{
    int player = 1;
    const auto taken = [this](int number) {
        return std::any_of(m_units.cbegin(), m_units.cend(), [number](const Unit *unit) { return unit->player() == number; });
    };
    while (taken(player))
        ++player;
    if (player > MaxPlayers) {
        qCInfo(lcUnits) << "four controllers already; not forming another";
        return;
    }

    QStringList names;
    for (JoyCon *joyCon : joyCons) {
        m_unassigned.removeAll(joyCon);
        names.append(joyCon->name());
    }
    auto *unit = new Unit(kind, joyCons, player, this);
    connect(unit, &Unit::homePressed, this, [this, player] {
        qCInfo(lcUnits) << "Home on player" << player;
        emit homePressed();
    });
    m_units.append(unit);
    qCInfo(lcUnits).noquote() << "player" << player << "is" << names.join(QStringLiteral(" + "))
                              << (kind == Unit::Kind::Pair ? "as a pair" : "sideways");
    emit changed();
}

void UnitManager::remove(JoyCon *joyCon)
{
    m_unassigned.removeAll(joyCon);
    for (Unit *unit : std::as_const(m_units)) {
        if (!unit->joyCons().contains(joyCon))
            continue;
        qCInfo(lcUnits) << "player" << unit->player() << "ended:" << joyCon->name() << "left";
        m_units.removeAll(unit);
        const QList<JoyCon *> members = unit->joyCons();
        delete unit;
        for (JoyCon *other : members) {
            if (other != joyCon)
                wait(other);
        }
        break;
    }
    emit changed();
}
