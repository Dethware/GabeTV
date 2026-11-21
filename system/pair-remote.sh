#!/bin/bash
# Pair the Google TV (Chromecast) remote with this machine. Run on gabecube as deck (no root),
# or from elsewhere: ssh deck@gabecube 'bash -s' < system/pair-remote.sh
#
# Put the remote in pairing mode first: hold Back + Home until its light pulses.
# Forgets any earlier pairing, waits for the remote's advert, then pairs, trusts and connects,
# retrying each step (the first attempt often times out).
set -uo pipefail

NAME=${REMOTE_NAME:-Chromecast Remote}
WAIT=${WAIT:-90}

say() { echo "[$(date +%T)] $*"; }

device_by_name() {
    bluetoothctl devices | awk -v n="$NAME" '{ mac = $2; $1 = $2 = ""; sub(/^ +/, ""); if ($0 == n) print mac }'
}

prop() { bluetoothctl info "$1" 2>/dev/null | awk -v k="$2:" '$1 == k { print $2 }'; }

# Forget it: a stale bond (the remote now bonded to another host) makes pairing fail.
for mac in $(device_by_name); do
    say "forgetting $mac"
    bluetoothctl remove "$mac" >/dev/null
done

say "scanning for \"$NAME\" for up to ${WAIT}s (hold Back + Home on the remote)"
bluetoothctl --timeout "$WAIT" scan le >/dev/null 2>&1 &
scan=$!
trap 'kill $scan 2>/dev/null' EXIT

mac=""
for ((i = 0; i < WAIT; i++)); do
    mac=$(device_by_name | head -1)
    [ -n "$mac" ] && [ -n "$(prop "$mac" RSSI)" ] && break
    mac=""
    sleep 1
done
if [ -z "$mac" ]; then
    say "remote not seen; is it in pairing mode (light pulsing)?"
    exit 1
fi
say "found $mac (RSSI $(prop "$mac" RSSI))"

for attempt in 1 2 3 4 5; do
    say "pair, attempt $attempt"
    bluetoothctl --agent NoInputNoOutput --timeout 30 pair "$mac" 2>&1 | grep -E "Pairing successful|Failed|Error" | tail -1
    [ "$(prop "$mac" Paired)" = yes ] && break
    sleep 2
done
if [ "$(prop "$mac" Paired)" != yes ]; then
    say "pairing failed; put the remote back in pairing mode and run this again"
    exit 1
fi
kill $scan 2>/dev/null

bluetoothctl trust "$mac" >/dev/null
for attempt in 1 2 3; do
    [ "$(prop "$mac" Connected)" = yes ] && break
    say "connect, attempt $attempt"
    bluetoothctl --timeout 15 connect "$mac" >/dev/null 2>&1
    sleep 2
done

say "paired: $(prop "$mac" Paired), bonded: $(prop "$mac" Bonded), trusted: $(prop "$mac" Trusted), connected: $(prop "$mac" Connected)"
[ "$(prop "$mac" Bonded)" = yes ] && [ "$(prop "$mac" Connected)" = yes ]
