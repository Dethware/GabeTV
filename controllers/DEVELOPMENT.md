# TV controller service: development

`tvcontrollers` is the only program that talks to the Joy-Cons and, over Bluetooth, the
Pro Controller (called "a Joy-Con" below where the difference doesn't matter: it speaks
the same protocol and is handled by the same `JoyCon` class, as side `Both`). The decision behind it
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
| Motion for emulators | `MotionServer` | cemuhook (DSU) over UDP, localhost |
| What the shell sees | `ControllerService` | session D-Bus, `local.tvcontrollers` |

Each layer rests on a basic Linux interface (bluez's D-Bus API, udev permissions, hidraw,
uinput), not on anyone's Joy-Con support. Steam's Joy-Con handling, SDL's Joy-Con driver,
`joycond` and the emulators' own drivers are deliberately not used.

## Keeping everyone else out

When a Joy-Con connects, bluez hands it to the kernel as a HID device. On stock SteamOS
the kernel's `hid_nintendo` binds it and makes two input devices next to the hidraw node;
SteamOS opens all hidraw nodes to the logged-in user, and Steam and every SDL program then
talk to the Joy-Con too. Two files stop that:

- **`system/72-tv-joycon.rules`** removes the user-access tag from the hidraw and input
  nodes of a Joy-Con or Pro Controller connected over Bluetooth and gives them to group `input`, which the `deck` user is not in. The service
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
  this also takes it off a Pro Controller on a USB cable, which the service leaves to
  Steam; Steam reads it through hidraw itself.
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
  sideways controller, L on a left one together with R on a right one makes a pair. A Pro
  Controller is a unit of its own as soon as it connects, and again with L+R after its unit
  was ended. A unit gets the lowest free player number, 1 to 4. When a Joy-Con leaves, its unit ends and a
  partner goes back to waiting.
