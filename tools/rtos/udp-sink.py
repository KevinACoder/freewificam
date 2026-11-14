#!/usr/bin/env python3
"""A UDP counting sink for the board's blast (runs on the bench Pi).

The RTOS AP arm's downlink probe sends raw 1470-byte datagrams with no iperf2
header, so `iperf -s -u` is the wrong receiver for it.  The GT arm sends iperf2
datagrams, which the same sink also counts as bytes.  Using this one program on
both arms is the point: the 2026-11-14 comparison had the GT arm on iperf and
the RTOS arm on Python, and read the receiver difference as a DUT gap.

Usage (on the Pi):
    python3 udp-sink.py --port 5002 --seconds 35 [--rcvbuf 4194304] \
        [--ready /tmp/x.ready] [--out FILE]

  --rcvbuf   SO_RCVBUF to request (the kernel may double it; both numbers are
             printed, so a loss reading can say which buffer it had).
  --ready    written after bind; the harness waits for it instead of racing
             the board's first datagram.
  --out      the final one-line JSON {bytes, pkts, seconds, rcvbuf}.

stdout carries one cumulative JSON line per second, so the analysis can slice
the phase's flow window from the sink's own clock rather than from a wall
clock that started with the ssh call.
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
    ap.add_argument("--rcvbuf", type=int, default=4194304)
    ap.add_argument("--ready", default=None)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if args.rcvbuf > 0:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, args.rcvbuf)
    applied = sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
    sock.bind(("0.0.0.0", args.port))
    sock.settimeout(0.2)

    if args.ready:
        with open(args.ready, "w") as ready:
            json.dump({"port": args.port, "rcvbuf": applied}, ready)
            ready.write("\n")

    t0 = time.monotonic()
    next_sec = 1.0
    total = 0
    pkts = 0
    buf = bytearray(65535)
    while time.monotonic() - t0 < args.seconds:
        try:
            n, _ = sock.recvfrom_into(buf)
        except socket.timeout:
            continue
        total += n
        pkts += 1
        now = time.monotonic() - t0
        if now >= next_sec:
            print(json.dumps({"t": round(now, 3), "bytes": total,
                              "pkts": pkts}), flush=True)
            next_sec = now + 1.0
    elapsed = time.monotonic() - t0
    sock.close()

    line = json.dumps({"bytes": total, "pkts": pkts,
                       "seconds": round(elapsed, 3),
                       "rcvbuf": applied})
    print(line, flush=True)
    if args.out:
        with open(args.out, "w") as out:
            out.write(line + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
