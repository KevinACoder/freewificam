#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Raw UDP downlink sender for the RTOS 8189FTV receive sink.

The board binds a plain lwIP UDP socket on port 5002 and counts delivered
datagrams/bytes per second. This sender is the AP-side witness: it offers a
fixed rate for N seconds and prints its own per-second count. A board zero
second while the sender still reports progress is the receive-path stall
signature -- UDP has no ACK or TCP window to blame.
"""

import argparse
import socket
import sys
import time


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.99.2")
    ap.add_argument("--port", type=int, default=5002)
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--mbps", type=float, default=8.0)
    ap.add_argument("--payload", type=int, default=1400)
    args = ap.parse_args()

    if args.seconds <= 0 or args.mbps <= 0 or args.payload <= 0:
        print("udp-sender: seconds, mbps and payload must be positive",
              flush=True)
        return 2

    payload = b"u" * args.payload
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # A connected UDP socket is fine here: the board's port is fixed and a
    # refused datagram is reported as an error rather than silently ignored.
    sock.connect((args.host, args.port))
    interval = args.payload * 8.0 / (args.mbps * 1e6)
    start = time.monotonic()
    next_tick = start + 1.0
    next_send = start
    total = 0
    pkts = 0
    last = 0
    last_pkts = 0
    print(f"udp-sender: target {args.host}:{args.port} "
          f"seconds={args.seconds:g} mbps={args.mbps:g} "
          f"payload={args.payload}", flush=True)

    while True:
        now = time.monotonic()
        if now - start >= args.seconds:
            break
        if now < next_send:
            time.sleep(next_send - now)
            now = time.monotonic()
        try:
            sock.send(payload)
        except OSError as exc:
            print(f"udp-sender: send failed t={now - start:.1f}s "
                  f"tx={total}: {exc}", flush=True)
            sock.close()
            return 1
        total += len(payload)
        pkts += 1
        next_send = max(next_send + interval, now)
        now = time.monotonic()
        while now >= next_tick:
            delta = total - last
            dpkts = pkts - last_pkts
            print(f"udp-sender: t={next_tick - start:.0f} tx={total} "
                  f"delta={delta} pkts={pkts} dpkts={dpkts} "
                  f"mbps={delta * 8.0 / 1e6:.2f}", flush=True)
            last = total
            last_pkts = pkts
            next_tick += 1.0

    sock.close()
    elapsed = time.monotonic() - start
    print(f"udp-sender: done tx={total} pkts={pkts} seconds={elapsed:.1f} "
          f"mbps={total * 8.0 / max(1e-6, elapsed) / 1e6:.2f}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
