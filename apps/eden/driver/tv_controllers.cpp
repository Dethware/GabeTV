// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "common/logging.h"
#include "common/thread.h"
#include "hid_core/frontend/tv_controllers.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_util.h"

namespace Core::HID {

namespace {

constexpr s32 StickMax = 0x7fff;
// How far a stick is pushed before it also counts as a direction pressed.
constexpr f32 StickDirection = 0.5f;
// Between two motion readings: 5 ms. After a silence, no more than this is taken as the
// time that passed, so that the orientation doesn't jump.
constexpr u64 MotionStepUs = 5000;
constexpr u64 LongestStepUs = 100000;
constexpr int RetryMs = 1000;

NpadPowerInfo PowerInfo(const TvProtocol::Controller& described) {
    static constexpr std::array levels{NpadBatteryLevel::Empty, NpadBatteryLevel::Critical,
                                       NpadBatteryLevel::Low, NpadBatteryLevel::High,
                                       NpadBatteryLevel::Full};
    return {
        .is_powered = described.charging != 0,
        .is_charging = described.charging != 0,
        .battery_level = levels[(std::min<std::size_t>)(described.battery, levels.size() - 1)],
    };
}

} // namespace

TvControllers::TvControllers(HIDCore& hid_core_) : hid_core{hid_core_} {
    for (std::size_t player = 0; player < Players; ++player) {
        hid_core.GetEmulatedControllerByIndex(player)->tv_controllers = this;
    }
    thread = std::jthread([this](std::stop_token stop_token) { Run(stop_token); });
}

TvControllers::~TvControllers() {
    thread.request_stop();
    if (thread.joinable()) {
        thread.join();
    }
    for (std::size_t player = 0; player < Players; ++player) {
        hid_core.GetEmulatedControllerByIndex(player)->tv_controllers = nullptr;
    }
}

bool TvControllers::Connect() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    const std::string path = (runtime ? std::string{runtime}
                                      : "/run/user/" + std::to_string(::getuid())) +
                             "/" + TvProtocol::SocketDirectory + "/" + TvProtocol::SocketName;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path)) {
        return false;
    }
    std::memcpy(address.sun_path, path.c_str(), path.size());

    const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return false;
    }
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(fd);
        return false;
    }
    {
        std::scoped_lock lock{send_mutex};
        socket_fd = fd;
    }
    TvProtocol::Hello hello{};
    std::strncpy(hello.name, "Eden", sizeof(hello.name) - 1);
    Send(&hello, sizeof(hello));
    // A service that was away doesn't know.
    SetPlaying(playing);
    LOG_INFO(Input, "Connected to the TV's controller service at {}", path);
    return true;
}

void TvControllers::Close() {
    {
        std::scoped_lock lock{send_mutex};
        if (socket_fd >= 0) {
            ::close(socket_fd);
        }
        socket_fd = -1;
    }
    // Without the service there are no controllers.
    TvProtocol::Controllers nothing{};
    OnControllers(nothing);
}

void TvControllers::Send(const void* message, std::size_t size) {
    std::scoped_lock lock{send_mutex};
    if (socket_fd >= 0) {
        ::send(socket_fd, message, size, MSG_NOSIGNAL);
    }
}

void TvControllers::Run(std::stop_token stop_token) {
    Common::SetCurrentThreadName("TvControllers");
    bool told = false;
    while (!stop_token.stop_requested()) {
        if (!Connect()) {
            if (!told) {
                LOG_WARNING(Input, "The TV's controller service doesn't answer; trying again");
                told = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(RetryMs));
            continue;
        }
        told = false;
        while (!stop_token.stop_requested()) {
            pollfd waiting{.fd = socket_fd, .events = POLLIN, .revents = 0};
            if (::poll(&waiting, 1, 200) <= 0) {
                continue;
            }
            std::array<u8, 512> buffer;
            const ssize_t size = ::recv(socket_fd, buffer.data(), buffer.size(), 0);
            if (size <= 0) {
                LOG_WARNING(Input, "The TV's controller service is gone");
                break;
            }
            TvProtocol::Header header;
            if (size < static_cast<ssize_t>(sizeof(header))) {
                continue;
            }
            std::memcpy(&header, buffer.data(), sizeof(header));
            if (header.type == TvProtocol::Type::Input &&
                size >= static_cast<ssize_t>(sizeof(TvProtocol::Input))) {
                TvProtocol::Input message;
                std::memcpy(&message, buffer.data(), sizeof(message));
                OnInput(message);
            } else if (header.type == TvProtocol::Type::Controllers &&
                       size >= static_cast<ssize_t>(sizeof(TvProtocol::Controllers))) {
                TvProtocol::Controllers message;
                std::memcpy(&message, buffer.data(), sizeof(message));
                OnControllers(message);
            }
        }
        Close();
    }
}

