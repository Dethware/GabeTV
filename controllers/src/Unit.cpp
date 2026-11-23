#include "Unit.h"

#include <cmath>

#include "JoyCon.h"
#include "JoyConProtocol.h"
#include "VirtualPad.h"

using namespace JoyConProtocol;

namespace {

struct Mapping
{
    quint32 joyCon; // JoyConProtocol::Button
    quint32 pad; // VirtualPad::Button
};

// The four buttons under the right thumb follow one rule in every grip, the Switch's own:
// the one on the right is A, the lower one B, the upper one X, the left one Y. Held
// upright that is what is printed on them; held sideways the print is turned and means
// nothing. A confirms and B goes back, here as everywhere else.

// A pair, held upright, or a Pro Controller: a full controller.
constexpr Mapping FullMap[] = {
    {A, VirtualPad::A},
    {B, VirtualPad::B},
    {X, VirtualPad::X},
    {Y, VirtualPad::Y},
    {Up, VirtualPad::DpadUp},
    {Down, VirtualPad::DpadDown},
    {Left, VirtualPad::DpadLeft},
    {Right, VirtualPad::DpadRight},
    {L, VirtualPad::LeftShoulder},
    {R, VirtualPad::RightShoulder},
    {ZL, VirtualPad::LeftTrigger},
    {ZR, VirtualPad::RightTrigger},
    {Minus, VirtualPad::Select},
    {Plus, VirtualPad::Start},
    {LeftStick, VirtualPad::LeftStick},
    {RightStick, VirtualPad::RightStick},
};

// The right Joy-Con sideways, rail up: the stick is on the left, and the four buttons have
// turned a quarter clockwise, so the one printed X is now on the right, A at the bottom,
// Y on top and B on the left.
constexpr Mapping RightSidewaysMap[] = {
    {X, VirtualPad::A},
    {A, VirtualPad::B},
    {Y, VirtualPad::X},
    {B, VirtualPad::Y},
    {RightSL, VirtualPad::LeftShoulder},
    {RightSR, VirtualPad::RightShoulder},
    {Plus, VirtualPad::Start},
    {R, VirtualPad::Select},
    {RightStick, VirtualPad::LeftStick},
};

// The right Joy-Con in the Ring-Con: it sits on its rail, turned the same quarter as one
// held sideways (its top to the player's right; the user tried the other way first and
// found it reversed). So the one printed X is on the right, A at the bottom, Y on top and
// B on the left; SL and SR are inside the ring.
constexpr Mapping RingMap[] = {
    {X, VirtualPad::A},
    {A, VirtualPad::B},
    {Y, VirtualPad::X},
    {B, VirtualPad::Y},
    {R, VirtualPad::RightShoulder},
    {ZR, VirtualPad::RightTrigger},
    {Plus, VirtualPad::Start},
    {RightStick, VirtualPad::LeftStick},
};

// The left Joy-Con sideways, rail up: turned the other way, so the arrow printed Down is
// now on the right, Left at the bottom, Right on top and Up on the left.
constexpr Mapping LeftSidewaysMap[] = {
    {Down, VirtualPad::A},
    {Left, VirtualPad::B},
    {Right, VirtualPad::X},
    {Up, VirtualPad::Y},
    {LeftSL, VirtualPad::LeftShoulder},
    {LeftSR, VirtualPad::RightShoulder},
    {Minus, VirtualPad::Start},
    {L, VirtualPad::Select},
    {LeftStick, VirtualPad::LeftStick},
};

// Without any input from the Joy-Con in the ring for this long, the ring is no longer read.
constexpr int RingIdleMs = 120000;
// A second of reports with the Joy-Con on its side, and how soon the rail is asked again.
constexpr int OnItsSideReports = 66;
constexpr int RingAskAgainMs = 6000;

template<std::size_t N>
quint32 mapped(const Mapping (&map)[N], quint32 buttons)
{
    quint32 pad = 0;
    for (const Mapping &entry : map) {
        if (buttons & entry.joyCon)
            pad |= entry.pad;
    }
    return pad;
}

} // namespace

