#include "Unit.h"

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

// A pair, held upright: a full controller.
constexpr Mapping PairMap[] = {
    {B, VirtualPad::South},
    {A, VirtualPad::East},
    {Y, VirtualPad::West},
    {X, VirtualPad::North},
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
// turned a quarter clockwise, so A is at the bottom, X on the right, B on the left, Y on top.
constexpr Mapping RightSidewaysMap[] = {
    {A, VirtualPad::South},
    {X, VirtualPad::East},
    {B, VirtualPad::West},
    {Y, VirtualPad::North},
    {RightSL, VirtualPad::LeftShoulder},
    {RightSR, VirtualPad::RightShoulder},
    {Plus, VirtualPad::Start},
    {R, VirtualPad::Select},
    {RightStick, VirtualPad::LeftStick},
};

// The left Joy-Con sideways, rail up: turned the other way, so Left is at the bottom, Down
// on the right, Up on the left, Right on top.
constexpr Mapping LeftSidewaysMap[] = {
    {Left, VirtualPad::South},
    {Down, VirtualPad::East},
    {Up, VirtualPad::West},
    {Right, VirtualPad::North},
    {LeftSL, VirtualPad::LeftShoulder},
    {LeftSR, VirtualPad::RightShoulder},
    {Minus, VirtualPad::Start},
    {L, VirtualPad::Select},
    {LeftStick, VirtualPad::LeftStick},
};

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
    for (JoyCon *joyCon : m_joyCons) {
        // The player's number on the lights, as a Switch shows it: 1 to 4 lights from the left.
        joyCon->setPlayerLights(quint8((1 << player) - 1));
        connect(joyCon, &JoyCon::buttonsChanged, this, [this](quint32 pressed) {
            if (pressed & Home)
                emit homePressed();
        });
        connect(joyCon, &JoyCon::stateChanged, this, &Unit::update);
        connect(joyCon, &JoyCon::quietChanged, this, &Unit::update);
    }
    update();
}

void Unit::update()
{
    VirtualPad::State state;
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
        state.buttons = mapped(PairMap, left->state().buttons | right->state().buttons);
        state.leftX = left->stick().x;
        state.leftY = -left->stick().y;
        state.rightX = right->stick().x;
        state.rightY = -right->stick().y;
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
    m_pad->update(state);
}