void TvControllers::OnControllers(const TvProtocol::Controllers& message) {
    std::array<Seat, Players> wanted{};
    const std::size_t count = (std::min<std::size_t>)(message.count, TvProtocol::MaxControllers);
    for (std::size_t i = 0; i < count; ++i) {
        const TvProtocol::Controller& controller = message.controllers[i];
        if (controller.player < 1 || controller.player > Players) {
            continue;
        }
        Seat& seat = wanted[controller.player - 1];
        switch (controller.grip) {
        case TvProtocol::Grip::Pair:
            seat.style = NpadStyleIndex::JoyconDual;
            (controller.side == TvProtocol::Side::Left ? seat.left : seat.right) = controller.id;
            break;
        case TvProtocol::Grip::Sideways:
            if (controller.side == TvProtocol::Side::Left) {
                seat.style = NpadStyleIndex::JoyconLeft;
                seat.left = controller.id;
            } else {
                seat.style = NpadStyleIndex::JoyconRight;
                seat.right = controller.id;
            }
            break;
        case TvProtocol::Grip::Pro:
            seat.style = NpadStyleIndex::Fullkey;
            seat.left = seat.right = controller.id;
            break;
        case TvProtocol::Grip::Waiting:
            break;
        }
    }

    bool changed;
    {
        std::scoped_lock lock{mutex};
        described = message;
        // Battery levels and the ring come with every description; who holds what is
        // only touched when it changed, because a game may have moved halves since.
        changed = wanted != assigned;
        if (changed) {
            assigned = wanted;
            seats = wanted;
        }
    }

    for (std::size_t player = 0; player < Players; ++player) {
        EmulatedController& controller = *hid_core.GetEmulatedControllerByIndex(player);
        // Read afresh, before and after: applying a pair may take it apart again, which
        // moves a half to a later player. (Applied from the copy made before the loop,
        // that later player was disconnected again right after it got its Joy-Con.)
        Seat seat;
        {
            std::scoped_lock lock{mutex};
            seat = seats[player];
        }
        if (changed) {
            Apply(player, seat);
            std::scoped_lock lock{mutex};
            seat = seats[player];
        }
        for (std::size_t i = 0; i < count; ++i) {
            const TvProtocol::Controller& one = message.controllers[i];
            if (one.side == TvProtocol::Side::Pro && one.id == seat.left) {
                ApplyBattery(controller, Half::Both, one);
            } else if (one.id == seat.left) {
                ApplyBattery(controller, Half::Left, one);
            } else if (one.id == seat.right) {
                ApplyBattery(controller, Half::Right, one);
            }
        }
        // A game that had the ring on still wants it after the service was away.
        if (changed && playing && seat.right != Nobody &&
            controller.controller.right_polling_mode == Common::Input::PollingMode::Ring) {
            WantRing(seat.right, true);
        }
    }
}

