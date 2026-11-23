#!/usr/bin/python3
"""A small client of the cemuhook (DSU) protocol, to check the controller service's motion
server: asks which pads there are, subscribes to all of them and prints, per pad, how many
packets a second arrive, how evenly, and the average acceleration and rotation.

Usage: dsu-client.py [seconds]
"""
import socket
import struct
import sys
import time
import zlib

SERVER = ("127.0.0.1", 26760)
seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 6.0


def packet(kind, payload):
    body = struct.pack("<I", kind) + payload
    head = b"DSUC" + struct.pack("<HHII", 1001, len(body), 0, 0x1234)
    crc = zlib.crc32(head + body)
    return head[:8] + struct.pack("<I", crc) + head[12:] + body


sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(0.5)

sock.sendto(packet(0x100001, struct.pack("<i4B", 4, 0, 1, 2, 3)), SERVER)
sock.sendto(packet(0x100002, bytes(8)), SERVER)

pads = {}
stats = {}
bad_crc = 0
end = time.monotonic() + seconds
asked = time.monotonic()
while time.monotonic() < end:
    if time.monotonic() - asked > 1.0:
        sock.sendto(packet(0x100002, bytes(8)), SERVER)
        asked = time.monotonic()
    try:
        data, _ = sock.recvfrom(256)
    except socket.timeout:
        continue
    if data[:4] != b"DSUS":
        continue
    crc = struct.unpack_from("<I", data, 8)[0]
    if zlib.crc32(data[:8] + bytes(4) + data[12:]) != crc:
        bad_crc += 1
        continue
    kind = struct.unpack_from("<I", data, 16)[0]
    body = data[20:]
    if kind == 0x100001:
        slot, state, model, conn = body[0], body[1], body[2], body[3]
        mac = ":".join("%02X" % b for b in body[4:10])
        pads[slot] = "state %d model %d connection %d address %s battery %#x" % (state, model, conn, mac, body[10])
    elif kind == 0x100002 and len(body) >= 80:
        slot = body[0]
        accel = struct.unpack_from("<3f", body, 56)
        gyro = struct.unpack_from("<3f", body, 68)
        s = stats.setdefault(slot, {"n": 0, "last": None, "gaps": [], "accel": [0, 0, 0], "gyro": [0, 0, 0],
                                    "stamp": None, "steps": [], "first": None})
        stamp = struct.unpack_from("<Q", body, 48)[0]
        if s["stamp"] is not None:
            s["steps"].append(stamp - s["stamp"])
        else:
            s["first"] = (stamp, time.monotonic())
        s["stamp"] = stamp
        s["end"] = (stamp, time.monotonic())
        now = time.monotonic()
        if s["last"] is not None:
            s["gaps"].append((now - s["last"]) * 1000)
        s["last"] = now
        s["n"] += 1
        for i in range(3):
            s["accel"][i] += accel[i]
            s["gyro"][i] += gyro[i]

print("pads:")
for slot in sorted(pads):
    print("  %d: %s" % (slot, pads[slot]))
print("packets with a wrong checksum:", bad_crc)
for slot in sorted(stats):
    s = stats[slot]
    gaps = sorted(s["gaps"])
    n = s["n"]
    print("pad %d: %d packets, %.0f a second; gap median %.1f ms, 95%% under %.1f ms, longest %.1f ms" % (
        slot, n, n / seconds, gaps[len(gaps) // 2], gaps[int(len(gaps) * 0.95)], gaps[-1]))
    steps = sorted(s["steps"])
    exact = sum(1 for step in steps if step == 5000)
    span = (s["end"][0] - s["first"][0]) / 1e6
    wall = s["end"][1] - s["first"][1]
    print("   reading times: %d of %d steps exactly 5 ms, shortest %d us, longest %d us; they span %.3f s in %.3f s" % (
        exact, len(steps), steps[0], steps[-1], span, wall))
    print("   acceleration (g), protocol x y z:   %+.2f %+.2f %+.2f   length %.2f" % (
        s["accel"][0] / n, s["accel"][1] / n, s["accel"][2] / n,
        sum((a / n) ** 2 for a in s["accel"]) ** 0.5))
    print("   rotation (deg/s), pitch yaw roll:   %+.1f %+.1f %+.1f" % (
        s["gyro"][0] / n, s["gyro"][1] / n, s["gyro"][2] / n))
if not stats:
    print("no motion data received")
