#!/bin/bash
# Start or stop Eden with a game on the Steam Machine by hand, over ssh. A stand-in until
# the TV shell starts Eden itself (DEVELOPMENT.md, "Next"): it puts Eden's windows on the
# screen in a way the shell takes for a Steam game, so the shell's own keys and dialogs
# don't work over it. Form the controller (L+R) before starting.
#
# Usage: ./try.sh start [game file on the Steam Machine]
#        ./try.sh stop
set -euo pipefail
TARGET=${TARGET:-deck@gabecube}
FOLDER=${FOLDER:-apps/eden}
GAME=${2:-Games/Emulators/Switch/ringfit/ROM.xci}
# The id Eden's windows are tagged with. Not one of the shell's (0x7E0000xx).
ID=2097152001

case "${1:-}" in
start)
    ssh "$TARGET" "
        export DISPLAY=:0
        systemctl --user reset-failed eden-test eden-tagger 2>/dev/null
        # From its own folder: that is where Eden looks for user/.
        systemd-run --user --unit eden-test --collect --quiet \
            -p EnvironmentFile=/run/user/\$(id -u)/gamescope-environment \
            -p WorkingDirectory=\$HOME/$FOLDER -E QT_QPA_PLATFORM=xcb \
            \$HOME/$FOLDER/eden -f -g \"\$HOME/$GAME\"
        # Every window Eden opens is tagged, also the ones it opens later: its on-screen
        # keyboard is a dialog of its own, and the game waits behind it.
        systemd-run --user --unit eden-tagger --collect --quiet bash -c '
            export DISPLAY=:0
            sleep 3
            while pgrep -x eden >/dev/null; do
                for w in \$(xdotool search --class eden 2>/dev/null); do
                    xprop -id \$w STEAM_GAME 2>/dev/null | grep -q $ID ||
                        xprop -id \$w -f STEAM_GAME 32c -set STEAM_GAME $ID 2>/dev/null
                done
                sleep 1
            done'
        sleep 10
        layer=\$(xprop -root GAMESCOPECTRL_BASELAYER_APPID | sed 's/.*= //')
        case \"\$layer\" in *$ID*) ;; *)
            xprop -root -f GAMESCOPECTRL_BASELAYER_APPID 32c -set GAMESCOPECTRL_BASELAYER_APPID \"$ID, \$layer\" ;;
        esac
        echo \"Eden: \$(systemctl --user is-active eden-test)\"
    "
    ;;
stop)
    ssh "$TARGET" "
        export DISPLAY=:0
        # Eden doesn't end on SIGTERM while a game runs.
        systemctl --user kill --signal=SIGKILL eden-test 2>/dev/null || true
        systemctl --user stop eden-tagger 2>/dev/null || true
        layer=\$(xprop -root GAMESCOPECTRL_BASELAYER_APPID | sed 's/.*= //; s/$ID, //; s/, $ID//')
        xprop -root -f GAMESCOPECTRL_BASELAYER_APPID 32c -set GAMESCOPECTRL_BASELAYER_APPID \"\$layer\"
        echo stopped
    "
    ;;
*)
    echo "usage: $0 start [game] | stop" >&2
    exit 1
    ;;
esac