void TvControllers::Apply(std::size_t player, const Seat& seat) {
    EmulatedController& controller = *hid_core.GetEmulatedControllerByIndex(player);
    if (seat.style == NpadStyleIndex::None) {
        if (controller.IsConnected()) {
            LOG_INFO(Input, "Player {} has no controller any more", player + 1);
        }
        controller.Disconnect();
        return;
    }
    if (controller.IsConnected() && controller.GetNpadStyleIndex() == seat.style) {
        return;
    }
    controller.Disconnect();
    controller.SetNpadStyleIndex(seat.style);
    controller.Connect();
    LOG_INFO(Input, "Player {} holds controller style {}: {}", player + 1, seat.style,
             controller.IsConnected() ? "connected" : "not accepted by the game");

    // A whole pair, at a player whose Joy-Cons the game wants apart.
    std::optional<bool> left_stays;
    std::function<void(NpadIdType, bool)> split;
    {
        std::scoped_lock lock{mutex};
        if (seat.style == NpadStyleIndex::JoyconDual && seat.left != Nobody && seat.right != Nobody) {
            left_stays = wanted_apart[player];
            split = splitter;
        }
    }
    if (left_stays && split && controller.IsConnected()) {
        LOG_INFO(Input, "Player {}'s Joy-Cons are taken apart again, as the game asked before",
                 player + 1);
        split(controller.npad_id_type, *left_stays);
    }
}

void TvControllers::SetJoyAssignment(NpadIdType npad_id, bool single, bool left_stays) {
    const std::size_t player = Service::HID::NpadIdTypeToIndex(npad_id);
    if (player >= Players) {
        return;
    }
    std::scoped_lock lock{mutex};
    if (single) {
        wanted_apart[player] = left_stays;
    } else {
        wanted_apart[player].reset();
    }
}

void TvControllers::SetSplitter(std::function<void(NpadIdType, bool)> splitter_) {
    std::scoped_lock lock{mutex};
    splitter = std::move(splitter_);
}

void TvControllers::Reload(EmulatedController& controller) {
    const std::size_t player = Service::HID::NpadIdTypeToIndex(controller.npad_id_type);

    // No bindings: every input device below is then an empty one, and nothing but this
    // class writes the controller's state.
    for (auto& params : controller.button_params) {
        params = {};
    }
    for (auto& params : controller.stick_params) {
        params = {};
    }
    for (auto& params : controller.motion_params) {
        params = {};
    }
    controller.ring_params[0] = {};
    controller.controller.color_values = {};
    controller.ReloadColorsFromSettings();

    controller.Disconnect();
    controller.ReloadInput();

    Seat seat;
    {
        std::scoped_lock lock{mutex};
        if (player < Players) {
            // A new start: whatever a game did with the Joy-Cons, or asked for, is over.
            seats[player] = assigned[player];
            wanted_apart[player].reset();
            seat = seats[player];
        }
    }
    if (player < Players) {
        Apply(player, seat);
    }
}

void TvControllers::ApplyBattery(EmulatedController& controller, Half half,
                                 const TvProtocol::Controller& described_controller) {
    const NpadPowerInfo power = PowerInfo(described_controller);
    auto& battery = controller.controller.battery_state;
    switch (half) {
    case Half::Left:
        battery.left = power;
        battery.dual = power;
        break;
    case Half::Right:
        battery.right = power;
        break;
    case Half::Both:
        battery.dual = power;
        battery.left = power;
        battery.right = power;
        break;
    }
    controller.TriggerOnChange(ControllerTriggerType::Battery, true);
}

void TvControllers::OnInput(const TvProtocol::Input& received) {
    TvProtocol::Input message = received;
    std::array<Seat, Players> now;
    TvProtocol::Side side = TvProtocol::Side::Left;
    bool known = false;
    {
        std::scoped_lock lock{mutex};
        now = seats;
        // What was held when the game took over stays the TV's until it is let go.
        const bool pushed = message.left[0] != 0.0f || message.left[1] != 0.0f ||
                            message.right[0] != 0.0f || message.right[1] != 0.0f;
        last_buttons[message.id] = message.buttons;
        last_pushed[message.id] = pushed;
        withheld_buttons[message.id] &= message.buttons;
        message.buttons &= ~withheld_buttons[message.id];
        withheld_sticks[message.id] = withheld_sticks[message.id] && pushed;
        if (withheld_sticks[message.id]) {
            message.left[0] = message.left[1] = message.right[0] = message.right[1] = 0.0f;
        }
        for (std::size_t i = 0; i < described.count && i < TvProtocol::MaxControllers; ++i) {
            if (described.controllers[i].id == message.id) {
                side = described.controllers[i].side;
                known = true;
            }
        }
    }
    if (!known) {
        return;
    }
    u64& last_time = last_motion[message.id];
    for (std::size_t player = 0; player < Players; ++player) {
        const Seat& seat = now[player];
        if (seat.left != message.id && seat.right != message.id) {
            continue;
        }
        EmulatedController& controller = *hid_core.GetEmulatedControllerByIndex(player);
        const Half half = side == TvProtocol::Side::Pro    ? Half::Both
                          : side == TvProtocol::Side::Left ? Half::Left
                                                           : Half::Right;
        ApplyInput(controller, half, message, last_time);
    }
    last_time = message.motion[TvProtocol::MotionReadings - 1].time;
}

