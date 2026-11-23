# TV controller service: development

`tvcontrollers` is the only program that talks to the Joy-Cons. The decision behind it
("we override everything") and the measurements are in the log,
`docs/gabecube-findings.md`, under "Joy-Con".

## The layers

| Layer | Who | Through |
|---|---|---|
| Bluetooth link, pairing keys | bluez | driven over the system D-Bus (`Bluetooth`) |
| Nobody else gets at a Joy-Con | udev rule | `system/72-tv-joycon.rules` |
| Finding connected Joy-Cons | `DeviceWatcher` | `/dev/hidraw*` and sysfs |
| Protocol: reports, subcommands | `JoyCon`, `JoyConProtocol.h` | read and write on the hidraw node |
| Which Joy-Cons are one controller | `UnitManager`, `Unit` | |
| What games see | `VirtualPad` | uinput |
| What the shell sees | `ControllerService` | session D-Bus, `local.tvcontrollers` |

Each layer rests on a basic Linux interface (bluez's D-Bus API, udev permissions, hidraw,
uinput), not on anyone's Joy-Con support. Steam's Joy-Con handling, SDL's Joy-Con driver,
`joycond` and the emulators' own drivers are deliberately not used.

## Keeping everyone else out

When a Joy-Con connects, bluez hands it to the kernel as a HID device. On stock SteamOS
the kernel's `hid_nintendo` binds it and makes two input devices next to the hidraw node;
SteamOS opens all hidraw nodes to the logged-in user, and Steam and every SDL program then
talk to the Joy-Con too. Two files stop that:

- **`system/72-tv-joycon.rules`** removes the user-access tag from a Joy-Con's hidraw and
  input nodes and gives them to group `input`, which the `deck` user is not in. The service
  is a system unit that runs as `deck` with `SupplementaryGroups=input`, so it is the only
  process that can open them. The file must sort after `70-steam-jupiter-input.rules` and
  `70-uaccess.rules`, which set the tag, and before `73-seat-late.rules`, which applies it.
- **`system/tv-no-hid-nintendo.conf`** (in `/etc/modprobe.d`) switches `hid_nintendo` off.
  A Joy-Con then gets `hid-generic`, which only provides the hidraw node and writes
  nothing. Sharing the Joy-Con with `hid_nintendo` didn't work: it sets the Joy-Con up when
  it binds (calibration, motion, player light 1) and keeps sending for about a second after
  its input devices appear, so its writes landed on top of the service's, and the service's
  traffic made its rate limiter give up (`joycon_enforce_subcmd_rate: exceeded max
  attempts`). The kernel offers no way to switch a driver off for some devices only, so
  this also takes it off the Pro Controller, which Steam reads through hidraw itself.
  `DeviceWatcher` still copes with the driver being back (it waits for its input devices
  and logs a warning), in case an update drops the file.

Neither file is on SteamOS's default keep list; `system/tv-controllers-keep.conf` adds them.

Because the unit runs as `deck`, its process can be ended without root, and
`Restart=always` brings it back: that is how `deploy.sh` restarts it. The unit sets the
session bus address by hand; the service may start before the session exists and retries
the bus every three seconds.

## The parts

- **`DeviceWatcher`** watches `/dev` and, for each hidraw node, reads
  `/sys/class/hidraw/<node>/device/uevent`: `HID_ID` for vendor and product (057e:2006 left,
  2007 right), `HID_UNIQ` for the Bluetooth address. A node appears before udev has set its
  group, so opening is retried for five seconds before it is reported as refused.
