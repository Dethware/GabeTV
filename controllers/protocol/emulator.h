#pragma once

// The controller service's connection for an emulator: what the service (controllers/) and
// our input driver inside the emulator (apps/eden) say to each other. This file is the
// whole contract; both sides include it, and it depends on nothing but the C++ standard
// library.
//
// The emulator gets the physical controllers, not the virtual pads games get: each
// Joy-Con and Pro Controller by itself, with which player it belongs to and how it is
// held. A game may take a pair apart (Ring Fit keeps the right Joy-Con as player 1 and
// makes the left one player 2), so the emulator has to be able to hand each one to
// whatever player the game says.
//
// Transport: a Unix socket of type SOCK_SEQPACKET at `<runtime dir>/tvcontrollers/emulator`
// (the runtime dir is $XDG_RUNTIME_DIR, /run/user/<uid>). One packet is one message, so
// there are no lengths; every message starts with a Header. Numbers are little endian,
// floats IEEE 754. The emulator connects and says Hello; the service answers with
// Controllers, and again whenever anything in it changes, and sends an Input for every
// report of every controller (66 a second each). What the emulator asked for (the ring,
// rumble, lights) ends when its connection does.

#include <cstdint>

namespace TvControllers::Emulator {

constexpr char SocketDirectory[] = "tvcontrollers";
constexpr char SocketName[] = "emulator";

// Raised when a message changes shape. The service refuses a Hello with another one.
constexpr std::uint16_t Version = 2;

enum class Type : std::uint16_t {
    // Emulator to service.
    Hello = 1,
    SetRing = 2,
    SetLights = 3,
    Rumble = 4,
    Playing = 5,
    // Service to emulator.
    Controllers = 16,
    Input = 17,
};

#pragma pack(push, 1)

struct Header
{
    Type type;
    std::uint16_t version = Version;
};

// --- Service to emulator ---------------------------------------------------------------

enum class Side : std::uint8_t {
    Left = 0,
    Right = 1,
    Pro = 2,
};

// How its player holds it: what the gestures on the controllers said (L+R, SL+SR).
enum class Grip : std::uint8_t {
    Waiting = 0, // connected, in no controller yet (its lights flash)
    Pair = 1, // one of two Joy-Cons held as one controller
    Sideways = 2, // one Joy-Con held sideways, rail up
    Pro = 3,
};

constexpr std::uint8_t NoController = 0xff;

struct Controller
{
    // Stands for this controller in every other message. Stays the same for as long as it
    // is connected; a controller that reconnects may get another.
    std::uint8_t id;
    Side side;
    Grip grip;
    // 1 to 4, or 0 while it waits. The player number the service gave its controller; the
    // emulator starts from it and the game may reassign.
    std::uint8_t player;
    // The other Joy-Con of a pair, or NoController.
    std::uint8_t partner;
    // 0 (empty) to 4 (full).
    std::uint8_t battery;
    std::uint8_t charging; // 0 or 1
    // A Ring-Con is attached and being read (only ever while SetRing asked for it).
    std::uint8_t ring; // 0 or 1
    // Its Bluetooth address, which is also what the service's other interfaces call it.
    std::uint8_t address[6];
};

constexpr int MaxControllers = 8;

struct Controllers
{
    Header header{Type::Controllers};
    std::uint8_t count;
    Controller controllers[MaxControllers];
};

// The buttons as one value, laid out as the Joy-Con's own report has them. A left Joy-Con
// only ever has the left ones, a right one the right ones. Home is not passed on: it
// belongs to the TV.
enum Button : std::uint32_t {
    Y = 1u << 0,
    X = 1u << 1,
    B = 1u << 2,
    A = 1u << 3,
    RightSR = 1u << 4,
    RightSL = 1u << 5,
    R = 1u << 6,
    ZR = 1u << 7,
    Minus = 1u << 8,
    Plus = 1u << 9,
    RightStick = 1u << 10,
    LeftStick = 1u << 11,
    Capture = 1u << 13,
    Down = 1u << 16,
    Up = 1u << 17,
    Right = 1u << 18,
    Left = 1u << 19,
    LeftSR = 1u << 20,
    LeftSL = 1u << 21,
    L = 1u << 22,
    ZL = 1u << 23,
};

// One reading of the motion sensor, calibrated and corrected by the service. Axes are the
// same for every controller held upright: x to its top, y to its left, z out of its face.
struct MotionReading
{
    // Microseconds on the service's clock; readings are 5000 apart unless reports were
    // lost for longer than the service fills in.
    std::uint64_t time;
    float acceleration[3]; // g; lying face up it is (0, 0, 1)
    float rotation[3]; // degrees a second
};

constexpr int MotionReadings = 3;

// The ring's value when there is none.
constexpr std::int16_t NoRing = INT16_MIN;

// One report of one controller, as it is upright in the hand: the sticks and buttons are
// not turned for a sideways grip (the emulator knows the grip and the game does that).
struct Input
{
    Header header{Type::Input};
    std::uint8_t id;
    // No report for a second (out of reach, or switched off and not yet gone): the rest
    // of this message is then the neutral state.
    std::uint8_t quiet; // 0 or 1
    // The Ring-Con's flex as the Joy-Con reports it, raw: about 3700 at rest, higher
    // squeezed, lower pulled, roughly 1500 to 6200. NoRing without a ring.
    std::int16_t ring;
    std::uint32_t buttons;
    // -1 to 1, x to the right and y up, calibrated, with the service's dead zone taken
    // out. A Joy-Con has one stick: the left one's is `left`, the right one's `right`.
    float left[2];
    float right[2];
    MotionReading motion[MotionReadings];
};

// --- Emulator to service ---------------------------------------------------------------

struct Hello
{
    Header header{Type::Hello};
    // For the service's log.
    char name[32];
};

// Read the Ring-Con on this right Joy-Con, or stop. Reading keeps the Joy-Con's second
// processor running, which costs battery: ask when the game switches the ring on.
struct SetRing
{
    Header header{Type::SetRing};
    std::uint8_t id;
    std::uint8_t on; // 0 or 1
};

// The four player lights, one bit each from the one nearest the stick's side (bit 0), as
// the game sets them. Until the emulator says, a controller shows the service's player.
struct SetLights
{
    Header header{Type::SetLights};
    std::uint8_t id;
    std::uint8_t lights;
};

// Rumble for one controller: a low and a high band, each an amplitude from 0 to 1 and a
// frequency in Hz, as a Switch game gives them. It lasts until the next one; all zero
// amplitudes stop it. Only while a game is played (Playing): the controllers' connections
// are kept ready for it then. The service also stops it when the emulator says it is not
// playing, when the controller leaves its controller and when the connection ends.
//
// Send every value the game sets, as it sets it (repeats may be left out): the service
// writes to the controller at its own pace (every 30 ms at most, a Joy-Con's limit) and
// keeps the strongest value since the last write, so a short pulse isn't lost.
enum class Motor : std::uint8_t {
    Left = 0,
    Right = 1,
};

struct Rumble
{
    Header header{Type::Rumble};
    std::uint8_t id;
    // A Pro Controller has one in each grip. A Joy-Con has one; this is ignored for it.
    Motor motor;
    float lowAmplitude;
    float lowFrequency;
    float highAmplitude;
    float highFrequency;
};

// A game is being played, or not (none running, or paused and parked). While one is, the
// controllers are the game's alone: the service holds its virtual pads still, so that
// nothing else on the machine sees the same presses. Home still reaches the TV.
struct Playing
{
    Header header{Type::Playing};
    std::uint8_t on; // 0 or 1
};

#pragma pack(pop)

static_assert(sizeof(Header) == 4);
static_assert(sizeof(Controller) == 14);
static_assert(sizeof(MotionReading) == 32);
static_assert(sizeof(Input) == 4 + 8 + 16 + MotionReadings * 32);
static_assert(sizeof(Rumble) == 4 + 2 + 16);

} // namespace TvControllers::Emulator