void TvControllers::ApplyInput(EmulatedController& controller, Half half,
                               const TvProtocol::Input& message, u64& last_time) {
    auto& state = controller.controller;
    const u32 buttons = message.buttons;
    const auto pressed = [buttons](TvProtocol::Button button) { return (buttons & button) != 0; };
    const auto stick = [](const float value[2]) {
        return AnalogStickState{
            .x = static_cast<s32>(std::clamp(value[0], -1.0f, 1.0f) * StickMax),
            .y = static_cast<s32>(std::clamp(value[1], -1.0f, 1.0f) * StickMax),
        };
    };

    auto& pad = state.npad_button_state;
    if (half != Half::Right) {
        pad.left.Assign(pressed(TvProtocol::Left));
        pad.up.Assign(pressed(TvProtocol::Up));
        pad.right.Assign(pressed(TvProtocol::Right));
        pad.down.Assign(pressed(TvProtocol::Down));
        pad.l.Assign(pressed(TvProtocol::L));
        pad.zl.Assign(pressed(TvProtocol::ZL));
        pad.minus.Assign(pressed(TvProtocol::Minus));
        pad.stick_l.Assign(pressed(TvProtocol::LeftStick));
        pad.left_sl.Assign(pressed(TvProtocol::LeftSL));
        pad.left_sr.Assign(pressed(TvProtocol::LeftSR));
        state.capture_button_state.capture.Assign(pressed(TvProtocol::Capture));
        state.analog_stick_state.left = stick(message.left);
        pad.stick_l_left.Assign(message.left[0] < -StickDirection);
        pad.stick_l_right.Assign(message.left[0] > StickDirection);
        pad.stick_l_down.Assign(message.left[1] < -StickDirection);
        pad.stick_l_up.Assign(message.left[1] > StickDirection);
    }
    if (half != Half::Left) {
        pad.a.Assign(pressed(TvProtocol::A));
        pad.b.Assign(pressed(TvProtocol::B));
        pad.x.Assign(pressed(TvProtocol::X));
        pad.y.Assign(pressed(TvProtocol::Y));
        pad.r.Assign(pressed(TvProtocol::R));
        pad.zr.Assign(pressed(TvProtocol::ZR));
        pad.plus.Assign(pressed(TvProtocol::Plus));
        pad.stick_r.Assign(pressed(TvProtocol::RightStick));
        pad.right_sl.Assign(pressed(TvProtocol::RightSL));
        pad.right_sr.Assign(pressed(TvProtocol::RightSR));
        state.analog_stick_state.right = stick(message.right);
        pad.stick_r_left.Assign(message.right[0] < -StickDirection);
        pad.stick_r_right.Assign(message.right[0] > StickDirection);
        pad.stick_r_down.Assign(message.right[1] < -StickDirection);
        pad.stick_r_up.Assign(message.right[1] > StickDirection);

        // The ring's raw value, as the share of the range the pretend ring was told.
        const f32 force =
            message.ring == TvProtocol::NoRing
                ? 0.0f
                : std::clamp(static_cast<f32>(message.ring - TvRingRest) / TvRingRange, -1.0f, 1.0f);
        state.ring_analog_state.force = force;
    }
    controller.TriggerOnChange(ControllerTriggerType::Button, true);
    controller.TriggerOnChange(ControllerTriggerType::Stick, true);
    if (half != Half::Left) {
        controller.TriggerOnChange(ControllerTriggerType::RingController, true);
    }
    if (message.quiet != 0) {
        return;
    }

    // Motion, every reading with the time since the one before. The service's axes (x to
    // the controller's top, y to its left, z out of its face) become the Switch's as
    // input_common's Joy-Con driver gives them: acceleration (y, -x, -z) in g, rotation
    // (-y, x, z) in turns a second.
    const std::size_t first = half == Half::Right ? 1 : 0;
    const std::size_t last = half == Half::Left ? 0 : 1;
    for (std::size_t index = first; index <= last; ++index) {
        auto& emulated = state.motion_values[index].emulated;
        u64 before = last_time;
        for (const TvProtocol::MotionReading& reading : message.motion) {
            const u64 step = before == 0 || reading.time <= before
                                 ? MotionStepUs
                                 : (std::min)(reading.time - before, LongestStepUs);
            before = reading.time;
            emulated.SetAcceleration(Common::Vec3f{
                reading.acceleration[1],
                -reading.acceleration[0],
                -reading.acceleration[2],
            });
            emulated.SetGyroscope(Common::Vec3f{
                -reading.rotation[1] / 360.0f,
                reading.rotation[0] / 360.0f,
                reading.rotation[2] / 360.0f,
            });
            emulated.UpdateRotation(step);
            emulated.UpdateOrientation(step);
        }
        auto& motion = state.motion_state[index];
        motion.accel = emulated.GetAcceleration();
        motion.gyro = emulated.GetGyroscope();
        motion.rotation = emulated.GetRotations();
        motion.euler = emulated.GetEulerAngles();
        motion.orientation = emulated.GetOrientation();
        motion.is_at_rest = !emulated.IsMoving(controller.motion_sensitivity);
    }
    controller.TriggerOnChange(ControllerTriggerType::Motion, true);
}

