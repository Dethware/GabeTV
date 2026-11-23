#!/usr/bin/python3
"""A small client of the controller service's emulator socket (controllers/protocol/
emulator.h): says Hello, prints the controllers, counts the input of each for a few
seconds and prints one report of each. With "ring", asks for the ring on right Joy-Cons.
Usage: emulator-client.py [seconds] [ring]"""
import os, socket, struct, sys, time
seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 3.0
ring = "ring" in sys.argv
path = os.environ.get("XDG_RUNTIME_DIR", "/run/user/%d" % os.getuid()) + "/tvcontrollers/emulator"
s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
s.connect(path)
s.send(struct.pack("<HH32s", 1, 1, b"emulator-client.py"))
s.settimeout(0.5)
SIDES = ["left", "right", "pro"]; GRIPS = ["waiting", "pair", "sideways", "pro"]
count = {}; last = {}; rings = {}; sides = {}
end = time.monotonic() + seconds
while time.monotonic() < end:
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
            print("  id %d %-5s %-8s player %d partner %s battery %d%s%s %s" % (cid, SIDES[side], GRIPS[grip], player,
                  "-" if partner == 255 else partner, battery, " charging" if charging else "", " ring" if hasring else "", mac))
            if ring and side == 1:
                s.send(struct.pack("<HHBB", 2, 1, cid, 1))
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
