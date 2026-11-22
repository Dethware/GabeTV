#!/bin/bash
# Install the watchdog on the Steam Machine as a user service and start it. No root needed.
#
# Usage: ./install.sh            install, enable and (re)start
#        ./install.sh --remove   stop, disable and remove it
#        ./install.sh --log      show what it has done
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
TARGET=${TARGET:-deck@gabecube}

case "${1:-}" in
--remove)
    ssh "$TARGET" 'systemctl --user disable --now steam-ui-watchdog.service; rm -rf .local/share/steam-ui-watchdog .config/systemd/user/steam-ui-watchdog.service; systemctl --user daemon-reload'
    exit
    ;;
--log)
    exec ssh "$TARGET" journalctl --user -u steam-ui-watchdog.service --no-pager -o short
    ;;
esac

ssh "$TARGET" 'mkdir -p .local/share/steam-ui-watchdog .config/systemd/user'
rsync -a "$HERE/steam-ui-watchdog.py" "$TARGET:.local/share/steam-ui-watchdog/"
rsync -a "$HERE/steam-ui-watchdog.service" "$TARGET:.config/systemd/user/"
ssh "$TARGET" 'systemctl --user daemon-reload && systemctl --user enable steam-ui-watchdog.service && systemctl --user restart steam-ui-watchdog.service && sleep 2 && systemctl --user --no-pager status steam-ui-watchdog.service | head -n 6'
