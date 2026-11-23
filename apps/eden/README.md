# Eden for the Steam Machine

[Eden](https://git.eden-emu.dev/eden-emu/eden), the Switch emulator, built for gabecube as a
plain folder: its binary plus the few libraries SteamOS doesn't have. It uses the Qt, SDL
and ffmpeg that SteamOS ships, and nothing is installed system-wide. It is built from source
so that our own changes (`patches/`) can go on top of a pinned version.

It is there for Ring Fit Adventure with the Joy-Cons and the Ring-Con. Its controllers
come from the TV's controller service (`../../controllers`) and from nowhere else: the
Joy-Cons and the Pro Controller, as that service has them. Eden's own controller settings
have no effect, and other controllers (the Steam Controller) don't play in it. How that is
built is in `DEVELOPMENT.md`.

## Build and install

    ./build.sh     # builds build/eden/ (needs Docker and the toolchain image)
    ./deploy.sh    # copies it to ~/apps/eden on gabecube and checks its libraries

`./build.sh` needs the SteamOS toolchain image first (see `toolchains/steamos-3.9`); it builds
its own image on top of that the first time. The first run needs network access twice: to
fetch Eden's source, and while configuring, when Eden's build fetches the source of the
dependencies SteamOS's repositories lack. After that it builds without.

To build another version, remove `build/src` and `build/out` and run
`VERSION=<tag> ./build.sh`. Another machine or folder: `TARGET=user@host FOLDER=some/folder ./deploy.sh`.

## Run

The TV shell starts it from the Ring Fit Adventure tile on Home (as the user service
`eden.service`, which `./deploy.sh` installs). Home on a Joy-Con or the remote parks the
game: it is paused and stays as it is, also through the machine's sleep, until you open the
tile again. It is lost with a reboot; the game's own saves are not.

Hold OK on the tile for its menu: Close ends the game, Force close is for one that doesn't
react.

Form the controller with L on the left Joy-Con and R on the right one together; after the
Joy-Cons have been away (the machine slept), do it again and the game carries on. When the
game asks for a name or a number, Eden shows its own on-screen keyboard; with the Joy-Con in
the Ring-Con its stick moves the cursor as you see it.

## Where its data is

Everything Eden keeps is in `~/apps/eden/user/` on the Steam Machine: settings, keys, the
Switch's system files, saves, shader caches. `./deploy.sh` creates that folder and never
touches what is in it. The game, keys and firmware themselves are in
`~/Games/Emulators/Switch/`.

## Patches

`patches/*.patch` are applied in name order to the fetched source, once. A patch that no
longer applies to a new version stops the build. Our own source files for Eden are in
`driver/`; `./build.sh` places them in Eden's tree.
