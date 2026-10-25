#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Raw TCP downlink sender for the RTOS 8189FTV receive sink.

The board runs a plain lwIP TCP server on port 5001 (no iperf3 protocol),
counts received bytes/segments per second, and prints the same clock on the
serial log.  This sender is the independent AP-side witness: it blasts bytes
for N seconds and prints its own per-second count.  A board-side zero-delta
second while this sender still reports progress is the receive-stall
signature.
"""

import argparse
import socket
import sys
import time


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.99.2")
    ap.add_argument("--port", type=int, default=5001)
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--chunk-kb", type=int, default=64)
    ap.add_argument("--connect-wait", type=float, default=90.0)
    args = ap.parse_args()

    chunk = b"x" * max(1, args.chunk_kb) * 1024
    deadline = time.monotonic() + args.connect_wait
    while True:
        try:
            sock = socket.create_connection((args.host, args.port), timeout=2.0)
            break
        except OSError as exc:
            if time.monotonic() >= deadline:
                print(f"tcp-sender: connect failed after "
                      f"{args.connect_wait:g}s: {exc}", flush=True)
                return 1
            print(f"tcp-sender: waiting for {args.host}:{args.port}: {exc}",
                  flush=True)
            time.sleep(1.0)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    sock.settimeout(5.0)
    print(f"tcp-sender: connected {args.host}:{args.port} "
          f"seconds={args.seconds:g} chunk={len(chunk)}", flush=True)

    start = time.monotonic()
    next_tick = start + 1.0
    total = 0
    last = 0
    rc = 0
    while True:
        now = time.monotonic()
        if now - start >= args.seconds:
            break
        try:
            sock.sendall(chunk)
        except socket.timeout:
            elapsed = time.monotonic() - start
            print(f"tcp-sender: send timeout t={elapsed:.1f}s "
                  f"tx={total}", flush=True)
            rc = 2
            break
        except OSError as exc:
            print(f"tcp-sender: send failed t={now - start:.1f}s "
                  f"tx={total}: {exc}", flush=True)
            rc = 1
            break
        total += len(chunk)
        now = time.monotonic()
        while now >= next_tick:
            delta = total - last
            print(f"tcp-sender: t={next_tick - start:.0f} tx={total} "
                  f"delta={delta} mbps={delta * 8.0 / 1e6:.2f}", flush=True)
            last = total
            next_tick += 1.0

    try:
        sock.shutdown(socket.SHUT_WR)
    except OSError:
        pass
    sock.close()
    print(f"tcp-sender: done tx={total} seconds={time.monotonic() - start:.1f} "
          f"mbps={total * 8.0 / max(1e-6, time.monotonic() - start) / 1e6:.2f}",
          flush=True)
    return rc


if __name__ == "__main__":
    sys.exit(main())