void TvControllers::WantRing(u8 id, bool on) {
    TvProtocol::SetRing request{};
    request.id = id;
    request.on = on;
    Send(&request, sizeof(request));
}

Common::Input::DriverResult TvControllers::SetPollingMode(EmulatedController& controller,
                                                          EmulatedDeviceIndex device_index,
                                                          Common::Input::PollingMode polling_mode) {
    const std::size_t player = Service::HID::NpadIdTypeToIndex(controller.npad_id_type);
    Seat seat;
    {
        std::scoped_lock lock{mutex};
        if (player >= Players) {
            return Common::Input::DriverResult::NotSupported;
        }
        seat = seats[player];
    }
    if (device_index != EmulatedDeviceIndex::RightIndex) {
        controller.controller.left_polling_mode = polling_mode;
    }
    if (device_index != EmulatedDeviceIndex::LeftIndex) {
        const bool ring = polling_mode == Common::Input::PollingMode::Ring;
        const bool was = controller.controller.right_polling_mode == Common::Input::PollingMode::Ring;
        controller.controller.right_polling_mode = polling_mode;
        if (seat.right != Nobody && ring != was && (playing || !ring)) {
            WantRing(seat.right, ring);
        }
    }
    return Common::Input::DriverResult::Success;
}

void TvControllers::SetLeds(EmulatedController& controller) {
    const std::size_t player = Service::HID::NpadIdTypeToIndex(controller.npad_id_type);
    Seat seat;
    {
        std::scoped_lock lock{mutex};
        if (player >= Players) {
            return;
        }
        seat = seats[player];
    }
    const LedPattern pattern = controller.GetLedPattern();
    TvProtocol::SetLights request{};
    request.lights = static_cast<u8>((pattern.position1 != 0 ? 1 : 0) | (pattern.position2 != 0 ? 2 : 0) |
                                     (pattern.position3 != 0 ? 4 : 0) | (pattern.position4 != 0 ? 8 : 0));
    for (const u8 id : {seat.left, seat.right}) {
        if (id == Nobody || (id == seat.right && seat.right == seat.left)) {
            continue;
        }
        request.id = id;
        Send(&request, sizeof(request));
    }
}

