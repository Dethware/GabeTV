# Eden on the Steam Machine: development

How our build of Eden differs from Eden's own, and why. For building and installing see
`README.md`; findings and decisions with their dates are in `../../docs/gabecube-findings.md`
("Eden for Ring Fit").

## What is ours

- **`build.sh`** fetches a pinned Eden version, places our own source files in its tree,
  applies `patches/*.patch` and builds a plain folder. Eden's own copy of `fmt` is built in
  (`-Dfmt_FORCE_BUNDLED=ON`): the version in SteamOS's repositories is newer than the one
  Eden is written for and doesn't compile with it.
- **`driver/tv_controllers.{h,cpp}`** go to `src/hid_core/frontend/`, and the controller
  service's contract, `../../controllers/protocol/emulator.h`, goes next to them as
  `tv_emulator_protocol.h`. They link into Eden, so they are GPL-3 like Eden.
- **`patches/tv-controllers.patch`** is the hooks in Eden's own files, kept as few as
  possible so that a new Eden version rarely conflicts with them.

## Controllers

Eden's controllers come from the TV's controller service (`../../controllers`) and from
nowhere else. Stock Eden builds each player's controller from binding strings in its
settings: one per button, stick and motion sensor, naming an input driver and a device by
GUID, with dead zones, ranges, toggles and a filter against two devices fighting over one
button. All of that exists to cope with whatever a PC has plugged in. Here one service
owns the Joy-Cons and the Pro Controller and already delivers them calibrated and timed,
so that layer is bypassed:

- `TvControllers` (one, owned by `HIDCore`) connects to the service's emulator socket from
  a thread of its own and is a friend of `EmulatedController`, the object that is a
  player's controller to the emulated Switch's HID services.
- **Who holds what.** From the service's list of controllers it gives players 1 to 4
  their kind (a pair is two Joy-Cons, a sideways Joy-Con a single one, a Pro Controller a
  Pro Controller) and connects or disconnects them. `EmulatedController::
  ReloadFromSettings` hands over to `TvControllers::Reload`, which empties the bindings
  (every input device is then a dummy) and applies the service's state; the players'
  type, bindings and "connected" in Eden's settings are not read.
- **State.** For every report of a controller it writes buttons, stick, battery and the
  ring's force straight into the `EmulatedController`'s state and feeds the three motion
  readings of the report, each with the time since the one before, into Eden's
  orientation code (`MotionInput`). The units and axes are the ones Eden's own Joy-Con
  driver produces: acceleration `(y, -x, -z)` of the service's axes in g, rotation
  `(-y, x, z)` in turns a second.
- **A change of hands.** A press belongs to whoever had the controller when it went
  down. `SetPlaying(true)` is called before a game carries on, and what is held then (the
  press on the game's tile) is kept from the game until it is let go; a pushed stick stays
  centred until it has been centred. The service does the same for its virtual pads when
  a game lets go.
- **A game moving Joy-Cons.** Each player has a left and a right half, which are the
  service's ids of the controllers. When a game takes a pair apart (`NPad::SetNpadMode`;
  Ring Fit keeps the right Joy-Con as player 1 and makes the left one player 2) or puts
  two single ones together (`MergeSingleJoyAsDualJoy`), the hooks there move the halves
  (`Split`, `Merge`). A fresh start (`Reload`) or a change of who holds what at the
  service resets them to the service's. What the game last asked for a player's Joy-Cons
  is remembered (`SetJoyAssignment`, from `SetNpadMode`, also when nobody is connected
  just then): a whole pair that appears at a player whose Joy-Cons the game wants apart,
  because it was formed late or formed again after the machine slept, is taken apart
  again through a callback the HID service sets, which does what the game's own request
  does.
- **What the game asks of a controller.** `SetPollingMode` and `SetLedPattern` go to
  `TvControllers`: switching the ring on or off becomes a request to the service, so the
  Joy-Con's second processor runs only while a game uses the ring.
