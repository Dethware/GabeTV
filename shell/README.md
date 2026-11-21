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
  events (A = Return, B/Back = Back, Start = Menu, d-pad/left stick = arrows with repeat; LB/RB as
  their own signal),
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
  `screensaverAfter`, `screenOffAfter`, `suspendAfter`.
  The left stick counts only while the shell is fully awake: a thumb resting on a Steam
  Controller trackpad that Steam maps to the stick moved it and ended the screensaver.
- **Screensaver** (`qml/Screensaver.qml`, `ScreensaverLayer.qml`, `ScreensaverBackdrop.qml`,
  `src/ScreensaverSettings.*`): photos from the Immich albums chosen in Settings, in an order
  shuffled once each time it starts (`ImmichClient::loadScreensaver`: all chosen albums,
  images only, no duplicates), one at a time on a timer. Left/right browse without leaving
  it, OK opens the photo in the viewer (in the screensaver's order), other keys end it.
  Transitions: slide (in the direction of travel), crossfade or none; one at a time, presses
  during a slide only move the target, and the next slide goes straight to it. A photo
  loads into the hidden layer and is revealed once loaded. Beside the photo: the photo
  blurred, its colours as a gradient (the thumbnail decoded at 3×2 pixels, then blurred), or
  black; the background crossfades in place under the sliding photos. Date and place at the
  bottom left, outlined, moving to a nearby spot every 5 minutes (OLED). No albums: a clock
  moving every minute. Settings in `tvshell.conf`, `[screensaver]`: `albums`, `interval`
  (s), `transition`, `background`, `showInfo`.
- **Power** (the remote's, remapped to the sleep key): a short press turns the screen off,
  again → on; held for 0.8 s it opens the power dialog (Sleep, Power off, Turn off controllers,
  Cancel; starts on Cancel), not yet over a running game. The shell holds a logind
  `handle-suspend-key` inhibitor lock so logind doesn't suspend, and grabs the key on the root
  window like Home, so it works in Steam's screens and games too. The grabs use
  `owner_events = false`: with `true`, X delivered the release to the shell's own window and
  Power looked stuck down. A held key auto-repeats as release/press pairs every 40 ms; a
  release only counts after 80 ms without a new press. Both grabs are redone after a resume.
  "Turn off controllers" (`src/PowerActions.*`) sends the Steam Controller "turn off" feature
  report (`01 9F 04 "off!"`, 64 bytes) to each HID interface of a Steam Controller puck
  (`28de:1304`/`1305`) through hidraw, which `deck` may open alongside Steam; and disconnects
  Bluetooth gamepads through bluez (not the remote).
- **Assistant** (`src/RemoteAssistant.*`, `qml/AssistantIndicator.qml`): the remote's
  Assistant button and microphone through its Android TV Voice service over bluez D-Bus (no
  root). On (re)subscribing it asks the remote for the hold-to-talk model. While Assistant is
  held, the audio is decoded (`src/ImaAdpcm.*`, IMA ADPCM 16 kHz) and a ring at the bottom
  follows its level; on release the recording is normalized and played back
  (`src/AudioPlayer.*`, vendored miniaudio in `3rdparty/miniaudio/`). Not connected to anything
  else yet (later: speech recognition / a local model on mikoshi). Protocol notes in
  `docs/gabecube-findings.md`.
- **Settings** (`qml/SettingsPage.qml`): from the top bar's Settings entry (up from the games)
  or the controller's Start/≡. Categories on the left (Power: the three idle times;
  Screensaver: albums, time per photo, transition, background, date and place); Right/OK goes
  into a category, left/right changes a value (saved and applied at once), OK on Albums opens
  a list of albums with checkmarks. Back goes out a level, then leaves.
- **Photos** (`src/ImmichClient.*`, `qml/PhotosPlace.qml`, `PhotoGrid.qml`, `PhotoViewer.qml`):
  a place next to Games (top bar tabs). Today's memories ("On this day") and the albums
  (by name) from Immich; an album or memory opens a photo grid, OK opens a full-screen viewer
  (whole photo, black bars; preview first, then the full-size rendering), left/right move,
  Back goes back. The viewer shows the cached thumbnail at once and loads the preview and
  full-size image after 250 ms on a photo; the last photo stays up until the next one shows
  (no black flashes); a held key steps at most ~8 photos/s. Once settled, the next three and
  previous two full-size images are preloaded and kept, so stepping to them is instant. Grids show whole photos (portraits upright); album covers are cropped.
  Images are requested with Immich's edits (`edited=true`) and EXIF rotation applied
  (`autoTransform`). Videos: OK plays/pauses, Back stops, left/right step on; the screen stays
  awake while one plays. Qt Multimedia (ffmpeg backend) plays them through
  `src/ImmichMediaProxy.*`, a loopback HTTP proxy that adds the API key (Qt's player can't
  send headers) and passes Range requests through; a random token in its URLs keeps other
  local programs out.
  It runs as a dedicated TV account on the Immich server, which sees the albums shared with
  it; that account owns no photos, so memories stay empty for now (the row hides itself).
  Server and key in `~/.config/tvshell/immich.conf` (mode 600; `[immich]` `url=`, `apiKey=`).
  A read-only key needs `album.read`, `memory.read`, `asset.read`, `asset.view` (and
  `person.read` for people later). `src/NetworkAccess.*` is the shell's network layer (also
  for QML images): it adds the key to requests for the Immich server only, and caches on disk
  (`~/.cache/tvshell/network`, up to 2 GiB; asset images are served from the cache when present).
