# tvshell

The TV's home screen. A Qt 6 / QML program that runs inside Steam's Gaming Mode session
as an X11 client on gamescope's `:0`, next to Steam (see `docs/gabecube-findings.md` for why).
Layouts and looks are placeholders; this is the plumbing.

## What it does now

- **Takes the screen** (`src/BaseLayer.*`): tags its window `STEAM_GAME=2113929217`
  (`0x7E000001`) and puts that ID first in gamescope's `GAMESCOPECTRL_BASELAYER_APPID`.
  Steam rewrites that list when a game starts or exits and when Steam restarts; the shell
  re-claims it whenever only Steam's own entries (769, 413091) are left, never while a game's
  ID is in it. On exit it removes its ID again. Does nothing outside gamescope.
- **Input** (`src/GamepadInput.*`): the remote and keyboards arrive as normal key events.
  The controller is read through SDL3 from Steam's virtual Xbox pad and turned into key
  events (A = Return, B/Back = Back, Start = Menu, d-pad/left stick = arrows with repeat),
  only while the shell is on screen and active, since the same pad feeds games.
- **Steam's own screens**: Steam keeps the Steam and … buttons and draws its menu and Quick
  Access in its own window, behind the shell. The shell steps aside (`claim = false`) when
  Steam opens its menu (Steam writes root `GAMESCOPE_BLUR_MODE`) or Quick Access (only visible
  in Steam's `logs/webhelper.txt`, followed by `src/SteamLog.*`). If Steam closes Quick Access
  right after the handoff, the shell re-opens it with Steam's Ctrl+2 shortcut (XTest).
  The remote's **Home** (grabbed on the root window) brings the shell back. There's no
  controller way back for now: picking up the controller suggests playing, and Steam doesn't
  feed its virtual pad while its UI has focus anyway.
- **Idle** (`src/IdleController.*`, replaces Steam's dim/screensaver/sleep, which should be
  off in Steam's settings): screensaver after 5 min, HDMI signal off after 30 min
  (`gamescopectl drm_sleep_external_screen 1`), suspend after 60 min with the screen off.
  Counts only while the shell is on screen. Activity: keys reaching the shell, any controller
  input (also during games), gamescope's `GAMESCOPE_INPUT_COUNTER`, Home/Power, resume from
  suspend. The first press after the screensaver or screen-off only wakes.
  Times in `~/.config/tvshell/tvshell.conf`, `[idle]` group, seconds, 0 = never:
  `screensaverAfter`, `screenOffAfter`, `suspendAfter`. The screensaver is a placeholder
  (moving clock); Immich photos later.
- **Power** (the remote's, remapped to the sleep key): screen off, again → on. The shell holds
  a logind `handle-suspend-key` inhibitor lock so logind doesn't suspend, and grabs the key on
  the root window like Home, so it works during games and in Steam's screens too.
- **Steam games** (`src/SteamLibrary.*`): installed games from Steam's manifests and its
  cached cover art; launching with `steam://rungameid/<id>`.
- **UI** (`qml/`): one place with a grid of games.

## Build and run

Builds in the SteamOS toolchain image (`../toolchains/steamos-3.9`), which has to be built
or loaded first.

    ./build.sh            # → build/tvshell
    ./deploy.sh           # copy to gabecube, install the user unit, restart it
    ./deploy.sh --log     # follow its log
    ./deploy.sh --stop    # stop it; Steam gets the screen back

`deploy/tvshell.service` is a user unit that belongs to the Gaming Mode session
(`PartOf=graphical-session.target`, reads `%t/gamescope-environment`). `deploy.sh` doesn't
enable it, so it runs until stopped or until Gaming Mode restarts.
`systemctl --user enable tvshell` would start it with every Gaming Mode session.

Environment: `TVSHELL_NO_CLAIM=1` runs it without taking the screen.
Logging categories: `tvshell.baselayer`, `tvshell.gamepad`, `tvshell.idle`, `tvshell.steam`,
`tvshell.steamlog`.

Confirmed in use (2026-11-21): launching a game from the shell, quitting it and landing back
in the shell; suspend and resume. Steam recreates its virtual pad after a game and after resume;
the shell picks it up again.

## Next

- Returning after a Steam restart: implemented, not yet seen in use.
- Assistant button (bluez D-Bus, Android TV Voice control channel).
- Immich photos as the screensaver.
- Settings screen for the idle times.
- More places: media apps, photos, emulators.
