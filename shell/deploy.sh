#!/bin/bash
# Copy the built shell and its user unit to the Steam Machine and (re)start it.
# Doesn't enable the unit: it runs until stopped or until Gaming Mode restarts.
#
# Usage: ./deploy.sh          deploy and restart
#        ./deploy.sh --stop   stop it (it hands the screen back to Steam)
#        ./deploy.sh --log    follow its log
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
TARGET=${TARGET:-deck@gabecube}

case "${1:-}" in
--stop)
    exec ssh "$TARGET" systemctl --user stop tvshell.service
    ;;
--log)
    exec ssh "$TARGET" journalctl --user -u tvshell.service -f -o cat
    ;;
esac

rsync -a "$HERE/build/tvshell" "$TARGET:.local/share/tvshell/"
rsync -a "$HERE/deploy/tvshell.service" "$TARGET:.config/systemd/user/"
ssh "$TARGET" 'systemctl --user daemon-reload && systemctl --user restart tvshell.service && sleep 2 && systemctl --user --no-pager status tvshell.service | head -5'
