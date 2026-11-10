#!/usr/bin/env python3
"""Drive the peer host from the board image's own network phase anchors.

The board's wired-network case (`net`) announces each phase before it waits:

    net: phase <n> begin proto=<p> role=<r> port=<port> window_ms=<ms> name=<name>

This tool tails the serial capture the board boot wrote, and answers the phase
with the peer command that phase names.  Nothing is fired on a sleep, so a
phase that never starts cannot be mistaken for one that did.

The delivery plan (the image's default, NET_PHASE_PLAN=0) has two phases:

    down   the board serves; the peer connects as an iperf client
    up     the board connects; the peer must already be serving

so the peer's iperf server is started once at the top of the run and stopped at
the end, and the down phase fires a client.

Usage:
    tools/rtos/peer.py --log runs/.../serial.log \
        --peer pi@192.168.0.17 --board 192.168.0.200

Exit code: 0 when every phase the board announced was answered and the image
reached its verdict line; 1 on a missing answer or the timeout.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

PHASE = re.compile(
    r"net: phase (?P<n>\d+) begin proto=(?P<proto>\w+) role=(?P<role>\w+) "
    r"port=(?P<port>\d+) window_ms=(?P<window>\d+) name=(?P<name>\S+)")
END = re.compile(r"rtos: result (?P<verdict>\w+) cases=(?P<ok>\d+)/(?P<total>\d+)")

SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=6"]


def peer_run(host: str, command: str, log: Path, background: bool) -> subprocess.Popen | subprocess.CompletedProcess:
    line = f"=== peer: {command}"
    with log.open("a") as out:
        out.write(line + "\n")
        out.flush()
        if background:
            return subprocess.Popen(SSH + [host, command],
                                    stdout=out, stderr=subprocess.STDOUT)
        return subprocess.run(SSH + [host, command], stdout=out,
                              stderr=subprocess.STDOUT, text=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--log", required=True, help="serial capture written by the boot")
    ap.add_argument("--peer", default="pi@192.168.0.17", help="ssh target of the peer")
    ap.add_argument("--board", default="192.168.0.200", help="the board's address")
    ap.add_argument("--port", type=int, default=5001, help="the board's lwiperf port")
    ap.add_argument("--wait", type=float, default=60.0,
                    help="seconds to wait for the capture file to appear")
    ap.add_argument("--timeout", type=float, default=240.0,
                    help="seconds before giving up on the verdict line")
    ap.add_argument("--out", default=None, help="peer log (default <log>.peer.log)")
    args = ap.parse_args()

    log = Path(args.log)
    peer_log = Path(args.out) if args.out else log.with_suffix(".peer.log")
    peer_log.parent.mkdir(parents=True, exist_ok=True)

    deadline = time.monotonic() + args.wait
    while not log.exists() and time.monotonic() < deadline:
        time.sleep(0.2)
    if not log.exists():
        print(f"peer: no capture at {log}", file=sys.stderr)
        return 1

    # The up phase needs a listener already running; start it once.
    server = peer_run(args.peer, f"iperf -s -p {args.port}",
                      peer_log, background=True)
    time.sleep(1.0)

    answered: list[str] = []
    verdict: dict | None = None
    offset = 0
    end = time.monotonic() + args.timeout
    pending = ""

    try:
        while time.monotonic() < end:
            data = log.read_text(errors="replace")
            if len(data) <= offset:
                time.sleep(0.2)
                continue
            chunk = data[offset:]
            offset = len(data)
            pending += chunk
            *lines, pending = pending.split("\n")
            for line in lines:
                m = PHASE.search(line)
                if m is None:
                    e = END.search(line)
                    if e is not None:
                        verdict = e.groupdict()
                        break
                    continue
                name = m.group("name")
                if name in answered:
                    continue
                answered.append(name)
                window = int(m.group("window")) / 1000.0
                if name == "down":
                    seconds = max(3, min(int(window) - 2, 10))
                    peer_run(args.peer,
                             f"iperf -c {args.board} -p {m.group('port')} -t {seconds}",
                             peer_log, background=False)
                else:
                    # `up` and every other board-initiated phase: the server
                    # started above is the answer; record that it was seen.
                    with peer_log.open("a") as out:
                        out.write(f"=== peer: phase {name} answered by the running server\n")
            if verdict is not None:
                break
    finally:
        subprocess.run(SSH + [args.peer, f"pkill -f 'iperf -s -p {args.port}'"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        server.wait(timeout=5) if isinstance(server, subprocess.Popen) else None

    summary = {
        "peer": args.peer,
        "board": args.board,
        "phases_answered": answered,
        "verdict": verdict,
    }
    peer_log.with_suffix(".json").write_text(json.dumps(summary, indent=2) + "\n")
    if verdict is None:
        print(f"peer: no verdict line within {args.timeout}s "
              f"(answered: {', '.join(answered) or 'none'})", file=sys.stderr)
        return 1
    if verdict["verdict"] != "pass":
        print(f"peer: board verdict {verdict['verdict']} "
              f"{verdict['ok']}/{verdict['total']}", file=sys.stderr)
        return 1
    print(f"peer: answered {', '.join(answered)}; board verdict pass "
          f"{verdict['ok']}/{verdict['total']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
