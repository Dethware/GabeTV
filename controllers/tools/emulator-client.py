#!/usr/bin/python3
"""A small client of the controller service's emulator socket (controllers/protocol/
emulator.h): says Hello, prints the controllers, counts the input of each for a few
seconds and prints one report of each. With "ring", asks for the ring on right Joy-Cons.
With "rumble", plays a test pattern on every controller in a unit, sent 200 times a second
as a game would (low band, high band, both, 10 ms pulses, a sweep of the high band; on a
Pro Controller then each grip alone), and stops it. With "hold", it ends with a rumble
still on and leaves without stopping it: the service must stop it when the connection ends.
With "rumble" it says it is playing, as Eden does while a game runs (the controllers' virtual
pads are held still meanwhile, and their connections kept ready for rumble).
Usage: emulator-client.py [seconds] [ring] [rumble] [hold]"""
import os, socket, struct, sys, time
VERSION = 2
seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 3.0
ring = "ring" in sys.argv
rumble = "rumble" in sys.argv
hold = "hold" in sys.argv
if rumble:
    seconds = max(seconds, 9.0)
path = os.environ.get("XDG_RUNTIME_DIR", "/run/user/%d" % os.getuid()) + "/tvcontrollers/emulator"
s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
s.connect(path)
s.send(struct.pack("<HH32s", 1, VERSION, b"emulator-client.py"))
s.settimeout(0.005 if rumble else 0.5)

def pattern(t):
    """The test pattern at t seconds: (low amplitude, low Hz, high amplitude, high Hz, motors)."""
    both = (0, 1)
    if t < 1: return (0.6, 160, 0, 320, both)
    if t < 2: return (0, 160, 0.6, 320, both)
    if t < 3: return (0.4, 160, 0.4, 320, both)
    if t < 4.5: return (0, 160, 0.8 if (t - 3) % 0.25 < 0.01 else 0, 320, both)
    if t < 6: return (0, 160, 0.5, 100 * 10 ** ((t - 4.5) / 1.5), both)
    if t < 7: return (0.5, 160, 0, 320, (0,))
    if t < 8: return (0.5, 160, 0, 320, (1,))
    if hold: return (0.4, 160, 0, 320, both)
    return (0, 160, 0, 320, both)

units = []; sent = {}; started = None
SIDES = ["left", "right", "pro"]; GRIPS = ["waiting", "pair", "sideways", "pro"]
count = {}; last = {}; rings = {}; sides = {}
end = time.monotonic() + seconds
next_rumble = 0
while time.monotonic() < end:
    now = time.monotonic()
    if rumble and units and now >= next_rumble:
        next_rumble = now + 0.005
        if started is None:
            # Rumble counts only while a game is played.
            s.send(struct.pack("<HHB", 5, VERSION, 1))
            started = now; print("rumble on", units)
        la, lf, ha, hf, motors = pattern(now - started)
        for cid in units:
            for motor in (0, 1):
                value = (la, lf, ha, hf) if motor in motors else (0, 160, 0, 320)
                if sent.get((cid, motor)) != value:
                    sent[(cid, motor)] = value
                    s.send(struct.pack("<HHBB4f", 4, VERSION, cid, motor, *value))
    try: data = s.recv(512)
    except socket.timeout: continue
    kind, version = struct.unpack_from("<HH", data)
    if kind == 16:
        n = data[4]
        print("controllers (%d bytes):" % len(data))
        for i in range(n):
            cid, side, grip, player, partner, battery, charging, hasring = struct.unpack_from("<8B", data, 5 + 14 * i)
            mac = ":".join("%02X" % b for b in data[5 + 14 * i + 8: 5 + 14 * i + 14])
            sides[cid] = side
            if grip != 0 and cid not in units: units.append(cid)
            print("  id %d %-5s %-8s player %d partner %s battery %d%s%s %s" % (cid, SIDES[side], GRIPS[grip], player,
                  "-" if partner == 255 else partner, battery, " charging" if charging else "", " ring" if hasring else "", mac))
            if ring and side == 1:
                s.send(struct.pack("<HHBB", 2, VERSION, cid, 1))
    elif kind == 17:
        cid, quiet, flex, buttons = struct.unpack_from("<BBhI", data, 4)
        count[cid] = count.get(cid, 0) + 1
        last[cid] = data
        if flex != -32768:
            rings.setdefault(cid, []).append(flex)
for cid in sorted(count):
    data = last[cid]
    cid_, quiet, flex, buttons = struct.unpack_from("<BBhI", data, 4)
    lx, ly, rx, ry = struct.unpack_from("<4f", data, 12)
    print("id %d: %d reports, %.0f a second, %d bytes each; quiet %d buttons %06x left %+.2f %+.2f right %+.2f %+.2f" % (
        cid, count[cid], count[cid] / seconds, len(data), quiet, buttons, lx, ly, rx, ry))
    times = []
    for i in range(3):
        t, ax, ay, az, gx, gy, gz = struct.unpack_from("<Q6f", data, 28 + 32 * i)
        times.append(t)
        if i == 2:
            print("   last reading: acceleration %+.2f %+.2f %+.2f g, rotation %+.1f %+.1f %+.1f deg/s; steps %s us" % (
                ax, ay, az, gx, gy, gz, [times[1] - times[0], times[2] - times[1]]))
    if cid in rings:
        r = rings[cid]; print("   ring: %d values, %d to %d, last %d" % (len(r), min(r), max(r), r[-1]))
