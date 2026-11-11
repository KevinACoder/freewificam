#!/usr/bin/env python3
"""A UDP counting sink for the board's blast (runs on the bench Pi).

The RTOS uplink probe sends raw 1470-byte datagrams with no iperf2 header, so
`iperf -s -u` is the wrong receiver: it parses the payload as its own header and
either reports nonsense or spins.  This sink counts exactly what arrived and
nothing else; the board's own offered bytes are the other half of the pair.

Usage (on the Pi):
    python3 udp-sink.py --port 5001 --seconds 35 [--out FILE]

Print: one JSON line {bytes, pkts, seconds} - the caller subtracts the idle
lead-in itself (the sink is started before the blast so the first datagram is
not lost).
"""

from __future__ import annotations

import argparse
import json
import socket
import time


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=5001)
    ap.add_argument("--seconds", type=float, required=True)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", args.port))
    sock.settimeout(0.2)

    t0 = time.monotonic()
    total = 0
    pkts = 0
    while time.monotonic() - t0 < args.seconds:
        try:
            data, _ = sock.recvfrom(65535)
        except socket.timeout:
            continue
        total += len(data)
        pkts += 1
    elapsed = time.monotonic() - t0
    sock.close()

    line = json.dumps({"bytes": total, "pkts": pkts,
                       "seconds": round(elapsed, 3)})
    print(line)
    if args.out:
        with open(args.out, "w") as out:
            out.write(line + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