- **`JoyCon`** is one physical Joy-Con. It reads input reports (`0x30`, 66 a second, and
  `0x21`, a subcommand's reply) into a `State`: buttons, the stick as two raw 12-bit
  values, battery. Subcommands go out one at a time; each waits 250 ms for its reply and is
  tried three times. At start it asks for the device info and switches to full reports,
  then emits `ready()`. The first subcommand is tried for 5 s: a Joy-Con answers about
  0.4 s after its node appears. The lights are read back 0.4 s after being set and set
  again if they differ. A Joy-Con that is switched off says nothing: it just stops, and the
  machine keeps the dead connection for Bluetooth's 20 s supervision timeout, during which
  a button press can't reconnect. A Joy-Con out of reach must not be dropped, though: its
  connection resumes by itself (seen after 15 s away). The two differ in the second before
  the silence: a switch-off comes out of a regular stream (longest gap 28 to 45 ms), a
  fading link has late and missing reports (gaps of 250 ms). So after 1 s without a
  report `JoyCon` is marked quiet (`quietChanged()`), and if none of the last 64 gaps was
  over 100 ms it also emits `switchedOff()`, on which `main.cpp` has `Bluetooth`
  disconnect it; bluez needs about 4 s for that. Otherwise the connection is left to
  Bluetooth. The log line gives the numbers and the battery level, which tells an empty
  battery from a switch-off.
- **`UnitManager`** holds the connected Joy-Cons that are in no unit (their lights flash)
  and forms units from them on the Switch's own gestures: SL+SR on one Joy-Con makes it a
  sideways controller, L on a left one together with R on a right one makes a pair. A unit
  gets the lowest free player number, 1 to 4. When a Joy-Con leaves, its unit ends and a
  partner goes back to waiting.
- **`Unit`** is one controller as a player holds it. It shows the player number on the
  lights (one to four lit) and turns its Joy-Cons' state into one `VirtualPad` state, by
  button position: a pair maps like a full controller; a sideways Joy-Con is turned a
  quarter, so its four buttons and its stick are rotated (the tables and the two sign
  pairs are in `Unit.cpp`). While any of its Joy-Cons is quiet the pad is neutral, so
  nothing stays held. Home is not mapped; it becomes `homePressed()`.
- **`VirtualPad`** is the uinput controller games see. It presents itself as an Xbox 360
  pad (USB id 045e:028e, the kernel `xpad` driver's buttons and axes), the layout every
  program knows without a mapping. It declares the guide button, because leaving it out
  would renumber the stick buttons for programs that count them, and never presses it.
  Steam wraps it like any controller, and the shell reads Steam's pad.
- **Stick calibration** comes from the Joy-Con's memory (subcommand `0x10`): the factory
  values, replaced by the user's own if those are marked valid. `JoyCon::stick()` gives
  -1 to 1 with a 15 % dead zone.
- **`Bluetooth`** asks bluez to search, pair, disconnect and forget. Disconnecting works;
  the rest is outline. It also logs bluez's connect and
  disconnect events, with the reason, for every Joy-Con the service has seen.
- **`ControllerService`** is the shell's view: `Describe()` (JSON), `Changed`,
  `HomePressed`, and the search and switch-off calls. The shell's `ShellService` listens
  for `HomePressed` and treats it as Home on the remote.

Still outline only: searching, pairing and forgetting in `Bluetooth` (marked `SCAFFOLD`
in the file). The pair has not been tried: only one Joy-Con is paired with the machine.

## Build, deploy, debug

    ./build.sh              # in the toolchain image, output in build/
    ./deploy.sh             # copy to gabecube, restart the service
    ./deploy.sh --log

The system part (rule, unit, keep-list entry) is installed once with sudo by
`system/install.sh`; `deploy.sh` prints the command and says when the installed files
differ from the repository's.

- Logging categories: `tvcontrollers`, `.watcher`, `.joycon`, `.units`, `.service`. Button
  presses are logged at debug level by `tvcontrollers.units`.
- Run locally without a Joy-Con: `QT_FORCE_STDERR_LOGGING=1 build/tvcontrollers` (Qt logs to
  the journal otherwise).
- State from outside: `busctl --user call local.tvcontrollers /local/tvcontrollers
  local.tvcontrollers Describe`.
- "cannot open /dev/hidrawN for a Joy-Con: Permission denied" in the log: the rule or the
  unit's group is missing.

## Next

1. `Bluetooth` search and pairing, and a controllers screen in the shell (pair, order,
   battery, switch off, and why a Joy-Con left: switched off, battery empty, out of reach).
2. A way to end a unit without switching a Joy-Con off.
3. Motion, rumble, then the Ring-Con.
