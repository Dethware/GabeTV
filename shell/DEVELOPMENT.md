# tvshell: development

How the shell works and how to work on it. What it does for users: [README.md](README.md).
Why things are the way they are, with dates and measurements:
[docs/gabecube-findings.md](../docs/gabecube-findings.md) (the log).

## Overview

A Qt 6.11 / QML program (C++ backend, QML UI, QML module `TvShell`). It runs inside Steam's
Gaming Mode session as an X11 client on gamescope's `:0`, next to Steam, and is started by a
user systemd unit. Only X11 windows can be put on screen in that session; `main.cpp` forces
the `xcb` platform. It uses SteamOS's own Qt (relying on Qt's ABI compatibility) and is
built in a toolchain image with the same package versions (`../toolchains/steamos-3.9`).

Sizes in QML are for a screen 1440 pixels high. `main.cpp` reads the X screen's height
(gamescope's output resolution) before the application object exists and sets
`QT_SCALE_FACTOR` to height / 1440, so everything scales as a whole (1.5 at 4K, 0.75 at
1080p); an explicit `QT_SCALE_FACTOR` wins. The factor can't change in a running program, so
when the screen's size changes the shell exits with code 75 and systemd starts it again
(`Restart=on-failure`). Images from the network aren't scaled by Qt: photo layers and tiles
ask for `sourceSize` times `Screen.devicePixelRatio`. The YouTube page sees 2560x1440 CSS
pixels at that device pixel ratio.

`qml/Main.qml` holds the pages (`page`: `home`, `settings`, `photos`, `viewer`, `youtube`)
and wires the C++ objects together; each page and place is its own QML file.

## Components

### Taking the screen (`src/BaseLayer.*`)

Tags the window `STEAM_GAME=2113929217` (`0x7E000001`) and puts that ID first in
gamescope's root property `GAMESCOPECTRL_BASELAYER_APPID`. Steam rewrites that list when a
game starts or exits and when Steam restarts; the shell re-claims it whenever only Steam's
own entries (769, 413091) are left, never while a game's ID is in it (`gameRunning`). On exit
it removes its ID again. `claim = false` steps aside for Steam. `onScreen` follows
`GAMESCOPE_FOCUSED_APP`. Does nothing outside gamescope (`available`).

Steam keeps the Steam and … buttons and draws its menu and Quick Access in its own window,
behind the shell. The shell steps aside when Steam opens its menu (Steam writes root
`GAMESCOPE_BLUR_MODE`) or Quick Access (only visible in Steam's `logs/webhelper.txt`,
followed by `src/SteamLog.*`). If Steam closes Quick Access right after the handoff, the
shell re-opens it with Steam's Ctrl+2 shortcut (XTest).

### Global keys (`src/BaseLayer.*`, `../system/`)

Home (`XF86HomePage`), Power, YouTube and Mute (`XF86AudioMute`) are passive grabs on the root window, so they
arrive whichever window has focus (Steam's screens included). Several remote keys send evdev
codes above 247, which X11 can't carry; `../system/etc/udev/hwdb.d/90-chromecast-remote.hwdb`
remaps them (OK → `enter`, YouTube → `prog1` = `XF86Launch1`, Power → `sleep` =
`XF86Sleep`), and `../system/etc/atomic-update.conf.d/tv-remote.conf` keeps that file across
SteamOS updates.

The grabs use `owner_events = false`: with `true`, X delivered the release to the shell's
own window and Power looked stuck down. A held key auto-repeats as release/press pairs every
40 ms; a Power release only counts after 80 ms without a new press. All grabs are redone
after a resume (logind `PrepareForSleep`). The shell holds a logind `handle-suspend-key`
inhibitor lock so logind doesn't suspend on Power itself.

### Controller and keys (`src/GamepadInput.*`)

The remote and keyboards arrive as normal key events. The controller is read through SDL3
from Steam's virtual Xbox pad (`SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD=1`, evdev
only, no hidapi) and turned into key events sent to the window: A = Return, B/Back = Back,
Start = Menu, d-pad and left stick = arrows with repeat; LB/RB are a signal
(`shoulderPressed`), not keys. Only while the shell is on screen and active, since the same
pad feeds games. The left stick counts only while the shell is fully awake (`sticks`): a
thumb resting on a Steam Controller trackpad that Steam maps to the stick ended the
screensaver.

### Idle (`src/IdleController.*`)

States Active → Screensaver → ScreenOff, then suspend. Counts only while the shell is on
screen. Activity: keys reaching the shell, controller input (also during games), gamescope's
`GAMESCOPE_INPUT_COUNTER`, the global keys, resume. Its event filter lets Left, Right and
Return through to the screensaver; other keys only wake. Screen off is
`gamescopectl drm_sleep_external_screen 1`. Videos and YouTube keep it awake while playing
(a timer in `Main.qml` reporting activity).

### Screensaver (`qml/Screensaver.qml`, `ScreensaverLayer.qml`, `PhotoBackdrop.qml`, `PhotoInfo.qml`, `src/ScreensaverSettings.*`)

`ImmichClient::loadScreensaver` collects all chosen albums (timeline buckets, images only, no
duplicates) and shuffles them once. That happens when a run ends (and at startup, and when
the albums change), so the next run's order and first photo (preloaded) are ready; a run
starts at opacity 0 and fades in once its first photo is revealed (at most 4 s), so the
screen is never black first.

Two photo layers: the next photo loads into the hidden one and is revealed once loaded
(slide 600 ms, fade 800 ms or at once); the other is then hidden, not just covered (the bars
beside a portrait photo would show it). `index` is the target, `shownIndex` what's on
screen; presses during a transition only move `index`, and the next transition goes
straight there. Backgrounds are a separate `PhotoBackdrop` pair under the layers that
crossfades in place (500 ms): Immich's thumbnail decoded at 96×54 (blur) or 3×2 (gradient),
blurred with `MultiEffect` and darkened. Date and place sit above the layers (z 2) and drift
to a nearby spot every 5 minutes (OLED).

### Photos (`src/ImmichClient.*`, `src/ImmichConfig.*`, `qml/PhotosPlace.qml`, `PhotoGrid.qml`, `PhotoViewer.qml`, `PhotoTile.qml`, `PhotoLayer.qml`)

Albums sorted by name, memories for today, album contents a month bucket at a time
(`/timeline/buckets`, `/timeline/bucket`). Each asset map carries `id`, `ratio`, `isImage`,
`date` (local, from `fileCreatedAt` + `localOffsetHours`) and `place`.

The viewer shows the cached thumbnail at once and loads the preview and full size after
250 ms on a photo; the last image stays up until the next one has something to show.
Once settled, the next three and previous two full-size images are preloaded and kept
referenced (Qt's image cache drops unreferenced large images quickly). Transitions follow
the screensaver's model (`index`/`shownIndex`, one at a time, 250 ms OutCubic); a held key
(auto-repeat) skips them and steps at most ~8 photos/s, and the backdrop follows once it
stops for 0.2 s. Images are requested with Immich's edits (`edited=true`) and EXIF rotation
(`autoTransform`).

Videos play in Qt Multimedia (ffmpeg backend) through `src/ImmichMediaProxy.*`, a loopback
HTTP proxy that adds the API key (Qt's player can't send headers) and passes Range requests
through; a random token in its URLs keeps other local programs out.

`src/NetworkAccess.*` is the shell's network layer, QML images included: it adds the key to
requests for the Immich server only, and caches on disk (`~/.cache/tvshell/network`, up to
2 GiB; asset images are served from the cache when present).

Place names (`src/PlaceLookup.*`): the nearest populated place to the photo's GPS and its
town, from `places.tsv` next to the program, made by `./make-places.py` from GeoNames
(CC BY 4.0). Sweden and the Czech Republic only: each country needs a rule there for which
administrative level is the town. Elsewhere, or without the list, Immich's `city, country`.
Immich's own names are too coarse; see the log, "Place names".

### YouTube (`qml/HomePlace.qml`, `qml/YouTubePage.qml`, `src/WebKeys.*`)

`youtube.com/tv` in a Qt WebEngine `WebEngineView`, created on first open and kept. Its
profile (`storageName` "youtube", forced persistent cookies) lives in
`~/.local/share/tvshell/QtWebEngine/youtube`. The page sends a user agent in the format of YouTube's TV app platform (Cobalt), naming
the device as it is (Valve, SteamMachine): without a TV user agent `/tv` redirects to the
desktop site, and YouTube picks the app's quality tier from it (this one gets
`TV_APP_QUALITY_FULL_ANIMATION`; see the log, "YouTube"). Leaving pauses every `<video>` and then
freezes the page (`lifecycleState` Frozen; a visible page can't be frozen); opening sets it
Active again. `recentlyAudible` keeps the screen awake. YouTube's own Exit calls
`window.close()` (`windowCloseRequested`).

`WebKeys` filters the window's key events while YouTube is shown: Back is taken and reported
(`backPressed`). The account picker has the same address as YouTube's home (`/tv#/`), so
`YouTubePage.checkAccountPicker` asks the page whether a `ytlr-account-selector` element is
visible; if so the shell leaves YouTube, otherwise `WebKeys.sendBack()` hands the page
Escape, YouTube's back. Leaving returns to the page YouTube was opened from, or gives the
screen back to Steam if Steam had it when the YouTube key was pressed.

Resolution (`qml/mediacaps.js`): YouTube's TV player asks `MediaSource.isTypeSupported()`
about width, height, frame rate, bitrate and more, including trick questions (`width=99999`,
`eotf=catavision`) that a TV's browser answers with no. Chromium ignores those parameters
and says yes to all of it, so the player distrusts every answer and stays at 720p. The
script, injected into the page's own world before its scripts, answers them: up to the
screen's real resolution, 60 frames a second, standard dynamic range only. With it the
player offers up to 2160p.

SponsorBlock (`src/SponsorBlock.*`, `src/YouTubeSettings.*`, `qml/sponsorskip.js`): the page's
address carries the video (`#/watch?v=<id>`), which `YouTubePage.videoId` reads. `SponsorBlock`
asks sponsor.ajay.app for the segments by the first four hex characters of the ID's SHA-256
(the service's privacy mode), for the categories switched on in Settings, and keeps the
entries of this video. `sponsorskip.js` is injected into the page as a user script in the
application JavaScript world (the page's own scripts can't see it); the shell hands it the
segments with `runJavaScript` in that world, and it checks the main `<video>` four times a
second, seeks past a segment and shows a note. Its `console.log` lines starting with
`tvshell:` go to the shell's log. Our own script rather than the SponsorBlock browser
extension (Qt 6.10+ can load extensions): that one is written for the desktop site.

### Power (`src/PowerActions.*`, `qml/PowerDialog.qml`)

Power short press toggles the screen; held 0.8 s it opens the dialog (Sleep, Power off, Turn
off controllers, Cancel). "Turn off controllers" sends the Steam Controller "turn off"
feature report (`01 9F 04 "off!"`, 64 bytes) to each HID interface of a Steam Controller
puck (`28de:1304`/`1305`) through hidraw, which `deck` may open alongside Steam, and
disconnects Bluetooth gamepads through bluez (not the remote).

### Assistant (`src/RemoteAssistant.*`, `src/ImaAdpcm.*`, `src/AudioPlayer.*`, `qml/AssistantIndicator.qml`)

The remote's Android TV Voice GATT service over bluez D-Bus (no root). On (re)subscribing it
asks the remote for the hold-to-talk model. While Assistant is held, the audio (IMA ADPCM,
16 kHz) is decoded and its level drives the ring; on release the recording is normalized
(peak to 70%) and played back through vendored miniaudio (`3rdparty/miniaudio/`). Protocol
notes: the log, "Voice protocol details".

### Other

- **Settings** (`qml/SettingsPage.qml`, `SettingRow.qml`, `AlbumPicker.qml`,
  `src/ScreensaverSettings.*`, `src/ViewerSettings.*`): categories on the left, values
  changed with left/right and saved at once through `QSettings` (`tvshell.conf`).
- **Steam games** (`src/SteamLibrary.*`, `qml/GamesPlace.qml`, `GameTile.qml`): installed
  games from Steam's manifests and its cached cover art; launched with
  `steam://rungameid/<id>`.
- **Top bar** (`qml/TopBar.qml`, `BarButton.qml`): one tab per place; the highlight moves
  freely, OK switches the place showing and the focus stays on the bar. Only Down, LB/RB and
  Back move the focus between the bar and a place (the places' `focus` isn't bound to the
  selected tab, which would pull it into the place on every switch).
- **Volume** (`src/SystemVolume.*`, `qml/VolumeIndicator.qml`): Steam handles volume up and
  down; the shell follows the default output's volume and mute through PipeWire's PulseAudio
  interface (libpulse, its own thread) and shows the indicator when they change on the same
  output, or when a volume key press reaches the window (an event filter; so it also shows
  at 0 % and 100 %). Nothing handles Mute, so the shell grabs it globally and toggles the
  output's mute itself. In the screensaver the volume keys pass without ending it
  (`IdleController`).
- **Card grids** (`qml/FitGrid.qml`): as many columns as fit at the design card size, then
  the cards grow so rows end flush with the right margin. Used by every place and the album
  grid.

## Build, deploy, debug

`./build.sh` runs CMake and Ninja in the toolchain image (`tv/toolchains/steamos-3.9:dev`,
`--network none`) and writes `build/tvshell`. `./deploy.sh` copies it (and
`build/places.tsv`) to `deck@gabecube:~/.local/share/tvshell/`, installs
`deploy/tvshell.service` and restarts it.

Lint QML in the toolchain image:

    docker run --rm -v "$PWD:/src:ro" -v "$PWD/build:/out" tv/toolchains/steamos-3.9:dev \
        /usr/lib/qt6/bin/qmllint -I /out /src/qml/<File>.qml

`deploy/tvshell.service` is a user unit that belongs to the Gaming Mode session
(`PartOf=graphical-session.target`, reads `%t/gamescope-environment`). `--enable` links it
into `~/.config/systemd/user/gamescope-session.target.wants/`: the shell then comes up with
Gaming Mode, before Steam has started, and takes the screen when Steam finishes starting.

- `TVSHELL_NO_CLAIM=1` runs it without taking the screen.
- `QTWEBENGINE_REMOTE_DEBUGGING=127.0.0.1:9222` (a unit drop-in) opens Chromium's DevTools
  on gabecube for inspecting YouTube's page; reach it with `ssh -L 9222:127.0.0.1:9222`.
  It exposes the signed-in session: remove it when done.
- A start without a display, to catch QML errors before deploying (runs until the timeout;
  anything QML prints is an error):

      docker run --rm --network none -u "$(id -u):$(id -g)" -e HOME=/tmp \
          -e QT_QPA_PLATFORM=offscreen -v "$PWD/build:/out:ro" \
          tv/toolchains/steamos-3.9:dev timeout 8 /out/tvshell

- A screenshot of what gamescope shows: `gamescopectl screenshot /tmp/x.png` on gabecube.
- Logging categories: `tvshell.baselayer`, `tvshell.gamepad`, `tvshell.idle`,
  `tvshell.power`, `tvshell.assistant`, `tvshell.audio`, `tvshell.immich`,
  `tvshell.mediaproxy`, `tvshell.steam`, `tvshell.steamlog`, `tvshell.volume`,
  `tvshell.sponsorblock`.

## Next

- Hand the Assistant audio to speech recognition.
- Photos: smarter framing for 4:3 and 3:4 on 16:9 (faces? crop/zoom), timeline and people,
  seeking and progress for videos.
- More apps in Home (Jellyfin, music), more places (emulators).
- YouTube: check video decoding (GPU or CPU, 4K), the remote's media keys, memory use.
- Home, the YouTube key and the power dialog over a running game.
