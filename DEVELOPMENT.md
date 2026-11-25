# GabeTV: development

How the parts of this project fit together, the decisions they rest on, and what is planned.
What it is for users: [README.md](README.md). Each part has its own `DEVELOPMENT.md` for how
it works inside. Findings, measurements and rejected alternatives, with their dates, are in
the log: [docs/gabecube-findings.md](docs/gabecube-findings.md).

## The parts

The Steam Machine (`gabecube`) runs stock SteamOS in Gaming Mode: Steam's session, drawn by
gamescope. Everything of ours runs inside or next to that session.

| Part | What it is | Runs as | Its notes |
|---|---|---|---|
| `shell/` | The TV's home screen: places with apps, games and photos, the screensaver, idle and power | An X11 window in the Gaming Mode session, a user service | [shell/DEVELOPMENT.md](shell/DEVELOPMENT.md) |
| `controllers/` | The only program that talks to the Joy-Cons and the Pro Controller | A system service running as the user | [controllers/DEVELOPMENT.md](controllers/DEVELOPMENT.md) |
| `apps/jellyfin-desktop/` | Jellyfin's player, patched to behave on a TV | A user service the shell starts and shows | [apps/jellyfin-desktop/README.md](apps/jellyfin-desktop/README.md) |
| `apps/eden/` | The Eden emulator with our controller source and patches | A user service the shell starts and shows | [apps/eden/DEVELOPMENT.md](apps/eden/DEVELOPMENT.md) |
| `system/` | The remote's key map and pairing script, a watchdog for Steam's interface | Files in `/etc`, a user service | [system/steam-ui-watchdog/README.md](system/steam-ui-watchdog/README.md) |
| `toolchains/` | Container images with SteamOS's own package versions, to build all of the above | On the development machine | [toolchains/README.md](toolchains/README.md) |

How they meet:

- **The screen.** The shell tags its window as a "game" for gamescope and puts itself first
  in gamescope's list of what to show; Steam keeps running behind it for games, downloads
  and its own menus. A program the shell starts itself (Jellyfin, Eden) is tagged the same
  way and shown in place of the shell; Home brings the shell back.
- **Input.** The remote's keys and Steam's virtual gamepads reach the shell. The Joy-Cons
  and the Pro Controller reach nothing directly: the controller service owns them and makes
  virtual controllers for Steam and ordinary programs, and gives an emulator of the Switch
  each physical controller by itself over a socket.
- **Games.** Steam games start through Steam and take the screen from the shell; Switch
  games run in Eden as one of the shell's own programs.

## Constraints

- **Stock SteamOS.** It gets new hardware support first (HDMI 2.1, drivers). Its root is
  read-only with A/B updates, so everything of ours lives in `/home`, in user systemd units,
  or in `/etc` with an entry in the update keep-list. A fully custom base may be looked at
  again later.
- **Programs are plain folders under `/home`**, built against the libraries SteamOS ships,
  with the few it lacks next to them. No Flatpak or similar.
- **SteamOS can't build anything** (no compiler, stripped headers): everything is built in
  the toolchain images and copied over.
- **Qt is SteamOS's own**, the version it ships (6.11.1 with SteamOS 3.9), relying on Qt's
  ABI compatibility. The shell is Qt/QML with a C++ backend, no web stack for its own
  interface.

## Decisions

1. **Our own window inside the stock Gaming Mode session**, not a session of our own and not
   a non-Steam shortcut. Steam stays in charge of games, Steam Input and its menus.
2. **"Places", not shelves.** The interface is a spatial library with stable positions that
   never reorder by themselves, and it unifies every source: Steam, emulators, media apps,
   photos. All of it is usable with a d-pad or a TV remote; no mouse or keyboard needed.
3. **The shell owns idle behaviour**: screensaver (photos), screen off, sleep. No Steam
   startup movies or Steam screensaver.
