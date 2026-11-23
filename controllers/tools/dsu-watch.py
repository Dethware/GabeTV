#!/usr/bin/python3
"""Lost readings per pad of the motion server, printed every ten seconds.
Usage: dsu-watch.py [seconds]"""
import socket
import struct
import sys
import time
import zlib

SERVER = ("127.0.0.1", 26760)
seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 300.0


def packet(kind, payload):
    body = struct.pack("<I", kind) + payload
    head = b"DSUC" + struct.pack("<HHII", 1001, len(body), 0, 0x1236)
    crc = zlib.crc32(head + body)
    return head[:8] + struct.pack("<I", crc) + head[12:] + body


sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(0.5)
sock.sendto(packet(0x100002, bytes(8)), SERVER)
last = {}
got = {}
lost = {}
end = time.monotonic() + seconds
asked = time.monotonic()
window = time.monotonic() + 10
while time.monotonic() < end:
    now = time.monotonic()
    if now - asked > 1.0:
        sock.sendto(packet(0x100002, bytes(8)), SERVER)
        asked = now
    if now >= window:
        window += 10
        print(time.strftime("%H:%M:%S"), "  ".join(
            "pad %d: %4d lost of %4d" % (slot, lost.get(slot, 0), got[slot] + lost.get(slot, 0)) for slot in sorted(got)),
            flush=True)
        got = {}
        lost = {}
    try:
        data, _ = sock.recvfrom(256)
    except socket.timeout:
        continue
    if data[:4] != b"DSUS" or struct.unpack_from("<I", data, 16)[0] != 0x100002:
        continue
    body = data[20:]
    slot = body[0]
    stamp = struct.unpack_from("<Q", body, 48)[0]
    got[slot] = got.get(slot, 0) + 1
    if slot in last:
        missing = round((stamp - last[slot]) / 5000) - 1
        if missing > 0:
            lost[slot] = lost.get(slot, 0) + missing
    last[slot] = stamp
