// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <optional>
#include <mutex>
#include <stop_token>
#include <thread>

#include "common/common_types.h"
#include "common/input.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/frontend/tv_emulator_protocol.h"
#include "hid_core/hid_types.h"

namespace TvProtocol = TvControllers::Emulator;

namespace Core::HID {

class HIDCore;

// A Ring-Con's numbers as our own ring gives them (measured): about 3700 at rest, 1474
// pulled and 6217 squeezed as far as it goes. The pretend ring the game talks to
// (core/hle/service/hid/hidbus/ringcon.h) is given these in place of its own, so what the
// game gets is the ring's raw value.
constexpr s16 TvRingRest = 3700;
constexpr s16 TvRingRange = 2600;

// The players' controllers come from the TV's controller service and from nowhere else.
//
// The service owns the Joy-Cons and the Pro Controller: it knows which are connected, who
// holds which and how, and it hands every one of them over by itself, already calibrated
// and timed (tv_emulator_protocol.h). This class takes that and writes it into the
// EmulatedControllers as the state of a Switch's own controllers: no bindings, no devices
// by GUID, no dead zones or smoothing of its own. What a game asks of a controller (the
// ring, the lights) goes back to the service.
//
// Which player has which controller starts as the service says. A game may take a pair of
// Joy-Cons apart and put them together again; the halves then go where the game puts
// them (Split, Merge).
class TvControllers {
public:
    explicit TvControllers(HIDCore& hid_core_);
    ~TvControllers();

    TvControllers(const TvControllers&) = delete;
    TvControllers& operator=(const TvControllers&) = delete;

    // In place of EmulatedController::ReloadFromSettings.
    void Reload(EmulatedController& controller);

    // In place of the controller's own outputs.
    Common::Input::DriverResult SetPollingMode(EmulatedController& controller,
                                               EmulatedDeviceIndex device_index,
                                               Common::Input::PollingMode polling_mode);
    void SetLeds(EmulatedController& controller);

    // A game is being played, or it is paused or there is none: passed on to the service,
    // which keeps the controllers to the game while one is. Call it before the game
    // carries on: a press belongs to whoever had the controller when it went down, so
    // what is held at that moment (the press on the game's tile) is kept from the game
    // until it is let go.
    void SetPlaying(bool playing_);

    // For Eden's own dialogs (its on-screen keyboard): a stick direction of player 1's
    // right Joy-Con as the player sees it. In the Ring-Con the Joy-Con sits on its rail,
    // turned a quarter, its top to the player's right; pushing the stick up as the player
    // sees it is then "left" to the Joy-Con. Games get the stick as it is, like on a
    // Switch; this is only for the dialogs that stand in for the Switch's own.
    NpadButton AsThePlayerSeesIt(NpadButton button);

    // What the game last asked for a player's Joy-Cons: apart (and which one stays) or
    // together. It is remembered, because the Joy-Cons may not be there when it asks, or
    // may go away and come back (the machine slept, L+R again): a pair that appears at a
    // player whose Joy-Cons the game wants apart is taken apart again, through `splitter`,
    // which the HID service sets to do what it does when the game asks.
    void SetJoyAssignment(NpadIdType npad_id, bool single, bool left_stays);
    void SetSplitter(std::function<void(NpadIdType, bool left_stays)> splitter_);

    // The game takes the pair at `from` apart: one Joy-Con stays, the other becomes the
    // controller at `to`.
    void Split(NpadIdType from, NpadIdType to, bool left_stays);
    // The game makes one pair of the single Joy-Cons at `into` and `from`.
    void Merge(NpadIdType into, NpadIdType from);

private:
    static constexpr std::size_t Players = 8;
    static constexpr u8 Nobody = TvProtocol::NoController;

    // What a player holds: the service's ids of the controllers that are its left and its
    // right half. A Pro Controller is both.
    struct Seat {
        u8 left{Nobody};
        u8 right{Nobody};
        NpadStyleIndex style{NpadStyleIndex::None};

        bool operator==(const Seat&) const = default;
    };

    enum class Half { Left, Right, Both };

    void Run(std::stop_token stop_token);
    bool Connect();
    void Close();
    void Send(const void* message, std::size_t size);

    void OnControllers(const TvProtocol::Controllers& message);
    void OnInput(const TvProtocol::Input& received);

    // Makes the emulated controller what its seat says: its kind, and whether it is there.
    void Apply(std::size_t player, const Seat& seat);
    void ApplyInput(EmulatedController& controller, Half half,
                    const TvProtocol::Input& message, u64& last_time);
    void ApplyBattery(EmulatedController& controller, Half half,
                      const TvProtocol::Controller& described);
    void WantRing(u8 id, bool on);

    HIDCore& hid_core;

    // Guards the seats and what the service last said. Never held while an
    // EmulatedController's callbacks run: those take the HID services' locks, and the
    // services call Split and Merge while holding them.
    std::mutex mutex;
    // As the service says, and as they are now that a game may have moved halves.
    std::array<Seat, Players> assigned{};
    std::array<Seat, Players> seats{};
    TvProtocol::Controllers described{};
    // Players whose Joy-Cons the game wants apart: whether the left one stays.
    std::array<std::optional<bool>, Players> wanted_apart{};
    std::function<void(NpadIdType, bool)> splitter;
    // The time of each controller's last motion reading, by id.
    std::array<u64, 256> last_motion{};
    // By id: what each controller last reported, and what of it was already held when the
    // game took over and hasn't been let go since (a stick: pushed then, not centred since).
    std::array<u32, 256> last_buttons{};
    std::array<bool, 256> last_pushed{};
    std::array<u32, 256> withheld_buttons{};
    std::array<bool, 256> withheld_sticks{};

    std::atomic<bool> playing{false};

    std::mutex send_mutex;
    int socket_fd{-1};
    std::jthread thread;
};

} // namespace Core::HID
