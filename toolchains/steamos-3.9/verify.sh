#!/bin/bash
# Compare package versions in the toolchain image with what's installed on the Steam Machine.
# Every package present in both must have the same version.
#
# Usage: ./verify.sh [image] [ssh-target]
set -euo pipefail
export LC_ALL=C
IMAGE=${1:-tv/toolchains/steamos-3.9:dev}
TARGET=${2:-deck@gabecube}

image=$(docker run --rm --network none "$IMAGE" cat /etc/toolchain-packages.txt | sort)
target=$(ssh "$TARGET" pacman -Q | sort)

common=$(join <(echo "$image") <(echo "$target") | wc -l)
mismatch=$(join <(echo "$image") <(echo "$target") | awk '$2 != $3')
echo "$common packages in both"
if [ -n "$mismatch" ]; then
    echo "version mismatches (package image target):"
    echo "$mismatch"
    exit 1
fi
echo "all versions match $TARGET ($(ssh "$TARGET" "grep -E '^(VERSION_ID|BUILD_ID)=' /etc/os-release | tr '\n' ' '"))"