4. **The device you pick up suggests what you want to do.** Not a rule, but it guides the
   design: the remote is for the TV (its Home brings back the shell); the Steam Controller
   is for Steam games (its Steam buttons open Steam's own screens); Nintendo controllers are
   for Nintendo games; a Joy-Con in the Ring-Con is for Ring Fit Adventure. The shell may
   later react to which device wakes up or connects.
5. **We override everything about the Nintendo controllers.** One service of ours does what
   a Switch does with them: pairing, forming two into one, player lights, battery, the
   Ring-Con, Home. Nothing is built on Steam's Joy-Con support, `joycond`, SDL's Joy-Con
   driver or an emulator's own: each does it its own way, and glue between them breaks with
   a random update. An emulator may be patched to fit, and ours is.
6. **A parked game stays** until it is closed; the TV asks before another game is started
   next to one. Joy-Cons that come back (after sleep) need L+R again, as on a Switch.

## Hardware

- **Steam Machine**, hostname `gabecube`, reachable with `ssh deck@gabecube`. HDMI 2.1 with
  VRR, its output capped at 4K120 until a SteamOS update that Valve has announced. Its
  HDMI-CEC is unreliable (spurious wake-ups), so nothing depends on it.
- **Display.** For now, and only for now, an older 40" 4K monitor (60 Hz) on DisplayPort.
  The target is an LG OLED G6 55" (`OLED55G64LW`), wall-mounted and kept offline, possibly
  with its radio module removed, which leaves an IR-only remote; it should boot to its last
  HDMI input. Anything about HDMI, refresh rates, VRR, HDR or CEC waits for that TV. On an
  OLED nothing static may stay on screen for hours.
- **Sound.** A Philips HTL3310 soundbar. For now over Bluetooth: it goes to sleep when
  idle and doesn't wake with the machine by itself. In the target setup it is on the TV's
  eARC port.
- **Input.** The Steam Controller; the Google TV remote, paired to the Steam Machine over
  Bluetooth for navigation while its IR side controls the TV directly (power, volume); two
  pairs of Joy-Cons, a Pro Controller and a Ring-Con with its leg strap. Keyboard and mouse
  are optional. A button on a paired Joy-Con or Pro Controller wakes the machine from
  sleep. The remote can wake it only if it let go of its connection by itself (about 30
  minutes after its last use) before the machine went to sleep; then merely moving it
  wakes the machine. So the shell sleeps only after the remote has let go, and keeps the
  screen off after a wake by the remote until a key is pressed (`shell/DEVELOPMENT.md`,
  Idle; log, "Two ways of going to sleep").
- The Steam Machine is the only source: no HDMI switch, no streaming dongle, no Switch.

## Apps

| Need | How | State |
|---|---|---|
| YouTube | The TV interface smart TVs use (`youtube.com/tv`) in the shell's own browser view | In the shell |
| Jellyfin | Jellyfin Desktop in its TV layout, built from source with patches | In the shell |
| Photos | Built into the shell with the Immich API: albums, memories, viewer, screensaver. It uses a dedicated TV account that sees the albums shared with it and owns no photos | In the shell |
| Steam games | Steam's library, started through Steam | In the shell |
| Ring Fit Adventure, other Switch games | Eden, built from source, with the real Joy-Cons and Ring-Con through the controller service | Ring Fit plays; a library of games is next |
| Music (Symfonium) | Android only, so Waydroid | Not started |

Waydroid, if it comes to that: it needs binder in the kernel, and the install scripts for
SteamOS that exist modify the read-only root and break on updates. It is regular Android,
not Android TV, so the TV versions of apps may not appear or behave. Symfonium is a paid app
with Google Play licensing, so it also needs Google's services and a registered device.

## Next

Larger pieces, roughly in order. Each part's own `DEVELOPMENT.md` has its smaller ones.

1. A pass over the controller service, the Eden source and the shell for refactoring and
   architecture. For the shell the direction is set: its source by topic and not by kind
   of file, YouTube as a program of its own, and in time one description per program in
   place of code that names each (`shell/DEVELOPMENT.md`, Next).
2. Rumble in Eden, then the Joy-Con's IR camera.
3. A library of Switch games in the shell's Games place, with each game's own art.
4. User profiles: a system of its own, to be built from the ground up. They apply to
   photos (each person's own Immich photos and memories), YouTube, Eden (its user and
   saves) and probably some of the settings.

## Open questions

- How user profiles work.
- Which server music comes from (Navidrome, Jellyfin, other).
- Turning the TV off without the Steam Machine's CEC.

## Ideas, not now

- Moving playback between devices in the way of Spotify Connect, on an open stack, with
  desktop, Android and TV clients. Prior art: Music Assistant.
- Receiving from phones with an open protocol. Wanted: an FCast receiver
  (https://fcast.org/). Other candidates: YouTube's "link with TV code" and DIAL, AirPlay
  (UxPlay), DLNA/UPnP rendering, Spotify Connect (librespot). Google Cast needs device keys
  and is out.
- The Ring-Con as a way to steer outside games: squeeze to confirm, pull to cancel, turn and
  point to move.
