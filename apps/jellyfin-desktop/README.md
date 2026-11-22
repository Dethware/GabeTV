# Jellyfin Desktop for the Steam Machine

[Jellyfin Desktop](https://github.com/jellyfin/jellyfin-desktop) built for gabecube as a plain
folder: the player's binary plus mpv and the few libraries SteamOS doesn't have. It uses the
Qt that SteamOS ships, so there is no second copy of Qt and nothing is installed system-wide.

## Build and install

    ./build.sh     # builds build/jellyfin-desktop/ (needs Docker and the toolchain image)
    ./deploy.sh    # copies it to ~/apps/jellyfin-desktop on gabecube and checks its libraries

`./build.sh` needs the SteamOS toolchain image first (see `toolchains/steamos-3.9`); it builds
its own image on top of that the first time.

To build another version of the player, remove `build/src` and run
`VERSION=<tag> ./build.sh`. Another machine or folder: `TARGET=user@host FOLDER=some/folder ./deploy.sh`.

## Run

The TV shell starts it from its Jellyfin tile, as the user service `jellyfin-desktop.service`
that `./deploy.sh` installs. By hand, inside the Gaming Mode session:

    systemctl --user start jellyfin-desktop

The service runs it like this:

    SDL_JOYSTICK_HIDAPI=0 SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD=1 \
        ~/apps/jellyfin-desktop/jellyfin-desktop --tv --fullscreen --scale-factor 2 --log-level error

- `--tv` is the layout for a remote or controller. In it every key is a remote button, so
  text can't be typed properly: **sign in once with `--desktop` instead of `--tv`** and a
  keyboard (stop the service, run the line above with `--desktop`, sign in, quit). The
  sign-in is kept.
- `--scale-factor 2` makes it readable on a 4K screen.
- `--log-level error`: at the more detailed levels the log contains the stored sign-in, and
  a level given once is remembered in the profile.
- The two `SDL_` settings make it use Steam's virtual gamepad, like the shell does. With
  them the Steam Controller works through `inputmaps/steam-virtual-gamepad.json`, which
  `./deploy.sh` copies into the player's profile: d-pad to move, A is OK, B is Back (hold:
  home), X and Menu play or pause, Y searches, the bumpers seek. The sticks do nothing.
- With the remote: arrows, OK and Back; Left and Right seek while something plays.

`--help` lists the other options. Settings,
sign-in and logs are kept per profile in `~/.local/share/jellyfin-desktop/profiles/`, the
cache in `~/.cache/jellyfin-desktop/`; an `mpv.conf` next to a profile's
`jellyfin-desktop.conf` adjusts playback.

To remove it, delete the folder (and those two for its data).

## What is in the folder

| | |
|---|---|
| `jellyfin-desktop` | the player; looks for libraries in `lib/` first |
| `lib/` | `libmpv` and what it needs that SteamOS lacks, from SteamOS's own packages (listed in `lib/PACKAGES`) |
| `VERSION` | the player's version |

After a SteamOS update that changes Qt or the libraries mpv uses, rebuild the toolchain image
and run both scripts again.
