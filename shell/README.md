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
Logging categories: `tvshell.baselayer`, `tvshell.gamepad`, `tvshell.steam`.

Confirmed in use (2026-11-21): launching a game from the shell, quitting it and landing back
in the shell; suspend and resume. Steam recreates its virtual pad after a game and after resume;
the shell picks it up again.

## Next

- Returning after a Steam restart: implemented, not yet seen in use.
- Assistant button (bluez D-Bus, Android TV Voice control channel).
- Idle chain: screensaver (Immich), dim, screen off (`drm_sleep_external_screen`); Power
  button via a logind `handle-suspend-key` inhibitor.
- More places: media apps, photos, emulators.
