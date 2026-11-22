#!/bin/bash
# Copy the built Jellyfin Desktop folder (./build.sh) to the Steam Machine, check that every
# library it needs is there, and install the user service the TV shell starts it with.
#
# Usage: ./deploy.sh
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
TARGET=${TARGET:-deck@gabecube}
# Where it lives on the Steam Machine, relative to the home directory.
FOLDER=${FOLDER:-apps/jellyfin-desktop}

ssh "$TARGET" "mkdir -p '$FOLDER'"
rsync -a --delete "$HERE/build/jellyfin-desktop/" "$TARGET:$FOLDER/"
ssh "$TARGET" "
    missing=\$(ldd '$FOLDER/jellyfin-desktop' | grep 'not found' | sort -u)
    if [ -n \"\$missing\" ]; then
        echo 'missing libraries (add their packages to BUNDLED_PACKAGES in build.sh):' >&2
        echo \"\$missing\" >&2
        exit 1
    fi
    echo \"installed \$(cat '$FOLDER/VERSION') in ~/$FOLDER, all libraries found\"
"

# The service is only installed, not enabled or started: the shell starts it. A running player
# keeps running; it gets the new files the next time it starts.
rsync -a "$HERE/jellyfin-desktop.service" "$TARGET:.config/systemd/user/"
ssh "$TARGET" 'systemctl --user daemon-reload'
echo "service installed"

# The key map for Steam's virtual gamepad and the script that hands the screen back to the
# shell go into every profile the player has made so far (it creates the first one when it
# first starts).
for profile in $(ssh "$TARGET" 'ls -d .local/share/jellyfin-desktop/profiles/*/ 2>/dev/null'); do
    rsync -a "$HERE/inputmaps/" "$TARGET:${profile}inputmaps/"
    rsync -a "$HERE/scripts/" "$TARGET:${profile}scripts/"
    echo "key map and script installed in ~/$profile"
done
