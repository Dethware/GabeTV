# tvshell

The TV's home screen on the Steam Machine. It runs inside Steam's Gaming Mode, next to
Steam: games still start and run through Steam, and the shell is what you see when no game
is running. Everything works with a TV remote or a controller; no mouse or keyboard needed.
Layouts and looks are placeholders for now.

How it's built: [DEVELOPMENT.md](DEVELOPMENT.md).

## What it does

- **Home**: the first place, with your apps. For now YouTube, in the same TV interface smart
  TVs use, with all its accounts, playing at up to the screen's resolution (4K on a 4K
  screen). Sponsor segments in videos are skipped (SponsorBlock), with
  a short note on screen; Settings chooses what else to skip. And Jellyfin, for your films and
  shows: its own TV interface, playing files directly.
- **Games**: your installed Steam games. OK starts one; when it ends, you're back in the shell.
- **Photos**: photos from an Immich server. "On this day" memories and your albums; an
  album opens a grid, OK opens a photo full screen. Left/right move between photos, with a
  slide or crossfade and the photo's colours or a blurred copy behind it. Each photo shows
  its date and place. Videos play with OK.
- **Screensaver**: after a while without input, photos from the albums you choose, one at a
  time, with their date and place. Left/right browse without leaving it, OK opens the photo,
  any other key ends it. Without albums: a clock.
- **Screen off and sleep**: later still, the screen turns off, and later the machine
  sleeps. Defaults: screensaver after 5 minutes, screen off after 30, sleep after 60.
- **Music in the background**: leave YouTube while music is playing (what YouTube itself
  marks as music) and it plays on; anything else pauses. The top bar then shows the title
  and artist: OK there pauses and resumes, the YouTube button goes back to it. While music
  plays, the screensaver and the screen-off time work as usual, but the screen only goes
  black (the sound stays) and the machine doesn't sleep. Starting a game asks whether to
  pause the music first; a photo's video pauses it. Paused for 10 minutes, it's put away.
- **Volume**: the remote's volume buttons show a small indicator in the top right corner,
  over whatever is on screen, also when the volume is already at either end. Mute mutes and
  unmutes, from anywhere; the indicator turns red and says "Muted". Neither ends the
  screensaver.
- **Any resolution**: the interface keeps its proportions from 1080p to 4K, and photos and
  YouTube are shown at the screen's full resolution. When the resolution changes, the shell
  restarts itself (a couple of seconds).
- **Assistant button**: holding it shows a ring that follows your voice and plays the
  recording back when you let go. It doesn't do anything else yet.

## Controls

| | Remote | Controller |
|---|---|---|
| Move | ring | d-pad, left stick |
| Select | OK | A |
| Back | Back | B |
| Switch place | top bar | LB / RB |
| Settings | top bar | Start (≡) |
| Back to the shell | Home, from anywhere (again: the Home place) | |
| YouTube | YouTube, from anywhere | Home → YouTube |
| Jellyfin | Netflix, from anywhere | Home → Jellyfin |
| Mute | Mute, from anywhere | |
| Screen off / on | Power (short press) | |
| Sleep, power off, controllers off | Power (hold) | |
| Steam's menu, Quick Access | | Steam, … buttons |

The top bar holds the places (Home, Games, Photos) and Settings: left/right move along it,
OK switches place, Down goes in. Back inside a place returns to the top bar.

In YouTube, Back is YouTube's own back. On its "Who's watching?" screen, Back leaves YouTube
for whatever was open before. Home pauses a video (opening YouTube again carries on) and
leaves music playing.

In Jellyfin, the arrows, OK and Back move around; while something plays, Left and Right seek.
On the controller the d-pad moves, A is OK, B is Back, the bumpers seek, X pauses and Y
searches. Home pauses and returns to the shell, and so does Back on Jellyfin's start screen;
the Jellyfin tile (or the remote's Netflix button) brings it back where it was. Left alone for ten minutes it closes; what you were
watching resumes from Jellyfin's "Continue watching". A paused film or an untouched menu gets
the screensaver like the shell does.

A blue dot in the corner of an app's tile means the app is still loaded: opening it is
instant and it is where you left it.

## Setup

**Steam:** turn off Steam's own dimming, screensaver and sleep; the shell handles those.

**Photos:** the shell signs in to Immich with an API key of a dedicated TV account and shows
the albums shared with that account. Put the server and key in `~/.config/tvshell/immich.conf`
(readable only by you, `chmod 600`):

    [immich]
    url=https://photos.example.com
    apiKey=...

The key needs the permissions `album.read`, `memory.read`, `asset.read` and `asset.view`.

**YouTube:** open it and choose "Sign in": it shows a code to enter at youtube.com/activate
on your phone. Add more accounts the same way. Sign-ins are kept.

**Jellyfin:** it is a separate program, Jellyfin Desktop, installed from
`../apps/jellyfin-desktop` (see its README). Signing in needs a keyboard once, in its
desktop mode; that is described there too.

**Remote:** the Chromecast with Google TV remote, paired over Bluetooth. Pair it with
`system/pair-remote.sh` (hold Back + Home on the remote until its light pulses). If pairing
keeps failing, take its batteries out for a few seconds.

## Settings

In the shell: Settings in the top bar, or Start on the controller. Power (when the
screensaver, screen off and sleep start), Screensaver (albums, time per photo, transition,
background, date and place), Photos (transition and background in the viewer) and YouTube
(what SponsorBlock skips: sponsors, self-promotion, reminders, intros, outros, recaps,
non-music parts, filler; only sponsors at first). Changes apply at once.

For the skipping, the shell asks the SponsorBlock service (sponsor.ajay.app) about each video
you watch, by the first characters of a hash of its ID, not the ID itself. With every switch
off, it asks nothing.

They are saved in `~/.config/tvshell/tvshell.conf`:

| Group | Key | Meaning |
|---|---|---|
| `[idle]` | `screensaverAfter`, `screenOffAfter`, `suspendAfter` | seconds, 0 = never |
| `[screensaver]` | `albums` | album IDs |
| | `interval` | seconds per photo |
| | `transition` | `slide`, `fade` or `none` |
| | `background` | `blur`, `gradient` or `black` |
| | `showInfo` | date and place on or off |
| `[viewer]` | `transition`, `background` | as above, for the photo viewer |
| `[youtube]` | `skipCategories` | SponsorBlock categories to skip, e.g. `sponsor, intro` |

## Install

Needs Docker on your computer and SSH access to the Steam Machine as `deck@gabecube`.

    docker build -t tv/toolchains/steamos-3.9:dev ../toolchains/steamos-3.9   # once
    ./build.sh            # build
    ./make-places.py      # the place list for photo captions (once, or to refresh)
    ./deploy.sh           # copy to the Steam Machine and (re)start it
    ./deploy.sh --enable  # start it with Gaming Mode from now on (--disable to undo)
    ./deploy.sh --stop    # stop it; Steam gets the screen back
    ./deploy.sh --log     # follow its log

## Limitations

- Music that YouTube doesn't mark as music pauses like any video when you leave YouTube.
- With the screen black for music, changing the volume turns the picture back on.
- While a game is running, Home, the YouTube and Netflix buttons and the power dialog don't
  bring up the shell. Use the controller's Steam button, or quit the game.
- Jellyfin: text can't be typed with the remote (search, sign-in), and its interface has one
  size, made for a 4K screen.
- "On this day" stays empty: the TV account owns no photos.
- Finer place names ("Blackeberg, Stockholm") only for Sweden and the Czech Republic so far;
  elsewhere the names come from Immich ("Stockholm, Sweden").
- The Assistant button doesn't do anything with your voice yet.