- **`Unit`** is one controller as a player holds it. It shows the player number on the
  lights (one to four lit) and turns its Joy-Cons' state into one `VirtualPad` state. The
  four buttons under the right thumb follow the Switch's rule in every grip: the one on
  the right is A, the lower one B, the upper one X, the left one Y. Upright that is the
  print; sideways the Joy-Con is turned a quarter and the print means nothing (on a right
  Joy-Con the button printed X is then A). The pad's A, B, X and Y are the letters
  programs see: A confirms, B goes back, also in the shell. On an Xbox pad those letters
  sit in other places, so a game's layout by position comes out mirrored, and an
  emulator's own mapping has to assign the four by letter. A sideways Joy-Con's stick is
  rotated with the grip (the tables and the two sign pairs are in `Unit.cpp`).
  A pair whose right Joy-Con sits in a Ring-Con is
  held differently again: that Joy-Con is the one in the hands, a quarter turned like a
  sideways one (its top to the player's right), so it steers with its stick as the player sees it,
  on the pad's left stick, and its four buttons follow the same rule (`RingMap`); the left
  one is strapped to a leg and keeps its buttons. Whether it sits in a ring is
  `JoyCon::inRing`: what the rail said last, asked while the ring is read, and once by
  `probeRing()` when the pair is formed. Nothing in the ordinary report says when the
  Joy-Con is slid out again, so while it sits in a ring and is in use the unit keeps the
  ring read (`followRing`); after two minutes without input from that Joy-Con the
  reading stops, and its next input asks the rail again. Nothing says
  either when a Joy-Con is slid into a ring later; what changes is how it is held, on its
  side. So when a pair's right Joy-Con has had gravity along its width for a second, its
  rail is asked, at most every six seconds and only while it is in use.
  While any of its Joy-Cons is quiet the pad is neutral, so
  nothing stays held. A press belongs to whoever had the controller when it went down:
  what is held when the pad gets the controller (the unit is formed with L+R, or a game
  lets go of it) stays released on the pad until it is let go, and a pushed stick stays
  centred until it has been centred. Our Eden does the same in the other direction. Home is not mapped; it becomes `homePressed()`.
  Two units can trade player numbers (`tradePlayerWith`): the virtual controllers go
  with the numbers, not with the Joy-Cons, because programs take controllers in the order
  they were made. So the controller that was made first stays player 1's, whoever holds
  that number.
- **`VirtualPad`** is the uinput controller games see. It presents itself as an Xbox 360
  pad (USB id 045e:028e, the kernel `xpad` driver's buttons and axes), the layout every
  program knows without a mapping. It declares the guide button, because leaving it out
  would renumber the stick buttons for programs that count them, and never presses it.
  Steam wraps it like any controller, and the shell reads Steam's pad.
- **Motion.** `JoyCon` reads the sensor's calibration from the controller's memory (factory
  at `0x6020`, the user's at `0x8026` if marked valid), switches the sensor on (subcommand
  `0x40`), and turns the three readings of each full report into `Motion` values:
  acceleration in g (reading × 4 / (sensitivity − origin)) and rotation in degrees a
  second ((reading − origin) × 936 / (sensitivity − origin)). The axes are the same for
  every controller held upright: x to its top, y to its left, z out of its face; a right
  Joy-Con's y and z are turned, because its sensor is mounted the other way round.
- **`MotionCalibration`** is the service's own motion calibration, like a Switch's
  "calibrate motion controls". The calibration stored in a controller leaves the strength
  at rest up to 7 % off 1 g (measured: 1.07, 1.03 and 1.02 on three controllers; Eden and
  the kernel driver convert the same way and would show the same), and the gyroscope
  shows a few tenths of a degree a second at rest. `calibrate(address)` measures a
  controller for two seconds as it lies still (it refuses if the readings varied by more
  than 4°/s or 0.05 g, or make no sense) and keeps what the sensors showed, by Bluetooth
  address, in `~/.config/tvcontrollers/motion.ini`. `JoyCon` applies it to every reading
  (`RestCorrection`): acceleration divided by the strength at rest, the gyroscope's
  reading at rest taken off. The gyroscope's part then follows the error as it creeps
  (`JoyCon::trackRest`): after a full second at rest (no axis varying by more than
  0.6°/s, the strength by more than 0.01 g, the mean under 1°/s; lying still it measures
  0.3°/s and 0.003 g at most) the correction moves a fifth of the way to what the
  gyroscope shows. A hand shakes far more than that, so it isn't followed while held; a
  turn slower than 1°/s by something steadier than a hand is swallowed. In memory only;
  the stored values change only by calibrating. So everything downstream, `MotionServer` and with it the
  emulator, gets the corrected motion. One posture gives a scale for the accelerometer,
  not an offset per axis.
- **`MotionServer`** serves that motion as a cemuhook (DSU) server on localhost port
  26760, the way Eden and other emulators take motion from outside
  (https://v1993.github.io/cemuhook-protocol/). Each Joy-Con is a pad of its own, since a
  pair has two sensors: pads 0 and 1 are player 1's left (or only) and right, pads 2 and 3
  player 2's; players 3 and 4 aren't served yet. Every reading carries its own time
  (`JoyCon::Motion::time`), and that is the packet's timestamp, so a client can add up
  rotation exactly: 5 ms after the reading before, plus 15 ms for every report lost in
  between. Lost reports are told by the report's own timer (its second byte), which
  counts one per reading, so 3 from one report to the next and 6 across a lost one; when
  a report arrives says little, the radio delivers them unevenly. Their readings are
  filled in (up to 8 reports in a row) on a straight line between the readings either
  side of the gap and sent first, so the stream is complete; `lostReports` in `Describe`
  says how many. The readings go out
  from a queue, one every 5 ms and in order, two at a time when a burst from the radio
  has built up a backlog: sent as they arrive they would come in bursts, which matters to
  a client that takes the time between packets as the time between readings (Eden), and
  sent by a timer each they arrived out of order. Only motion, battery and identity are filled in; buttons and sticks
  go through the virtual controller. The protocol's reference doesn't say which way its
  axes point, so the mapping in `sendMotion` is set by Eden: its cemuhook client must end
  up with what its own Joy-Con driver gives for the same movement (the comment there has
  both). A controller lying face up shows -1 g on the protocol's y. A client that stops asking is dropped
  after five seconds.
- **`EmulatorServer`** is the service's second face, for an emulator of the Switch: a Unix
  socket (`SOCK_SEQPACKET`, `$XDG_RUNTIME_DIR/tvcontrollers/emulator`) speaking
  `protocol/emulator.h`, which is the whole contract and is also built into our Eden
  (`../apps/eden`). Where a virtual pad is one combined controller for ordinary programs,
  an emulator gets every physical controller by itself: on connecting, and whenever it
  changes, the list of controllers (side, how it is held, whose it is, its partner,
  battery, ring), and for every report an `Input` with buttons, stick, the report's three
  motion readings with their times and the ring's raw flex. It asks for the ring with
  `SetRing` and says with `Playing` when a game is being played: the units' virtual pads
  are then held neutral (`UnitManager::setPadsHeld`), so that the controllers are the
  game's alone, while Home still reaches the shell. Lights and rumble are in the protocol
  and not acted on yet. A program that
  disconnects is taken out of the list before anything it asked for is given up: giving
  up a ring announces a change to every client, and that must not reach the one being
  dropped.
- **The Ring-Con** hangs on a right Joy-Con's rail and is read through the Joy-Con's
  second processor, which also drives the IR camera and NFC. `JoyCon::setRingWanted(who, true)`
  (the shell's test and an emulator may each want it; it is read while either does)
  switches that processor on and puts it in standby, then asks the rail every second what
  is attached (`lookForRing`); when it answers with the ring's id, the format block and
  the polling command go out (the bytes are in `JoyConProtocol.h`) and `ringChanged()`
  says there is one. From then on the Joy-Con puts the rail's data where the third
  motion reading's accelerometer was: a zero, the flex, the ring's id; all zeros once the
  ring is taken off. The gyroscope half stays its own. `handleReport` knows such a
  reading by that pattern, gives it the second reading's acceleration, emits the flex
  (`ringFlex`, 66 a second) and takes the ring as gone when the id was missing for ten
  reports. The flex is passed on as it comes, about 3700 at rest and 1500 to 6200 at the
  ends: the game calibrates it for the player. The second processor costs battery, so it
  runs only while the ring is wanted; when nobody wants it any more it is switched off.
- **Stick calibration** comes from the Joy-Con's memory (subcommand `0x10`): the factory
  values, replaced by the user's own if those are marked valid. An answer to a memory
  read only counts if it repeats the address and length asked for: a slow Joy-Con
  answers a read after the next one has gone out. `JoyCon::stick()` gives
  -1 to 1 with a 15 % dead zone.
- **`Bluetooth`** is the bluez side, on the system D-Bus. A search sets the discovery
  filter to classic Bluetooth and starts discovery; every device object that appears
  (`InterfacesAdded`), gets its name late (`PropertiesChanged`) or is already known
  (`GetManagedObjects`) is considered, and one named "Joy-Con…" that isn't paired is
  paired, trusted and connected. A search ends by itself after two minutes. `PairingAgent`
  is the service's own bluez agent (`NoInputNoOutput`): a Joy-Con pairs without a code, and
  the agent agrees only for a device the service is pairing. `Bluetooth` also disconnects
  and forgets a device, and logs bluez's connect and disconnect events, with the reason,
  for every Joy-Con the service has seen.
- **`ControllerService`** is the shell's view: `Describe()` (JSON, units in player order),
  `Changed`, `HomePressed`, the search and switch-off calls, `EndUnit`, `SwapPlayers`,
  `CalibrateMotion` with its answer `MotionCalibrated`, and `WatchRing` with the signal
  `RingFlex`. `WatchRing(address, true)` holds for five seconds, so whoever watches a
  ring keeps asking and a watcher that dies doesn't leave the Joy-Con's second processor
  running. The shell's `ShellService` listens for `HomePressed` and treats it as Home on
  the remote.

Not tried yet: forgetting a Joy-Con.

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

1. In the shell's Controllers settings (`ControllersPane`): forgetting a controller, and
   why one left (switched off, battery empty, out of reach).
2. For the emulator (`EmulatorServer`): lights and rumble as the game sets them, then the
   IR camera. Units that survive a restart of the service.
3. An idea, not decided: the Ring-Con as the way to steer outside games too (squeeze to
   confirm, pull to cancel, turn it left or right, point it up or down), as one more
   mapping onto the virtual pad. It needs the ring read the whole time, which keeps the
   Joy-Con's second processor on.
4. Lost reports, later and not urgent: their readings are filled in on a straight line
   once the next report is there. Worth reading up on what is done elsewhere (loss
   concealment for inertial sensors) and trying prediction, so that a reader gets
   something sensible during the gap and not only after it.
5. Motion pads for players 3 and 4 (a second server port).
6. The Pro Controller on a USB cable.
