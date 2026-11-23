#!/usr/bin/python3
"""How quiet the motion sensors are, per pad, in one-second windows: the spread (highest
minus lowest) of each gyroscope axis and of the acceleration's strength, and the mean of
the gyroscope. Usage: dsu-noise.py [seconds]"""
import socket
import struct
import sys
import time
import zlib

SERVER = ("127.0.0.1", 26760)
seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0


def packet(kind, payload):
    body = struct.pack("<I", kind) + payload
    head = b"DSUC" + struct.pack("<HHII", 1001, len(body), 0, 0x1237)
    crc = zlib.crc32(head + body)
    return head[:8] + struct.pack("<I", crc) + head[12:] + body


sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(0.5)
sock.sendto(packet(0x100002, bytes(8)), SERVER)
windows = {}
current = {}
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
    if data[:4] != b"DSUS" or struct.unpack_from("<I", data, 16)[0] != 0x100002:
        continue
    body = data[20:]
    slot = body[0]
    accel = struct.unpack_from("<3f", body, 56)
    gyro = struct.unpack_from("<3f", body, 68)
    c = current.setdefault(slot, [])
    c.append((gyro, sum(a * a for a in accel) ** 0.5))
    if len(c) == 200:
        spread = [max(s[0][i] for s in c) - min(s[0][i] for s in c) for i in range(3)]
        mean = [sum(s[0][i] for s in c) / 200 for i in range(3)]
        strength = max(s[1] for s in c) - min(s[1] for s in c)
        windows.setdefault(slot, []).append((max(spread), max(abs(m) for m in mean), strength))
        current[slot] = []
for slot in sorted(windows):
    w = windows[slot]
    spreads = sorted(x[0] for x in w)
    means = sorted(x[1] for x in w)
    strengths = sorted(x[2] for x in w)
    print("pad %d, %d windows of 1 s:" % (slot, len(w)))
    print("   gyroscope spread, worst axis (deg/s): lowest %.2f, median %.2f, highest %.2f" % (spreads[0], spreads[len(w) // 2], spreads[-1]))
    print("   gyroscope mean, worst axis (deg/s):   lowest %.2f, median %.2f, highest %.2f" % (means[0], means[len(w) // 2], means[-1]))
    print("   strength spread (g):                  lowest %.3f, median %.3f, highest %.3f" % (strengths[0], strengths[len(w) // 2], strengths[-1]))
