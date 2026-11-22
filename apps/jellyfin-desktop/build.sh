#!/bin/bash
# Build Jellyfin Desktop for gabecube: against SteamOS's own Qt, with mpv and the few
# libraries SteamOS lacks next to it. Output: build/jellyfin-desktop/ (a self-contained
# folder; ./deploy.sh copies it over).
#
# Usage: ./build.sh
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
IMAGE=${IMAGE:-tv/apps/jellyfin-desktop:dev}
# The player's version: a tag of https://github.com/jellyfin/jellyfin-desktop
VERSION=${VERSION:-v2.1.0-pre2}
# Packages the player or mpv need that SteamOS 3.9 doesn't install; their libraries go into
# the folder. ./deploy.sh checks on the Steam Machine that nothing else is missing.
BUNDLED_PACKAGES="mpv mujs libcdio libcdio-paranoia libsixel libxpresent uchardet"

docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" "$HERE"

SRC=$HERE/build/src
if [ ! -d "$SRC/.git" ]; then
    git clone --quiet --depth 1 --branch "$VERSION" --recurse-submodules --shallow-submodules \
        https://github.com/jellyfin/jellyfin-desktop.git "$SRC"
elif [ "$(git -C "$SRC" describe --tags --exact-match 2>/dev/null)" != "$VERSION" ]; then
    echo "build/src is not at $VERSION: remove it to fetch that version" >&2
    exit 1
fi

mkdir -p "$HERE/build/out"
rm -rf "$HERE/build/jellyfin-desktop"
mkdir -p "$HERE/build/jellyfin-desktop"
# The binary looks for libraries in lib/ next to it first. RPATH, not RUNPATH (--force-rpath):
# it also applies to what libmpv itself loads.
docker run --rm --network none -u "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$SRC:/src:ro" -v "$HERE/build/out:/out" -v "$HERE/build/jellyfin-desktop:/folder" "$IMAGE" \
    sh -euc "
        cmake -S /src -B /out -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_STATIC_MPVQT=ON \
            -DCMAKE_SKIP_RPATH=ON >/dev/null
        cmake --build /out
        install -m755 /out/src/jellyfin-desktop /folder/jellyfin-desktop
        patchelf --force-rpath --set-rpath '\$ORIGIN/lib' /folder/jellyfin-desktop
        mkdir /folder/lib
        pacman -Qlq $BUNDLED_PACKAGES | grep -E '^/usr/lib/[^/]+\.so(\.[0-9]+)*\$' | xargs cp -a -t /folder/lib
        pacman -Q $BUNDLED_PACKAGES > /folder/lib/PACKAGES
        # libmpv names this one by its full path, which no search path can redirect.
        patchelf --replace-needed /usr/lib/libmujs.so libmujs.so /folder/lib/libmpv.so.2.*
    "
echo "$VERSION" > "$HERE/build/jellyfin-desktop/VERSION"
du -sh "$HERE/build/jellyfin-desktop"
