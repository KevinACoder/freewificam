#!/usr/bin/env python3
"""Fit an eMMC chunk sweep: per-command fixed cost vs per-byte bus rate.

The case prints one line per chunk size and direction:

    emmc: sweep write chunk=128 KiB xfers=32 bytes=4194304 us=651234 kibs=6441

A transfer's cost is  t(size) = C + size / BW  - a fixed part C (command issue,
the data-circuit reset, the DMA programming, the wait loop's step) and the data
leg's own wire time.  Rearranged,  1/rate = C / size + 1 / BW,  so a straight
line through (1/size, 1/rate) has slope C (microseconds per command) and
intercept 1/BW.  One number per size cannot separate the two; a curve can.

Usage:
    tools/rtos/fit-sweep.py <serial.log> [<serial.log> ...]
    tools/rtos/fit-sweep.py --json fit.json runs/.../b1/serial.log

This is a measurement aid, not a gate: it prints the fit and the per-size table
and leaves the verdict to the ledger.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from pathlib import Path

LINE = re.compile(
    r"emmc: sweep (?P<dir>read|write) chunk=(?P<kib>\d+) KiB "
    r"xfers=(?P<xfers>\d+) bytes=(?P<bytes>\d+) us=(?P<us>\d+) "
    r"kibs=(?P<kibs>\d+)"
)

# The Linux leg (gt/linux-truth.py) writes one file per size, named after it:
#   09-emmc-read-4k.log  holding  "4194304 bytes (4.0MB) copied, 0.21 seconds, 19.0MB/s"
# The chunk size comes from the name, so no parsing of busybox's prose is needed
# beyond bytes and seconds.
GT_NAME = re.compile(r"-emmc-(?P<dir>read|write)-(?P<kib>\d+)k\.log$")
DD = re.compile(r"(?P<bytes>\d+) bytes .*? (?P<secs>[\d.]+) seconds")


def collect_gt(directory: Path) -> dict[str, list[dict]]:
    """Linux-side points: same (chunk, bytes, us) shape, from dd's own report."""
    points: dict[str, dict[int, dict]] = {"read": {}, "write": {}}
    for path in sorted(directory.glob("*.log")):
        name = GT_NAME.search(path.name)
        if not name:
            continue
        match = DD.search(path.read_text(errors="replace"))
        if not match:
            continue
        kib = int(name.group("kib"))
        total = int(match.group("bytes"))
        us = int(float(match.group("secs")) * 1e6)
        xfers = max(total // (kib * 1024), 1)
        points[name.group("dir")][kib] = {
            "chunk_kib": kib,
            "xfers": xfers,
            "bytes": total,
            "us": us,
            "per_cmd_us": us / xfers,
            "rate_kibs": total * 1e6 / us / 1024.0,
            "rate_mibs": total * 1e6 / us / (1024.0 * 1024.0),
        }
    return {key: [row for _, row in sorted(value.items())] for key, value in points.items()}


def collect(paths: list[Path]) -> dict[str, list[dict]]:
    """Group the sweep points by direction; a repeated size keeps the last."""
    points: dict[str, dict[int, dict]] = {"read": {}, "write": {}}
    for path in paths:
        text = path.read_text(errors="replace")
        for match in LINE.finditer(text):
            kib = int(match.group("kib"))
            xfers = int(match.group("xfers"))
            us = int(match.group("us"))
            if xfers <= 0 or us <= 0:
                continue
            total = int(match.group("bytes"))   # the line carries the window total
            points[match.group("dir")][kib] = {
                "chunk_kib": kib,
                "xfers": xfers,
                "bytes": int(match.group("bytes")),
                "us": us,
                "per_cmd_us": us / xfers,
                "rate_kibs": total * 1e6 / us / 1024.0,
                "rate_mibs": total * 1e6 / us / (1024.0 * 1024.0),
            }
    return {key: [row for _, row in sorted(value.items())] for key, value in points.items()}


def fit(rows: list[dict]) -> dict | None:
    """Least squares of 1/rate (us per byte) against 1/size (per byte)."""
    if len(rows) < 3:
        return None
    xs = [1.0 / (row["chunk_kib"] * 1024.0) for row in rows]	# per byte
    ys = [row["us"] / row["bytes"] for row in rows]			# us per byte
    n = len(xs)
    mean_x = sum(xs) / n
    mean_y = sum(ys) / n
    sxx = sum((x - mean_x) ** 2 for x in xs)
    if sxx == 0.0:
        return None
    sxy = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys))
    slope = sxy / sxx
    intercept = mean_y - slope * mean_x
    ss_tot = sum((y - mean_y) ** 2 for y in ys)
    ss_res = sum((y - (intercept + slope * x)) ** 2 for x, y in zip(xs, ys))
    r2 = 1.0 - ss_res / ss_tot if ss_tot else 1.0
    return {
        "fixed_cost_us": slope,
        "bandwidth_mibs": (1.0 / intercept) * 1e6 / (1024.0 * 1024.0) if intercept else math.inf,
        "r2": r2,
        "points": n,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("logs", nargs="*", type=Path)
    ap.add_argument("--gt-dir", type=Path, default=None,
                    help="a linux-truth evidence directory (dd lines, one per size)")
    ap.add_argument("--json", type=Path, default=None)
    args = ap.parse_args()

    if not args.logs and args.gt_dir is None:
        ap.error("give a serial.log, or --gt-dir")
    points = collect(args.logs or [])
    if args.gt_dir is not None:
        for direction, rows in collect_gt(args.gt_dir).items():
            for row in rows:
                points[direction].append(row)
        points = {k: sorted(v, key=lambda r: r["chunk_kib"]) for k, v in points.items()}
    report: dict = {"sources": [str(path) for path in args.logs], "directions": {}}

    for direction in ("read", "write"):
        rows = points[direction]
        if not rows:
            continue
        result = fit(rows)
        report["directions"][direction] = {"points": rows, "fit": result}

        print(f"== {direction}")
        print(f"{'chunk':>9} {'xfers':>6} {'us/cmd':>9} {'KiB/s':>9} {'MB/s':>7}")
        for row in rows:
            print(
                f"{row['chunk_kib']:>7} K {row['xfers']:>6} "
                f"{row['per_cmd_us']:>9.1f} {row['rate_kibs']:>9.0f} "
                f"{row['rate_mibs']:>7.2f}"
            )
        if result:
            print(
                f"   fit: C={result['fixed_cost_us']:.1f} us/command, "
                f"BW={result['bandwidth_mibs']:.2f} MiB/s, r2={result['r2']:.3f} "
                f"({result['points']} points)"
            )
        else:
            print("   fit: not enough points")
        print()

    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())