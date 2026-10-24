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
            points[match.group("dir")][kib] = {
                "chunk_kib": kib,
                "xfers": xfers,
                "bytes": int(match.group("bytes")),
                "us": us,
                "per_cmd_us": us / xfers,
                "rate_kibs": (int(match.group("bytes")) * xfers) / us * 1000.0,
            }
    return {key: [row for _, row in sorted(value.items())] for key, value in points.items()}


def fit(rows: list[dict]) -> dict | None:
    """Least squares of 1/rate (us per byte) against 1/size (per byte)."""
    if len(rows) < 3:
        return None
    xs = [1.0 / (row["chunk_kib"] * 1024.0) for row in rows]
    ys = [1.0 / (row["rate_kibs"] * 1024.0) * 1e6 for row in rows]  # us per byte
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
        "bandwidth_mibs": 1e6 / intercept / (1024.0 * 1024.0) if intercept else math.inf,
        "r2": r2,
        "points": n,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("logs", nargs="+", type=Path)
    ap.add_argument("--json", type=Path, default=None)
    args = ap.parse_args()

    points = collect(args.logs)
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
                f"{row['rate_kibs'] / 1024.0:>7.2f}"
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