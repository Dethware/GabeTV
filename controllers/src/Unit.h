#pragma once

#include <QList>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>

class JoyCon;
class VirtualPad;

// One controller as a player holds it:
//   Pair      a left and a right Joy-Con as one full controller;
//   Sideways  a single Joy-Con held sideways, rail up, as a small controller;
//   Pro       a Pro Controller.
// (The Ring Fit set, the right Joy-Con in the Ring-Con and the left one in the leg strap,
// will be a third kind.)
// A unit turns its Joy-Cons' input into the state of one virtual controller, which is what
// Steam and the emulator see. Of the four buttons under the thumb, the right one is A in
// every grip, as on a Switch. Home is never passed on: it goes to the shell.
class Unit : public QObject
{
    Q_OBJECT

public:
    enum class Kind { Pair, Sideways, Pro };

    // For a pair: the left Joy-Con first.
    Unit(Kind kind, const QList<JoyCon *> &joyCons, int player, QObject *parent = nullptr);
    ~Unit() override;

    Kind kind() const { return m_kind; }
    QList<JoyCon *> joyCons() const { return m_joyCons; }
    // 1 to 4, shown on the player lights.
    int player() const { return m_player; }
    // The two units trade player numbers. Their virtual controllers go with the numbers:
    // programs see the controllers in the order they were made, so the first one stays
    // player 1's, whoever that now is.
    void tradePlayerWith(Unit *other);
    // Held, the virtual pad stays neutral whatever the controller does: an emulator has
    // the controllers to itself while a game is played. Home still gets through.
    void setHeld(bool held);

signals:
    void homePressed();

private:
    void update();
    void showPlayer();

    Kind m_kind;
    QList<JoyCon *> m_joyCons;
    int m_player;
    bool m_held = false;
    // A pair's right Joy-Con, for as long as it exists. While it sits in a Ring-Con and is
    // in use, the unit keeps the ring read, so that taking it out is noticed at once and
    // the pad goes back to the upright grip; nothing else says so. After a while without
    // any input the reading stops (the Joy-Con's battery), and the next input asks the
    // rail again.
    void followRing();
    QPointer<JoyCon> m_ringJoyCon;
    // Nothing says either when a Joy-Con is slid into a ring. What does change is how it
    // is held: in a ring it lies on its side, its length across. When a pair's right
    // Joy-Con has been like that for a second, the rail is asked (not more often than
    // every few seconds).
    int m_onItsSide = 0;
    QElapsedTimer m_sinceRingAsked;
    QTimer m_ringIdle;
    bool m_ringResting = false;
    // A press belongs to whoever had the controller when it went down: what was held
    // when the pad got the controller (it was formed, or a game let go of it) stays
    // released on the pad until it is let go. A stick: pushed then, not centred since.
    bool m_withholdNext = true;
    quint32 m_withheldButtons = 0;
    bool m_withheldSticks = false;
    VirtualPad *m_pad;
};