bool TvControllers::SetVibration(EmulatedController& controller, DeviceIndex device_index,
                                 const VibrationValue& vibration) {
    const std::size_t player = Service::HID::NpadIdTypeToIndex(controller.npad_id_type);
    Seat seat;
    {
        std::scoped_lock lock{mutex};
        if (player >= Players) {
            return false;
        }
        seat = seats[player];
    }
    // A Joy-Con is the half of its side; a Pro Controller is both, with a motor in each grip.
    const bool right = device_index == DeviceIndex::Right;
    const u8 id = right ? seat.right : seat.left;
    if (device_index == DeviceIndex::None || id == Nobody) {
        return false;
    }
    TvProtocol::Rumble message{};
    message.id = id;
    message.motor = right ? TvProtocol::Motor::Right : TvProtocol::Motor::Left;
    message.lowAmplitude = vibration.low_amplitude;
    message.lowFrequency = vibration.low_frequency;
    message.highAmplitude = vibration.high_amplitude;
    message.highFrequency = vibration.high_frequency;
    Send(&message, sizeof(message));
    return true;
}

void TvControllers::SetPlaying(bool playing_) {
    if (playing_ && !playing) {
        std::scoped_lock lock{mutex};
        withheld_buttons = last_buttons;
        withheld_sticks = last_pushed;
    }
    playing = playing_;
    TvProtocol::Playing message{};
    message.on = playing_;
    Send(&message, sizeof(message));

    // A parked game doesn't need its ring read (the Joy-Con's battery); a game that
    // carries on gets it back.
    std::array<Seat, Players> now;
    {
        std::scoped_lock lock{mutex};
        now = seats;
    }
    for (std::size_t player = 0; player < Players; ++player) {
        const EmulatedController& controller = *hid_core.GetEmulatedControllerByIndex(player);
        if (now[player].right != Nobody &&
            controller.controller.right_polling_mode == Common::Input::PollingMode::Ring) {
            WantRing(now[player].right, playing_);
        }
    }
}

NpadButton TvControllers::AsThePlayerSeesIt(NpadButton button) {
    {
        std::scoped_lock lock{mutex};
        bool in_ring = false;
        for (std::size_t i = 0; i < described.count && i < TvProtocol::MaxControllers; ++i) {
            in_ring = in_ring || (described.controllers[i].id == seats[0].right &&
                                  described.controllers[i].ring != 0);
        }
        if (!in_ring) {
            return button;
        }
    }
    switch (button) {
    case NpadButton::StickRUp:
        return NpadButton::StickRRight;
    case NpadButton::StickRRight:
        return NpadButton::StickRDown;
    case NpadButton::StickRDown:
        return NpadButton::StickRLeft;
    case NpadButton::StickRLeft:
        return NpadButton::StickRUp;
    default:
        return button;
    }
}

void TvControllers::Split(NpadIdType from, NpadIdType to, bool left_stays) {
    const std::size_t source = Service::HID::NpadIdTypeToIndex(from);
    const std::size_t target = Service::HID::NpadIdTypeToIndex(to);
    if (source >= Players || target >= Players) {
        return;
    }
    std::scoped_lock lock{mutex};
    seats[target] = {};
    seats[target].style = NpadStyleIndex::JoyconDual;
    if (left_stays) {
        seats[target].right = seats[source].right;
        seats[source].right = Nobody;
        seats[source].style = NpadStyleIndex::JoyconLeft;
    } else {
        seats[target].left = seats[source].left;
        seats[source].left = Nobody;
        seats[source].style = NpadStyleIndex::JoyconRight;
    }
    LOG_INFO(Input, "The game took player {}'s Joy-Cons apart: the {} one is now player {}",
             source + 1, left_stays ? "right" : "left", target + 1);
}

void TvControllers::Merge(NpadIdType into, NpadIdType from) {
    const std::size_t target = Service::HID::NpadIdTypeToIndex(into);
    const std::size_t source = Service::HID::NpadIdTypeToIndex(from);
    if (source >= Players || target >= Players) {
        return;
    }
    std::scoped_lock lock{mutex};
    if (seats[target].left == Nobody) {
        seats[target].left = seats[source].left;
    }
    if (seats[target].right == Nobody) {
        seats[target].right = seats[source].right;
    }
    seats[target].style = NpadStyleIndex::JoyconDual;
    seats[source] = {};
    LOG_INFO(Input, "The game put the Joy-Cons of players {} and {} together", target + 1, source + 1);
}

} // namespace Core::HID
