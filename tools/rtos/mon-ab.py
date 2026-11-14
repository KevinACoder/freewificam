#!/usr/bin/env python3
"""Compare the DUT monitor's beacon census with the bench monitor's capture.

The M5 arm of the monitor round (runs/2026-11-14-rtl8xxxu-8188eu-mon) boots the
DUT as an RX-only monitor while the standard bench stays up: lab-ap on the Pi's
8812AU and the Pi's MT7921AU as a monitor on the same channel.  Both monitors
therefore look at the same air at the same time, and the question is whether
the DUT's sink counted the same beacons the independent monitor saw.

Inputs:

  --serial      the DUT boot's serial.log (the case's own anchors)
  --pi-beacons  tshark TSV on the Pi's monitor pcap, one row per beacon:
                frame.time_relative <tab> wlan.bssid <tab> wlan.fixed.beacon

The two sides are compared as *rates*, never as raw counts: the capture window
is longer than the DUT's window on purpose (20 s vs 15 s), so a count ratio
would encode the harness's timing, not the air.  Each side measures beacons
over its own observed span.

Exit 0 only when every gate is true; a missing input is a fail, not a skip.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

WINDOW = re.compile(r"rtl8xxxu_mon: window ms=(\d+)")
SINK = re.compile(r"rtl8xxxu_mon: sink frames=\d+ bytes=\d+ beacons=(\d+) ")
BSSID = re.compile(r"rtl8xxxu_mon: bssid=([0-9a-f:]{17}) ")
BSSBEACONS = re.compile(r"rtl8xxxu_mon: bssbeacons=([0-9a-f:]{17}) "
                        r"beacons=(\d+)")
FLOOR_PER_S = 8.0
RATIO_TOL = 0.25


def read(path: Path) -> str:
    try:
        return path.read_text(errors="replace")
    except OSError:
        return ""


def board_side(serial: Path, want_bssid: str | None) -> dict:
    text = read(serial)
    out: dict = {"window_ms": None, "beacons": None, "bssid": None,
                 "census": {}, "rate": None}
    windows = WINDOW.findall(text)
    if windows:
        out["window_ms"] = int(windows[-1])
    sinks = SINK.findall(text)
    if sinks:
        out["beacons"] = int(sinks[-1])
    bssids = BSSID.findall(text)
    if bssids:
        out["bssid"] = bssids[-1]
    for mac, n in BSSBEACONS.findall(text):
        out["census"][mac.lower()] = int(n)
    # The bench AP's own count when the harness names it, else the channel's
    # total.  The channel total includes neighbours, so it is only a fallback.
    bssid = want_bssid.lower() if want_bssid else out["bssid"]
    out["bssid"] = bssid
    if want_bssid and want_bssid.lower() in out["census"]:
        out["bssid_beacons"] = out["census"][want_bssid.lower()]
    elif not want_bssid:
        out["bssid_beacons"] = out["beacons"]
    else:
        out["bssid_beacons"] = None
    if out["window_ms"] and out["bssid_beacons"] is not None:
        out["rate"] = out["bssid_beacons"] * 1000.0 / out["window_ms"]
    out["channel_rate"] = (
        out["beacons"] * 1000.0 / out["window_ms"]
        if out["window_ms"] and out["beacons"] is not None else None
    )
    return out


def pi_side(tsv: Path, want_bssid: str | None) -> dict:
    rows = []
    for line in read(tsv).replace("\r", "").splitlines():
        cols = line.split("\t")
        if len(cols) < 2:
            continue
        try:
            t = float(cols[0])
        except ValueError:
            continue
        bssid = cols[1].strip().lower()
        interval = cols[2].strip() if len(cols) > 2 else ""
        rows.append((t, bssid, interval))
    out: dict = {"rows": len(rows), "bssid": None, "rate": None,
                 "interval": None, "seen": {}}
    out["seen"] = dict(Counter(r[1] for r in rows).most_common(8))
    if not rows:
        return out
    if want_bssid:
        sel = [r for r in rows if r[1] == want_bssid.lower()]
    else:
        # No DUT BSSID to aim at: the busiest beacon source is the bench AP.
        top = Counter(r[1] for r in rows).most_common(1)[0][0]
        sel = [r for r in rows if r[1] == top]
    if not sel:
        return out
    sel.sort()
    out["bssid"] = sel[0][1]
    span = sel[-1][0] - sel[0][0]
    if span > 0:
        out["rate"] = len(sel) / span
        out["span_s"] = round(span, 3)
    intervals = Counter(r[2] for r in sel if r[2])
    if intervals:
        out["interval"] = intervals.most_common(1)[0][0]
    out["bssid_rows"] = len(sel)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--serial", required=True, type=Path)
    ap.add_argument("--pi-beacons", required=True, type=Path)
    ap.add_argument("--bssid", default=None,
                    help="the bench AP's address (from the AP itself)")
    ap.add_argument("--json", type=Path, default=None)
    args = ap.parse_args()

    board = board_side(args.serial, args.bssid)
    pi = pi_side(args.pi_beacons, board["bssid"])

    gates = {
        "board_beacons_ge_floor": board["rate"] is not None
        and board["rate"] >= FLOOR_PER_S,
        "pi_beacons_ge_floor": pi["rate"] is not None
        and pi["rate"] >= FLOOR_PER_S,
        "same_bssid": board["bssid"] is not None
        and pi["bssid"] is not None
        and board["bssid"].lower() == pi["bssid"].lower(),
        "rate_within_25pct": (
            board["rate"] is not None and pi["rate"] is not None
            and max(board["rate"], pi["rate"]) > 0
            and abs(pi["rate"] - board["rate"]) /
            max(board["rate"], pi["rate"]) <= RATIO_TOL
        ),
    }
    verdict = "pass" if all(gates.values()) else "fail"
    report = {"verdict": verdict, "gates": gates, "board": board, "pi": pi}

    print(f"verdict: {verdict}")
    for name, ok in gates.items():
        print(f"  [{'ok' if ok else 'FAIL'}] {name}")
    if board["rate"] is not None:
        print(f"board  bssid={board['bssid']} beacons={board['bssid_beacons']} "
              f"over {board['window_ms']} ms = {board['rate']:.2f}/s")
    else:
        print(f"board  no census for {board['bssid']} "
              f"(census={board['census']})")
    if board["channel_rate"] is not None:
        print(f"board  whole channel = {board['channel_rate']:.2f}/s "
              f"({board['beacons']} beacons over {board['window_ms']} ms)")
    if pi["rate"] is not None:
        print(f"pi     bssid={pi['bssid']} beacons={pi.get('bssid_rows')} "
              f"over {pi.get('span_s')} s = {pi['rate']:.2f}/s "
              f"interval={pi['interval']} seen={pi['seen']}")
    else:
        print(f"pi     no beacon rows (rows={pi['rows']} seen={pi['seen']})")
    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + "\n")
    return 0 if verdict == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
