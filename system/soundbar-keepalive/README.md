# Soundbar keep-alive

Plays a tone too low and too quiet to hear (20 Hz, about −50 dB) to the sound output all the
time. The Philips HTL3310 soundbar behind the TV goes quiet during silence and clicks when
sound starts again, so the first Steam menu sound after a pause clicked. With the tone
playing, it never sees silence.

Only for that soundbar: when it is replaced, remove this and check whether the new one needs
it.

## Install

    ./install.sh             # copy it to the Steam Machine, enable and start it (no root)
    ./install.sh --remove    # stop and remove it

It runs as the user service `soundbar-keepalive.service`, and shows in the sound settings as
the stream "Soundbar keep-alive". Another machine: `TARGET=user@host ./install.sh`.
