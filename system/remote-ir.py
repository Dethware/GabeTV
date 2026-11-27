#!/usr/bin/env python3
"""Load the TV's infrared codes into the Google TV (Chromecast) remote, or clear them.

Run on gabecube as deck, with the remote connected:

    ssh deck@gabecube 'python3 - lg' < system/remote-ir.py      # LG TV: volume and mute
    ssh deck@gabecube 'python3 - clear' < system/remote-ir.py   # no infrared at all

A key with a code goes out as infrared to the TV; the remote keeps the codes in its flash.
The programming service and the code format are described in the log
(docs/gabecube-findings.md, "IR programming service").
"""

import subprocess
import sys

REMOTE_NAME = "Chromecast Remote"
SERVICE_PREFIX = "d343bfc"
PROGRAM_CONTROL, KEY_ID, CODE = "1", "2", "3"

# Android key codes, as the remote's Key ID wants them.
KEY_VOLUME_UP = 0x0018
KEY_VOLUME_DOWN = 0x0019
KEY_MUTE = 0x00A4

CARRIER_HZ = 38000
DUTY_PERCENT = 33


def us(microseconds):
    """A duration in carrier cycles."""
    return round(microseconds * CARRIER_HZ / 1_000_000)


def nec(code):
    """An NEC frame and its repeat frame, as (mark, space) pairs in carrier cycles.

    code is the 32 bits in the order they are sent, as LIRC writes LG's codes (0x20DF....).
    Each frame lasts 108 ms; the last space fills it up.
    """
    frame = [(9000, 4500)]
    for bit in range(31, -1, -1):
        frame.append((560, 1690 if code >> bit & 1 else 560))
    frame.append((560, 0))
    frame[-1] = (560, 108000 - sum(m + s for m, s in frame))
    repeat = [(9000, 2250), (560, 108000 - 9000 - 2250 - 560)]
    return [(us(m), us(s)) for m, s in frame], [(us(m), us(s)) for m, s in repeat]


def code_bytes(first, repeat=None):
    """The Code characteristic's value: type 1 (one sequence) or 3 (first, then repeating)."""
    out = bytearray([3 if repeat else 1, DUTY_PERCENT])
    out += (CARRIER_HZ // 100).to_bytes(2, "big")
    out += len(first).to_bytes(2, "big")
    if repeat:
        out += len(repeat).to_bytes(2, "big")
    for mark, space in first + (repeat or []):
        out += mark.to_bytes(2, "big") + space.to_bytes(2, "big")
    return bytes(out)


# LG TVs: NEC, address 0x04. Volume repeats while held; Mute is sent once per press.
def lg_codes():
    up = nec(0x20DF40BF)
    down = nec(0x20DFC03F)
    mute = nec(0x20DF906F)
    return [
        (KEY_VOLUME_UP, code_bytes(*up)),
        (KEY_VOLUME_DOWN, code_bytes(*down)),
        (KEY_MUTE, code_bytes(mute[0])),
    ]


def busctl(*args):
    return subprocess.run(["busctl", *args], check=True, capture_output=True, text=True).stdout


def characteristics():
    """The programming service's characteristics on the connected remote, by last UUID digit."""
    found = {}
    for path in busctl("tree", "org.bluez", "--list").split():
        if "/char" not in path:
            continue
        try:
            uuid = busctl("get-property", "org.bluez", path, "org.bluez.GattCharacteristic1", "UUID")
        except subprocess.CalledProcessError:
            continue
        uuid = uuid.split('"')[1]
        if uuid.startswith(SERVICE_PREFIX):
            device = busctl("get-property", "org.bluez", path.rsplit("/", 2)[0], "org.bluez.Device1", "Name")
            if REMOTE_NAME in device:
                found[uuid[len(SERVICE_PREFIX)]] = path
    if not all(k in found for k in (PROGRAM_CONTROL, KEY_ID, CODE)):
        sys.exit(f"{REMOTE_NAME}'s IR programming service not found: is it connected?")
    return found


def write(path, value):
    busctl("call", "org.bluez", path, "org.bluez.GattCharacteristic1", "WriteValue", "aya{sv}",
           str(len(value)), *(str(b) for b in value), "1", "type", "s", "request")


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else ""
    if what == "lg":
        codes = lg_codes()
    elif what == "clear":
        codes = []
    else:
        sys.exit("usage: remote-ir.py lg|clear")
    chars = characteristics()
    write(chars[PROGRAM_CONTROL], b"\x01")  # start: clears the table
    for key, code in codes:
        write(chars[KEY_ID], key.to_bytes(2, "big"))
        write(chars[CODE], code)
        print(f"key {key:#06x}: {len(code)} bytes")
    write(chars[PROGRAM_CONTROL], b"\x00")  # end: kept in flash
    print("done" if codes else "cleared")


main()
