#!/bin/bash
# Copy the built Eden folder (./build.sh) to the Steam Machine and check that every library
# it needs is there. What Eden keeps (settings, keys, system files, saves) is in the folder
# user/ next to the program and is left alone.
#
# Usage: ./deploy.sh
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
TARGET=${TARGET:-deck@gabecube}
# Where it lives on the Steam Machine, relative to the home directory.
FOLDER=${FOLDER:-apps/eden}

# With user/ next to the program Eden keeps everything in there, not in ~/.config and
# ~/.local/share.
ssh "$TARGET" "mkdir -p '$FOLDER/user'"
rsync -a --delete --exclude /user "$HERE/build/eden/" "$TARGET:$FOLDER/"
ssh "$TARGET" "
    missing=\$(ldd '$FOLDER/eden' | grep 'not found' | sort -u)
    if [ -n \"\$missing\" ]; then
        echo 'missing libraries (add their packages to BUNDLED_PACKAGES in build.sh):' >&2
        echo \"\$missing\" >&2
        exit 1
    fi
    echo \"installed \$(cat '$FOLDER/VERSION') in ~/$FOLDER, all libraries found\"
"
