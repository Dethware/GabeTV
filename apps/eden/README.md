# Eden for the Steam Machine

[Eden](https://git.eden-emu.dev/eden-emu/eden), the Switch emulator, built for gabecube as a
plain folder: its binary plus the few libraries SteamOS doesn't have. It uses the Qt, SDL
and ffmpeg that SteamOS ships, and nothing is installed system-wide. It is built from source
so that our own changes (`patches/`) can go on top of a pinned version.

It is there for Ring Fit Adventure with the Joy-Cons and the Ring-Con, which the controller
service (`../../controllers`) owns.

## Build and install

    ./build.sh     # builds build/eden/ (needs Docker and the toolchain image)
    ./deploy.sh    # copies it to ~/apps/eden on gabecube and checks its libraries

`./build.sh` needs the SteamOS toolchain image first (see `toolchains/steamos-3.9`); it builds
its own image on top of that the first time. The first run needs network access twice: to
fetch Eden's source, and while configuring, when Eden's build fetches the source of the
dependencies SteamOS's repositories lack. After that it builds without.

To build another version, remove `build/src` and `build/out` and run
`VERSION=<tag> ./build.sh`. Another machine or folder: `TARGET=user@host FOLDER=some/folder ./deploy.sh`.

## Where its data is

Everything Eden keeps is in `~/apps/eden/user/` on the Steam Machine: settings, keys, the
Switch's system files, saves, shader caches. `./deploy.sh` creates that folder and never
touches what is in it. The game, keys and firmware themselves are in
`~/Games/Emulators/Switch/`.

## Patches

`patches/*.patch` are applied in name order to the fetched source, once. A patch that no
longer applies to a new version stops the build.
