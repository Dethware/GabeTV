#!/bin/bash
# The part of the controller service that needs root, run on the Steam Machine itself:
#
#   sudo bash ~/.local/share/tvcontrollers/system/install.sh            install or update
#   sudo bash ~/.local/share/tvcontrollers/system/install.sh --remove   take it out again
#
# It installs four files: the udev rule that hides the Joy-Cons from everything but the
# service, the line that switches off the kernel's own Joy-Con driver, the service's unit,
# and the entry that keeps the first two across SteamOS updates.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
RULE=/etc/udev/rules.d/72-tv-joycon.rules
MODPROBE=/etc/modprobe.d/tv-no-hid-nintendo.conf
UNIT=/etc/systemd/system/tv-controllers.service
KEEP=/etc/atomic-update.conf.d/tv-controllers.conf

if [ "$(id -u)" != 0 ]; then
    echo "run this with sudo" >&2
    exit 1
fi

if [ "${1:-}" = --remove ]; then
    systemctl disable --now tv-controllers.service 2>/dev/null || true
    rm -f "$RULE" "$MODPROBE" "$UNIT" "$KEEP"
    systemctl daemon-reload
    udevadm control --reload
    modprobe hid_nintendo || true
    echo "removed; Joy-Cons are visible to Steam again from their next connection"
    exit
fi

install -m 0644 "$HERE/72-tv-joycon.rules" "$RULE"
install -m 0644 "$HERE/tv-no-hid-nintendo.conf" "$MODPROBE"
install -m 0644 "$HERE/tv-controllers.service" "$UNIT"
install -D -m 0644 "$HERE/tv-controllers-keep.conf" "$KEEP"
udevadm control --reload
# Take the kernel driver out now; after the next boot it no longer loads.
if [ -d /sys/module/hid_nintendo ]; then
    rmmod hid_nintendo && echo "kernel driver hid_nintendo unloaded" \
        || echo "hid_nintendo is still loaded; it will be gone after a reboot" >&2
fi
systemctl daemon-reload
systemctl enable tv-controllers.service
systemctl restart tv-controllers.service
sleep 1
systemctl --no-pager status tv-controllers.service | head -n 5
echo "installed; a Joy-Con that is connected now must reconnect for the changes to apply"
