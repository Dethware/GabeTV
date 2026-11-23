#!/bin/bash
# Build the Eden emulator for gabecube: against SteamOS's own Qt, SDL and ffmpeg, with the
# few libraries SteamOS lacks next to it. Output: build/eden/ (a self-contained folder;
# ./deploy.sh copies it over).
#
# Usage: ./build.sh
#        FETCH=1 ./build.sh    # let Eden's build fetch dependency sources again
set -euo pipefail
shopt -s nullglob
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
IMAGE=${IMAGE:-tv/apps/eden:dev}
# Eden's version: a tag of https://git.eden-emu.dev/eden-emu/eden
VERSION=${VERSION:-v0.2.1}
# Packages Eden needs that SteamOS 3.9 doesn't install; their libraries go into the folder.
# ./deploy.sh checks on the Steam Machine that nothing else is missing.
BUNDLED_PACKAGES="enet qt6-charts"

docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" "$HERE"

SRC=$HERE/build/src
if [ ! -d "$SRC/.git" ]; then
    git clone --quiet --depth 1 --branch "$VERSION" https://git.eden-emu.dev/eden-emu/eden.git "$SRC"
elif [ "$(git -C "$SRC" describe --tags --exact-match 2>/dev/null)" != "$VERSION" ]; then
    echo "build/src is not at $VERSION: remove it to fetch that version" >&2
    exit 1
fi

# Our changes to Eden (patches/): applied once, on top of the fetched version.
for patch in "$HERE"/patches/*.patch; do
    if git -C "$SRC" apply --check "$patch" 2>/dev/null; then
        git -C "$SRC" apply "$patch"
        echo "applied $(basename "$patch")"
    elif ! git -C "$SRC" apply --reverse --check "$patch" 2>/dev/null; then
        echo "$(basename "$patch") doesn't apply to $VERSION" >&2
        exit 1
    fi
done

# Our own source files for Eden (driver/) and the controller service's side of what they
# speak (../../controllers/protocol) go into Eden's tree; a patch hooks them in. Only
# copied when they changed, so that nothing is rebuilt for nothing.
place() {
    cmp -s "$1" "$2" || install -m644 "$1" "$2"
}
place "$HERE/driver/tv_controllers.h" "$SRC/src/hid_core/frontend/tv_controllers.h"
place "$HERE/driver/tv_controllers.cpp" "$SRC/src/hid_core/frontend/tv_controllers.cpp"
place "$HERE/../../controllers/protocol/emulator.h" "$SRC/src/hid_core/frontend/tv_emulator_protocol.h"
place "$HERE/driver/tv_player.h" "$SRC/src/yuzu/tv_player.h"
place "$HERE/driver/tv_player.cpp" "$SRC/src/yuzu/tv_player.cpp"

mkdir -p "$HERE/build/out"
run() {
    docker run --rm "$@" -u "$(id -u):$(id -g)" -e HOME=/tmp \
        -v "$SRC:/src" -v "$HERE/build/out:/out" -v "$HERE/build/eden:/folder" "$IMAGE" sh -euc "$COMMAND"
}
rm -rf "$HERE/build/eden"
mkdir -p "$HERE/build/eden"

# Configuring is the one step with network access: Eden's build fetches the source of what
# SteamOS's repositories don't have (into build/src/.cache/cpm) while it configures. Once
# that is there, everything runs without.
# - zen4: the Steam Machine's processor.
# - fmt is Eden's own copy, built in: the version it is written for.
# - No tests, no multiplayer room or web service, no update check, no second (SDL) frontend.
# - Qt Multimedia: Eden's way of taking camera images, for the Joy-Con's IR camera later.
COMMAND="
    cmake -S /src -B /out -G Ninja -DCMAKE_BUILD_TYPE=Release -DYUZU_BUILD_PRESET=zen4 \
        -Dfmt_FORCE_BUNDLED=ON \
        -DYUZU_TESTS=OFF -DENABLE_WEB_SERVICE=OFF -DENABLE_UPDATE_CHECKER=OFF \
        -DUSE_DISCORD_PRESENCE=OFF -DYUZU_ROOM=OFF -DYUZU_ROOM_STANDALONE=OFF -DYUZU_CMD=OFF \
        -DENABLE_QT_TRANSLATION=OFF -DYUZU_USE_QT_MULTIMEDIA=ON \
        -DCMAKE_SKIP_RPATH=ON >/dev/null
"
if [ -n "${FETCH:-}" ] || [ ! -d "$SRC/.cache/cpm" ]; then
    run
else
    run --network none
fi

# The binary looks for libraries in lib/ next to it first.
COMMAND="
    cmake --build /out
    install -m755 /out/bin/eden /folder/eden
    patchelf --force-rpath --set-rpath '\$ORIGIN/lib' /folder/eden
    mkdir /folder/lib
    pacman -Qlq $BUNDLED_PACKAGES | grep -E '^/usr/lib/[^/]+\.so(\.[0-9]+)*\$' | xargs cp -a -t /folder/lib
    pacman -Q $BUNDLED_PACKAGES > /folder/lib/PACKAGES
"
run --network none
echo "$VERSION" > "$HERE/build/eden/VERSION"
du -sh "$HERE/build/eden"
