#!/bin/bash
# Install the wake log on the Steam Machine as a user service and start it. No root needed;
# the one setting that does need it is printed, for you to run once.
#
# Usage: ./install.sh            install, enable and (re)start
#        ./install.sh --remove   stop, disable and remove it
#        ./install.sh --log      show the reports so far
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
TARGET=${TARGET:-deck@gabecube}

case "${1:-}" in
--remove)
    ssh "$TARGET" 'systemctl --user disable --now wake-log.service; rm -rf .local/share/wake-log .config/systemd/user/wake-log.service; systemctl --user daemon-reload'
    exit
    ;;
--log)
    exec ssh "$TARGET" journalctl --user -u wake-log.service --no-pager -o short
    ;;
esac

ssh "$TARGET" 'mkdir -p .local/share/wake-log .config/systemd/user'
rsync -a "$HERE/wake-log.py" "$HERE/tv-wake-log.conf" "$HERE/tv-wake-log-keep.conf" "$TARGET:.local/share/wake-log/"
rsync -a "$HERE/wake-log.service" "$TARGET:.config/systemd/user/"
ssh "$TARGET" 'systemctl --user daemon-reload && systemctl --user enable wake-log.service && systemctl --user restart wake-log.service && sleep 2 && systemctl --user --no-pager status wake-log.service | head -n 6'

if [ "$(ssh "$TARGET" cat /sys/power/pm_debug_messages)" != 1 ]; then
    cat <<'TEXT'

For the kernel to say which interrupt ended a sleep, run this once on the Steam Machine
(it needs root, and stays through reboots and SteamOS updates):

    sudo install -m644 ~/.local/share/wake-log/tv-wake-log.conf /etc/tmpfiles.d/ && \
    sudo install -m644 ~/.local/share/wake-log/tv-wake-log-keep.conf /etc/atomic-update.conf.d/ && \
    sudo systemd-tmpfiles --create /etc/tmpfiles.d/tv-wake-log.conf
TEXT
fi
