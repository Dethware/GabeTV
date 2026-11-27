#!/bin/bash
# Install the soundbar keep-alive on the Steam Machine as a user service and start it.
#
# Usage: ./install.sh            install, enable and (re)start
#        ./install.sh --remove   stop, disable and remove it
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
TARGET=${TARGET:-deck@gabecube}

if [ "${1:-}" = --remove ]; then
    ssh "$TARGET" 'systemctl --user disable --now soundbar-keepalive.service; rm -rf .local/share/soundbar-keepalive .config/systemd/user/soundbar-keepalive.service; systemctl --user daemon-reload'
    exit
fi

ssh "$TARGET" 'mkdir -p .local/share/soundbar-keepalive .config/systemd/user'
rsync -a "$HERE/soundbar-keepalive.py" "$TARGET:.local/share/soundbar-keepalive/"
rsync -a "$HERE/soundbar-keepalive.service" "$TARGET:.config/systemd/user/"
ssh "$TARGET" 'systemctl --user daemon-reload && systemctl --user enable soundbar-keepalive.service && systemctl --user restart soundbar-keepalive.service && sleep 2 && systemctl --user --no-pager status soundbar-keepalive.service | head -n 6'
