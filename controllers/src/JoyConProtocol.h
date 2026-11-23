#pragma once

#include <QtTypes>

// The Joy-Con's HID protocol over Bluetooth, as far as the service uses it. Reference:
// dekuNukem's Nintendo_Switch_Reverse_Engineering (bluetooth_hid_notes.md and
// bluetooth_hid_subcommands_notes.md).
namespace JoyConProtocol {

constexpr quint16 VendorNintendo = 0x057e;
constexpr quint16 ProductLeft = 0x2006;
constexpr quint16 ProductRight = 0x2007;
constexpr quint16 ProductPro = 0x2009;

// Output report 0x01: a packet counter, eight bytes of rumble data, then one subcommand
// and its arguments.
constexpr quint8 OutputSubcommand = 0x01;
constexpr int SubcommandOffset = 10;
// Rumble data for "no vibration" on both sides.
constexpr quint8 RumbleOff[8] = {0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};

// Input reports. 0x21 and 0x30 start the same way: timer, battery and connection, three
// bytes of buttons, three bytes per stick, the vibrator state.
constexpr quint8 InputSubcommandReply = 0x21;
constexpr quint8 InputFull = 0x30; // 66 a second; motion data from byte 13
constexpr quint8 InputSimple = 0x3f; // what a Joy-Con sends until it is told otherwise

constexpr int BatteryOffset = 2;
constexpr int ButtonsOffset = 3;
constexpr int LeftStickOffset = 6;
constexpr int RightStickOffset = 9;
// In a 0x21 report: the acknowledgement (top bit set), the subcommand answered, its data.
constexpr int ReplyAckOffset = 13;
constexpr int ReplyIdOffset = 14;
constexpr int ReplyDataOffset = 15;

enum Subcommand : quint8 {
    DeviceInfo = 0x02, // firmware, type, Bluetooth address
    SetInputMode = 0x03, // 0x30 for full reports
    ReadMemory = 0x10, // address (4 bytes, little endian) and length; answers with both, then the data
    SetPlayerLights = 0x30, // low nibble: lit, high nibble: flashing
    GetPlayerLights = 0x31, // answers with the same byte
    EnableMotion = 0x40,
    EnableVibration = 0x48,
};

// Where the stick calibration is in the Joy-Con's memory: nine bytes of packed 12-bit
// values each. The user's own calibration is valid if it starts with the two magic bytes.
constexpr quint32 FactoryLeftStick = 0x603d;
constexpr quint32 FactoryRightStick = 0x6046;
constexpr quint32 UserLeftStick = 0x8010;
constexpr quint32 UserRightStick = 0x801b;
constexpr quint8 UserCalibrationMagic[2] = {0xb2, 0xa1};
constexpr int StickCalibrationSize = 9;

// The buttons as one value: byte 3 of the report in bits 0-7 (the right Joy-Con's), byte 4
// in bits 8-15 (shared), byte 5 in bits 16-23 (the left Joy-Con's).
enum Button : quint32 {
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
    Home = 1u << 12,
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

} // namespace JoyConProtocol
