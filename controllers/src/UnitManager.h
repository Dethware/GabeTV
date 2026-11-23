#pragma once

#include "Unit.h"

#include <QList>
#include <QObject>

class DeviceWatcher;
class JoyCon;

// Decides which Joy-Cons form which unit, the way a Switch does on its "change grip and
// order" screen. A Joy-Con that connects flashes its lights until its holder shows how it
// is held:
//   SL+SR on one Joy-Con          that Joy-Con alone, sideways;
//   L on a left and R on a right   those two as a pair.
// A unit gets the lowest free player number, 1 to 4, shown on its lights. When one of its
// Joy-Cons leaves, the unit ends and the other one waits again.
class UnitManager : public QObject
{
    Q_OBJECT

public:
    explicit UnitManager(DeviceWatcher *watcher, QObject *parent = nullptr);

    QList<Unit *> units() const { return m_units; }
    // Connected Joy-Cons that belong to no unit yet.
    QList<JoyCon *> unassigned() const { return m_unassigned; }

signals:
    void changed();
    // Home on any Joy-Con, in a unit or not.
    void homePressed();

private:
    void add(JoyCon *joyCon);
    void remove(JoyCon *joyCon);
    void wait(JoyCon *joyCon);
    void tryForm(JoyCon *joyCon);
    void form(Unit::Kind kind, const QList<JoyCon *> &joyCons);

    QList<Unit *> m_units;
    QList<JoyCon *> m_unassigned;
};
