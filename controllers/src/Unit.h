#pragma once

#include <QList>
#include <QObject>

class JoyCon;
class VirtualPad;

// One controller as a player holds it, made of one or two Joy-Cons:
//   Pair      a left and a right Joy-Con as one full controller;
//   Sideways  a single Joy-Con held sideways, rail up, as a small controller.
// (The Ring Fit set, the right Joy-Con in the Ring-Con and the left one in the leg strap,
// will be a third kind.)
// A unit turns its Joy-Cons' input into the state of one virtual controller, which is what
// Steam and the emulator see. Buttons go by where they are under the thumb, not by their
// letter. Home is never passed on: it goes to the shell.
class Unit : public QObject
{
    Q_OBJECT

public:
    enum class Kind { Pair, Sideways };

    // For a pair: the left Joy-Con first.
    Unit(Kind kind, const QList<JoyCon *> &joyCons, int player, QObject *parent = nullptr);

    Kind kind() const { return m_kind; }
    QList<JoyCon *> joyCons() const { return m_joyCons; }
    // 1 to 4, shown on the player lights.
    int player() const { return m_player; }

signals:
    void homePressed();

private:
    void update();

    Kind m_kind;
    QList<JoyCon *> m_joyCons;
    int m_player;
    VirtualPad *m_pad;
};
