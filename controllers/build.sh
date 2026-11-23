#!/bin/bash
# Build tvcontrollers in the SteamOS toolchain image (toolchains/steamos-3.9).
# Output: controllers/build/
#
# Usage: ./build.sh [cmake build options...]
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
IMAGE=${IMAGE:-tv/toolchains/steamos-3.9:dev}
BUILD_TYPE=${BUILD_TYPE:-RelWithDebInfo}

mkdir -p "$HERE/build"
docker run --rm --network none -u "$(id -u):$(id -g)" \
    -v "$HERE:/src:ro" -v "$HERE/build:/out" "$IMAGE" \
    sh -c "cmake -S /src -B /out -G Ninja -DCMAKE_BUILD_TYPE=$BUILD_TYPE >/dev/null && cmake --build /out $*"
