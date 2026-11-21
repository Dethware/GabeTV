# steamos-3.9

Toolchain matching gabecube's SteamOS 3.9.5 (build 20261023.100): glibc 2.43, GCC 16.1.1,
Qt 6.11.1 (base, declarative, multimedia with the ffmpeg backend, shadertools, tools), SDL3 3.4.12, CMake 4.4, Ninja.

SteamOS itself can't build anything: no compiler, no CMake, and `/usr/include` is stripped.

## How it's made

- Base: `archlinux:base`, pinned by digest (only used for its pacman; every package is then
  replaced by SteamOS's version).
- `pacman.conf`: the SteamOS 3.9 repositories, as in gabecube's `/etc/pacman.conf`
  (`jupiter-3.9`, `holo-3.9`, `core-3.9`, `extra-3.9`, from
  `steamdeck-packages.steamos.cloud/archlinux-mirror`).
- `keyrings/`: Valve's public package-signing keys (`holo.gpg`, `holo-trusted`,
  `holo-revoked`), copied from gabecube's `/usr/share/pacman/keyrings/` (package
  `holo-keyring 20250801-2`). Packages are signature-checked against them.
- `pacman -Syuu` moves the base to SteamOS versions, then the toolchain is installed.
  The full package list is in the image at `/etc/toolchain-packages.txt`.

Build:

    docker build -t tv/toolchains/steamos-3.9:dev toolchains/steamos-3.9

## Verifying

`./verify.sh` compares every package present both in the image and on gabecube; all
versions must match.

## Preserving

Valve's 3.9 repositories keep receiving updates, so rebuilding later gives a newer
toolchain. The built image is kept in `../archive/steamos-3.9.tar.zst` (the latest row below).

| Built | Image ID | Verified |
|-------|----------|----------|
| 2026-11-21 | `sha256:7cbeb46d87c3108aa06aa38d6fb15f7d2ad7f6c34fc266bd2ea608363a28442d` | `verify.sh`: 221 shared packages, all versions equal to gabecube (3.9.5, 20261023.100). A Qt Quick test program built in it (`--network none`, as the user) needs only `GLIBC_2.34` and `Qt_6.11`, and ran on gabecube under gamescope (X11, shown via the base layer). |
| 2026-11-21, evening | `sha256:0be5c6bb862126e566599763a30b74d9e31145696821f71fda03dbca0a992b8f` | Added `qt6-multimedia` (video). `verify.sh`: 343 shared packages, all versions equal to gabecube (3.9.5, 20261023.100). This is the archived one. |
