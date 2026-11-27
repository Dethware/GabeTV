#pragma once

#include <QtTypes>

#include <algorithm>
#include <cmath>

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
// Output report 0x10: a packet counter and the eight bytes of rumble data alone.
constexpr quint8 OutputRumble = 0x10;

// Rumble data: four bytes for the left Joy-Con's actuator, then four for the right's (a
// Pro Controller has one in each grip; a Joy-Con alone is given the same in both). Each
// actuator plays two sine waves at once, a high band (81.75 to 1252.57 Hz) and a low band
// (40.875 to 626.28 Hz), each with an amplitude from 0 to 1, as a Switch game sets them.
// Reference: dekuNukem's rumble_data_table.md and bluetooth_hid_notes.md.
constexpr int RumbleSize = 4;
// "No vibration": 320 Hz and 160 Hz, both silent.
constexpr quint8 RumbleOff[RumbleSize] = {0x00, 0x01, 0x40, 0x40};

inline void encodeRumble(float lowAmplitude, float lowFrequency, float highAmplitude, float highFrequency,
                         quint8 out[RumbleSize])
{
    // Both bands move the same actuator, so its swing is their sum: kept within 1. Above
    // an amplitude of 1 the reference warns of damaging the actuator.
    lowAmplitude = std::max(lowAmplitude, 0.0f);
    highAmplitude = std::max(highAmplitude, 0.0f);
    const float sum = lowAmplitude + highAmplitude;
    if (sum > 1.0f) {
        lowAmplitude /= sum;
        highAmplitude /= sum;
    }
    // Amplitude as a step from 0 to 100 (0.0 to 1.0). The reference gives two of the three
    // pieces; the lowest one (below 0.12) is fitted to its table, where each step is
    // 2^(1/4) more than the one before.
    const auto amplitude = [](float a) {
        float step;
        if (a > 0.23f)
            step = std::log2(a * 8.7f) * 32.0f;
        else if (a > 0.12f)
            step = std::log2(a * 17.0f) * 16.0f;
        else
            step = a > 0.0f ? std::log2(a * 120.0f) * 4.0f : 0.0f;
        return std::clamp(int(std::lround(step)), 0, 100);
    };
    // Frequency as 32 steps an octave above 10 Hz.
    const auto frequency = [](float hz, float lowest, float highest) {
        if (!(hz >= lowest)) // also a NaN
            hz = lowest;
        return int(std::lround(std::log2(std::min(hz, highest) / 10.0f) * 32.0f));
    };
    const int high = (frequency(highFrequency, 81.75f, 1252.57f) - 0x60) * 4; // 0x004 to 0x1fc
    const int low = frequency(lowFrequency, 40.875f, 626.28f) - 0x40; // 0x01 to 0x7f
    const int highStep = amplitude(highAmplitude);
    const int lowStep = amplitude(lowAmplitude);
    // The high band's frequency and the low band's amplitude are nine bits each: their
    // ninth bit is the lowest of the high band's amplitude byte and the top of the low
    // band's frequency byte.
    const int highAmp = highStep * 2; // 0x00 to 0xc8
    const int lowAmp = ((lowStep & 1) << 15) | (lowStep / 2 + 0x40); // 0x40 to 0x72
    out[0] = quint8(high & 0xff);
    out[1] = quint8(highAmp | ((high >> 8) & 0x01));
    out[2] = quint8(low | ((lowAmp >> 8) & 0x80));
    out[3] = quint8(lowAmp & 0xff);
}

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
    SetMcuConfig = 0x21, // a 38-byte block for the second processor, with a checksum
    SetMcuState = 0x22, // 1: the second processor on, 0: off
    GetExternalDevice = 0x59, // what is attached to the rail; answers with a 16-bit id
    EnableExternalPolling = 0x5a,
    DisableExternalPolling = 0x5b,
    SetExternalFormat = 0x5c,
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

// Motion: a full report carries three samples, 5 ms apart, each six 16-bit values (little
// endian): the accelerometer's x, y, z, then the gyroscope's. The calibration is twelve
// such values: the accelerometer's origin and sensitivity, then the gyroscope's.
constexpr int MotionOffset = 13;
constexpr int MotionSampleSize = 12;
constexpr int MotionSamples = 3;
constexpr int MotionIntervalUs = 5000;
constexpr quint32 FactoryMotion = 0x6020;
constexpr quint32 UserMotion = 0x8026;
constexpr int MotionCalibrationSize = 24;

// The Ring-Con hangs on a right Joy-Con's rail and is reached through the Joy-Con's second
// processor (the "MCU", which also drives the IR camera and NFC). Facts from Eden's
// joycon_protocol (ringcon.cpp, common_protocol.cpp, joycon_types.h):
//   1. the second processor on (SetMcuState 1), then into standby: SetMcuConfig with a
//      block of command 0x21, subcommand 1 (set device mode), mode 1 (standby), zeros,
//      and in the last byte a CRC-8 (polynomial 7, from 0) of bytes 1 to 36;
//   2. GetExternalDevice until it answers with the Ring-Con's id;
//   3. SetExternalFormat with the fixed block below, then EnableExternalPolling with
//      the four bytes below.
// From then on a full report carries, in place of its third motion reading's accelerometer
// (JoyCon.cpp), a zero, the ring's flex as a signed 16-bit value, and the ring's id.
constexpr int McuConfigSize = 38;
constexpr quint16 ExternalRingCon = 0x2000;
constexpr quint8 RingFormat[37] = {0x06, 0x03, 0x25, 0x06, 0x00, 0x00, 0x00, 0x00, 0x1c, 0x16, 0xed, 0x34, 0x36,
                                   0x00, 0x00, 0x00, 0x0a, 0x64, 0x0b, 0xe6, 0xa9, 0x22, 0x00, 0x00, 0x04, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0xa8, 0xe1, 0x34, 0x36};
constexpr quint8 RingPolling[4] = {0x04, 0x01, 0x01, 0x02};

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
