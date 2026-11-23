#!/bin/bash
# Copy the built service to the Steam Machine and restart it. No root needed, once the
# system part is installed (see --system).
#
# Usage: ./deploy.sh            deploy and restart
#        ./deploy.sh --log      follow its log
#        ./deploy.sh --system   print the command that installs the system part (needs sudo)
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
TARGET=${TARGET:-deck@gabecube}
DIR=.local/share/tvcontrollers

system_command() {
    echo "ssh -t $TARGET sudo bash $DIR/system/install.sh"
}

case "${1:-}" in
--log)
    exec ssh "$TARGET" journalctl -u tv-controllers.service -f -o cat
    ;;
--system)
    system_command
    exit
    ;;
esac

ssh "$TARGET" "mkdir -p $DIR"
# The service keeps running from the old file until it is restarted below.
rsync -a "$HERE/build/tvcontrollers" "$TARGET:$DIR/"
rsync -a --delete "$HERE/system/" "$TARGET:$DIR/system/"

if ! ssh "$TARGET" systemctl -q is-enabled tv-controllers.service 2>/dev/null; then
    echo "The system part isn't installed yet. Run this once, with your sudo password:" >&2
    echo "    $(system_command)" >&2
    exit 0
fi
# The unit runs as the same user, so ending its process needs no root; systemd starts it
# again at once (Restart=always).
ssh "$TARGET" 'pkill -TERM -x tvcontrollers; sleep 2; systemctl --no-pager status tv-controllers.service | head -5'
if ! ssh "$TARGET" "cmp -s $DIR/system/72-tv-joycon.rules /etc/udev/rules.d/72-tv-joycon.rules \
    && cmp -s $DIR/system/tv-no-hid-nintendo.conf /etc/modprobe.d/tv-no-hid-nintendo.conf \
    && cmp -s $DIR/system/tv-controllers-keep.conf /etc/atomic-update.conf.d/tv-controllers.conf \
    && cmp -s $DIR/system/tv-controllers.service /etc/systemd/system/tv-controllers.service"; then
    echo "The installed system files differ from this version. To update them:" >&2
    echo "    $(system_command)" >&2
fi