- **Steam games** (`src/SteamLibrary.*`): installed games from Steam's manifests and its
  cached cover art; launching with `steam://rungameid/<id>`.
- **UI** (`qml/`): a top bar with the places as tabs (Games, Photos) and Settings. Left/right
  only move the highlight (white); OK switches to the highlighted place (blue underline = the
  place showing); the highlight stays on the tab, Down goes into the place. The controller's LB/RB switch places directly (a GamepadInput
  signal, not a key event). Back inside a place returns to the top bar. Placeholder looks.

## Build and run

Builds in the SteamOS toolchain image (`../toolchains/steamos-3.9`), which has to be built
or loaded first.

    ./build.sh            # → build/tvshell
    ./deploy.sh           # copy to gabecube, install the user unit, restart it
    ./deploy.sh --log     # follow its log
    ./deploy.sh --stop    # stop it; Steam gets the screen back
    ./deploy.sh --enable  # start it with every Gaming Mode session (--disable to undo)

`deploy/tvshell.service` is a user unit that belongs to the Gaming Mode session
(`PartOf=graphical-session.target`, reads `%t/gamescope-environment`). Enabled on gabecube
since 2026-11-21 (link in `~/.config/systemd/user/gamescope-session.target.wants/`), so the
shell is the default interface: it comes up with Gaming Mode, before Steam has started, and
takes the screen back when Steam finishes starting.

Environment: `TVSHELL_NO_CLAIM=1` runs it without taking the screen.
Logging categories: `tvshell.baselayer`, `tvshell.gamepad`, `tvshell.idle`, `tvshell.power`, `tvshell.assistant`, `tvshell.audio`, `tvshell.immich`, `tvshell.mediaproxy`, `tvshell.steam`,
`tvshell.steamlog`.

Confirmed in use (2026-11-21): launching a game from the shell, quitting it and landing back
in the shell; suspend and resume; a full Gaming Mode restart (Steam restarting included). Steam recreates its virtual pad after a game and after resume;
the shell picks it up again.

## Next

- Hand the Assistant audio to speech recognition.
- Photos: smarter framing for 4:3 and 3:4 on 16:9 (faces? crop/zoom), timeline and people, seeking/progress for videos.
- Screensaver: check whether Immich's place names are accurate (the Android app's were off).
- More places: media apps, photos, emulators.
