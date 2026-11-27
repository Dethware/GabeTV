#!/usr/bin/env python3
"""Play a tone too low and too quiet to hear, without end, to the default sound output.

The Philips HTL3310 soundbar behind the TV (or the TV's ARC output to it) goes quiet when
the sound is silent, and clicks when sound starts again: the first Steam menu sound after a
pause clicks. With this tone playing it never sees silence. -96 dB of dither noise isn't
enough; a 20 Hz tone at -50 dBFS is. See the log, docs/gabecube-findings.md.
"""

import math
import subprocess
import sys

RATE = 48000
FREQUENCY = 20  # Hz: below hearing, and a whole number of periods per second
AMPLITUDE = 0.003  # about -50 dBFS


def one_second():
    """One second of the tone, 16-bit stereo; it repeats without a seam."""
    out = bytearray()
    for i in range(RATE):
        v = round(32767 * AMPLITUDE * math.sin(2 * math.pi * FREQUENCY * i / RATE))
        out += v.to_bytes(2, "little", signed=True) * 2
    return bytes(out)


def main():
    player = subprocess.Popen(
        ["pw-cat", "--playback", "--raw", "--format", "s16", "--rate", str(RATE), "--channels", "2",
         "--media-role", "Notification",
         "--properties", "node.name=soundbar-keepalive node.description=\"Soundbar keep-alive\"",
         "-"],
        stdin=subprocess.PIPE)
    block = one_second()
    try:
        while True:
            player.stdin.write(block)  # blocks while pw-cat's buffer is full
    except BrokenPipeError:
        pass
    sys.exit(player.wait() or 1)


main()