Unit::Unit(Kind kind, const QList<JoyCon *> &joyCons, int player, QObject *parent)
    : QObject(parent)
    , m_kind(kind)
    , m_joyCons(joyCons)
    , m_player(player)
    , m_pad(new VirtualPad(QStringLiteral("TV Controller %1").arg(player), this))
{
    m_pad->create();
    showPlayer();
    if (m_kind == Kind::Pair) {
        // Whether the right one sits in a Ring-Con changes how the pair is held.
        m_ringJoyCon = m_joyCons.at(1);
        connect(m_ringJoyCon, &JoyCon::inRingChanged, this, [this] {
            followRing();
            update();
        });
        m_ringIdle.setSingleShot(true);
        m_ringIdle.setInterval(RingIdleMs);
        connect(&m_ringIdle, &QTimer::timeout, this, [this] {
            m_ringResting = true;
            followRing();
        });
        // Any input from it is use.
        connect(m_ringJoyCon, &JoyCon::stateChanged, this, [this] {
            m_ringIdle.start();
            if (m_ringResting) {
                m_ringResting = false;
                m_ringJoyCon->probeRing();
                followRing();
            }
        });
        m_ringIdle.start();
        m_ringJoyCon->probeRing();
        m_sinceRingAsked.start();
        connect(m_ringJoyCon, &JoyCon::motion, this, [this](const JoyCon::MotionReport &motion) {
            if (m_ringJoyCon->inRing() || m_ringResting)
                return;
            // Gravity along the Joy-Con's width (its y), little of it along its length.
            const JoyCon::Motion &now = motion.back();
            const bool onItsSide = std::abs(now.accel[1]) > 0.7f && std::abs(now.accel[0]) < 0.5f;
            m_onItsSide = onItsSide ? m_onItsSide + 1 : 0;
            if (m_onItsSide >= OnItsSideReports && m_sinceRingAsked.elapsed() > RingAskAgainMs) {
                m_onItsSide = 0;
                m_sinceRingAsked.start();
                m_ringJoyCon->probeRing();
            }
        });
    }
    for (JoyCon *joyCon : m_joyCons) {
        connect(joyCon, &JoyCon::buttonsChanged, this, [this](quint32 pressed) {
            if (pressed & Home)
                emit homePressed();
        });
        connect(joyCon, &JoyCon::stateChanged, this, &Unit::update);
        connect(joyCon, &JoyCon::quietChanged, this, &Unit::update);
    }
    update();
}

// The player's number on the lights, as a Switch shows it: 1 to 4 lights from the left.
void Unit::showPlayer()
{
    for (JoyCon *joyCon : std::as_const(m_joyCons))
        joyCon->setPlayerLights(quint8((1 << m_player) - 1));
}

void Unit::tradePlayerWith(Unit *other)
{
    std::swap(m_player, other->m_player);
    std::swap(m_pad, other->m_pad);
    m_pad->setParent(this);
    other->m_pad->setParent(other);
    for (Unit *unit : {this, other}) {
        unit->showPlayer();
        unit->update();
    }
}

Unit::~Unit()
{
    if (m_ringJoyCon)
        m_ringJoyCon->setRingWanted(JoyCon::RingUser::Grip, false);
}

void Unit::followRing()
{
    if (m_ringJoyCon)
        m_ringJoyCon->setRingWanted(JoyCon::RingUser::Grip, m_ringJoyCon->inRing() && !m_ringResting);
}

void Unit::setHeld(bool held)
{
    if (m_held == held)
        return;
    m_held = held;
    // Back from a game: what is held now was the game's.
    if (!held)
        m_withholdNext = true;
    update();
}

void Unit::update()
{
    VirtualPad::State state;
    if (m_held) {
        m_pad->update(state);
        return;
    }
    // A Joy-Con that has fallen silent may be gone; nothing stays held down meanwhile.
    for (const JoyCon *joyCon : std::as_const(m_joyCons)) {
        if (joyCon->isQuiet()) {
            m_pad->update(state);
            return;
        }
    }

    if (m_kind == Kind::Pair) {
        const JoyCon *left = m_joyCons.at(0);
        const JoyCon *right = m_joyCons.at(1);
        if (right->inRing()) {
            // The Joy-Con in the ring is the one in the hands, held a quarter turned like
            // a sideways one: it steers, with its stick as the player sees it (its up is
            // to the right, its right is down). The other one is strapped to a leg; its
            // buttons stay what they are.
            state.buttons = mapped(RingMap, right->state().buttons) | mapped(FullMap, left->state().buttons);
            state.leftX = right->stick().y;
            state.leftY = right->stick().x;
        } else {
            state.buttons = mapped(FullMap, left->state().buttons | right->state().buttons);
            state.leftX = left->stick().x;
            state.leftY = -left->stick().y;
            state.rightX = right->stick().x;
            state.rightY = -right->stick().y;
        }
    } else if (m_kind == Kind::Pro) {
        const JoyCon *pro = m_joyCons.first();
        state.buttons = mapped(FullMap, pro->state().buttons);
        state.leftX = pro->stick(JoyCon::Side::Left).x;
        state.leftY = -pro->stick(JoyCon::Side::Left).y;
        state.rightX = pro->stick(JoyCon::Side::Right).x;
        state.rightY = -pro->stick(JoyCon::Side::Right).y;
    } else {
        const JoyCon *joyCon = m_joyCons.first();
        const JoyCon::Stick stick = joyCon->stick();
        if (joyCon->side() == JoyCon::Side::Right) {
            // Turned a quarter clockwise: the stick's up is now to the right, its right is down.
            state.buttons = mapped(RightSidewaysMap, joyCon->state().buttons);
            state.leftX = stick.y;
            state.leftY = stick.x;
        } else {
            state.buttons = mapped(LeftSidewaysMap, joyCon->state().buttons);
            state.leftX = -stick.y;
            state.leftY = -stick.x;
        }
    }
    const bool pushed = state.leftX != 0 || state.leftY != 0 || state.rightX != 0 || state.rightY != 0;
    if (m_withholdNext) {
        m_withholdNext = false;
        m_withheldButtons = state.buttons;
        m_withheldSticks = pushed;
    }
    m_withheldButtons &= state.buttons;
    state.buttons &= ~m_withheldButtons;
    m_withheldSticks = m_withheldSticks && pushed;
    if (m_withheldSticks)
        state.leftX = state.leftY = state.rightX = state.rightY = 0;
    m_pad->update(state);
}
