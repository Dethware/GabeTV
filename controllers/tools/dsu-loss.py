#!/usr/bin/python3
"""Per pad of the motion server: how many readings were lost, from the steps between the
readings' own times (5 ms when none is lost). Usage: dsu-loss.py [seconds]"""
import socket
import struct
import sys
import time
import zlib

SERVER = ("127.0.0.1", 26760)
seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0


def packet(kind, payload):
    body = struct.pack("<I", kind) + payload
    head = b"DSUC" + struct.pack("<HHII", 1001, len(body), 0, 0x1235)
    crc = zlib.crc32(head + body)
    return head[:8] + struct.pack("<I", crc) + head[12:] + body


sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(0.5)
sock.sendto(packet(0x100002, bytes(8)), SERVER)
last = {}
got = {}
lost = {}
runs = {}
when = {}
mac = {}
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
    mac[slot] = ":".join("%02X" % b for b in body[4:10])
    stamp = struct.unpack_from("<Q", body, 48)[0]
    got[slot] = got.get(slot, 0) + 1
    if slot in last:
        missing = round((stamp - last[slot]) / 5000) - 1
        if missing > 0:
            lost[slot] = lost.get(slot, 0) + missing
            runs.setdefault(slot, []).append(missing // 3)
            when.setdefault(slot, []).append(stamp / 1e6)
    last[slot] = stamp
for slot in sorted(got):
    n = got[slot]
    m = lost.get(slot, 0)
    r = runs.get(slot, [])
    print("pad %d %s: %d readings, %d lost (%.2f %%), in %d gaps, longest %d reports in a row" % (
        slot, mac[slot], n, m, 100.0 * m / (n + m), len(r), max(r) if r else 0))
    w = when.get(slot, [])
    if len(w) > 3:
        gaps = sorted(round(b - a, 2) for a, b in zip(w, w[1:]))
        from collections import Counter
        common = Counter(round(g, 1) for g in gaps).most_common(6)
        print("   time between gaps (s): shortest %.2f, median %.2f, longest %.2f; most common: %s" % (
            gaps[0], gaps[len(gaps) // 2], gaps[-1], ", ".join("%.1f s x%d" % c for c in common)))