- **The ring.** The game gets the ring's raw value. Eden's pretend ring
  (`hid_core/hidbus/ringcon.h`) reports the numbers of our real ring (`TvRingRest`,
  `TvRingRange`) in place of its own, and `TvControllers` gives the raw value as its share
  of that range, which the ring code turns back into the same number.
- **Eden's own dialogs.** Its on-screen keyboard stands in for the Switch's and moves by
  the stick's direction. In the ring the Joy-Con is a quarter turned (its top to the
  player's right), so the dialog asks `AsThePlayerSeesIt` first, which turns the right
  stick's directions while that Joy-Con has a ring attached. Games get the stick as it
  is.
- **Locks.** `TvControllers`' own lock is never held while an `EmulatedController`
  notifies its listeners: those take the HID services' locks, and the services call
  `Split` and `Merge` while holding them.

## Parking a game

Eden has no save states: a game is kept by keeping Eden running and paused. `TvPlayer`
(`driver/tv_player.*`, placed in `src/yuzu/`) is MPRIS as far as the TV shell uses it:
`PlaybackStatus` with its change signal, `Pause` and `Play`, as `org.mpris.MediaPlayer2.eden`
on the session bus, hooked to the main window's own pause and continue. Eden's pause stops
the emulated clock, which freezing the process from outside wouldn't. The shell pauses it on
Home and before the machine sleeps, and continues it from the game's tile
(`../../shell/DEVELOPMENT.md`, managed apps). `TvPlayer` also tells `TvControllers` when a
game is being played, which passes it to the controller service: the Joy-Cons are then the
game's alone. A paused game also gives up its ring and asks for it again when it carries
on.

A parked game survives Home, other apps and sleep; not a reboot, an update or a crash.

Closing: Eden ends on SIGTERM, but stock Eden first asks "are you sure?" in a dialog nobody
on the TV can answer. Our main window hook sets `confirm_before_stopping` to "never" at
start, so the shell's Close (stopping `eden.service`) ends it properly; Force close kills it.

## The rest

Eden's other controller drivers are still compiled. Its Joy-Con driver and its cemuhook
client are switched off in the settings (`enable_joycon_driver`, `udp_input_servers`); SDL
runs but nothing is bound to it.

## Settings that matter

In `~/apps/eden/user/config/qt-config.ini` on the Steam Machine; a value only counts when
the `…\default` line above it says `false`:

- `region_index=2` (Europe) and `language_index=12` (British English). With the default,
  USA, Ring Fit asks "Canada or outside Canada" and uses imperial units outside.
- `enable_joycon_driver=false`, `udp_input_servers=` (empty).

Eden must be started with `~/apps/eden` as its working directory: that is where it looks
for `user/`.

## Next

1. A pass over all of this for refactoring and architecture: it was built in one long
   evening of trying things on the TV, and was meant to be gone over afterwards.
2. Rumble from the game: Ring Fit uses it. The message exists in the protocol; neither
   Eden's side nor the service's does anything with it yet.
3. The IR camera (Ring Fit reads a pulse with it).
4. More games, with their own art: a library of Switch games in the shell's Games place
   (name and icon read from each game file, which needs Eden's own loader and the keys),
   and one unit per game in place of `eden.service` naming Ring Fit.
5. A Joy-Con held sideways in a game (its buttons and stick are passed as held upright).
6. User profiles pass through to Eden (its user, and with it the saves, follows who is
   using the TV), once the TV has them: a system of its own, see `../../DEVELOPMENT.md`.

Not pursued: the stray geometry the user saw once or twice in Ring Fit's first level
("very minor"), which is Eden's rendering; and the player lights as the game sets them. A
game can only set those, not read them, so they change nothing but the look: on a Switch
the Joy-Con in the ring shows player 1 and the one on the leg player 2, because the game
takes the pair apart, and here both show the pair's number. The game's request reaches the
service, which ignores it.
