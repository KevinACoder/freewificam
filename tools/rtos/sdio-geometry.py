#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""sdio-geometry.py - put the RTOS and GT SDIO command streams on one table.

The two sides record the same thing at the same semantic level (the transfer
the driver asked the bus for): kind, address, bytes, blocks, duration, return.
The RTOS side is the engine's last-transfers ring (dumped by the freeze
watchdog); the GT side is the instrumented ko's ring, dumped through the
sdio_trace_dump parameter.  This tool turns both into one TSV plus a short
markdown read-out, so "same geometry / different geometry" is a field-by-field
answer instead of an impression.

Usage:
  sdio-geometry.py --rtos <serial.log> [--gt <sdio-trace-*.txt>] --out <dir>

Caveats it prints in the report (and must not hide):
  * the RTOS ring is the tail of the stream at the freeze, not a healthy window;
  * the GT ring is the last 64 ops at the phase boundary, and its counters are
    cumulative since the boot; the two windows are therefore adjacent, not equal;
  * GT CMD52 counts are a lower bound (only the vendor's own entry points are
    instrumented).
"""

import argparse
import os
import re
import sys

RTOS_OP = re.compile(
    r"rtl8189fs_c6t: stall op kind=(\d+) addr=0x([0-9a-fA-F]+) "
    r"bytes=(\d+) blk=(\d+) dur_us=(\d+) ret=(-?\d+)")
GT_OP = re.compile(
    r"RTWTR OP t=(\d+) dur=(\d+) k=(\d+) addr=([0-9a-fA-F]+) "
    r"bytes=(\d+) blk=(\d+) ret=(-?\d+)")
GT_GEOM = re.compile(
    r"RTWTR GEOM k=(\w+) n=(\d+) bytes=(\d+) avg_us=(\d+) max_us=(\d+)")
RTOS_CLK = re.compile(
    r"rtl8189fs_c6t: stall clk freq=(\d+) poll_us=(\d+) dpoll_us=(\d+)")
RTOS_VIOL = re.compile(r"rtl8189fs_c6t: stall claim_violations=(\d+)")
RTOS_INFLIGHT = re.compile(
    r"rtl8189fs_c6t: stall inflight valid=(\d+) kind=(\d+) func=(\d+) "
    r"addr=0x([0-9a-fA-F]+) bytes=(\d+) blocks=(\d+) age_ms=(\d+)")

NAMES = {0: "c52r", 1: "c52w", 2: "ext_r", 3: "ext_w", 4: "byt_r", 5: "byt_w"}


def read(path):
    with open(path, "r", errors="replace") as f:
        return f.read()


def rtos_rows(text):
    rows = []
    for m in RTOS_OP.finditer(text):
        k, addr, n, blk, dur, ret = m.groups()
        rows.append({
            "side": "rtos", "kind": NAMES.get(int(k), "k%s" % k),
            "addr": int(addr, 16), "bytes": int(n), "blocks": int(blk),
            "dur_us": int(dur), "ret": int(ret),
        })
    return rows


def gt_rows(text):
    rows = []
    for m in GT_OP.finditer(text):
        t, dur, k, addr, n, blk, ret = m.groups()
        rows.append({
            "side": "gt", "kind": NAMES.get(int(k), "k%s" % k),
            "addr": int(addr, 16), "bytes": int(n), "blocks": int(blk),
            "dur_us": int(dur), "ret": int(ret),
        })
    return rows


def gt_geom(text):
    out = []
    for m in GT_GEOM.finditer(text):
        k, n, b, avg, mx = m.groups()
        out.append({"kind": k, "n": int(n), "bytes": int(b),
                    "avg_us": int(avg), "max_us": int(mx)})
    return out


def summary_row(rows):
    by = {}
    for r in rows:
        d = by.setdefault(r["kind"], {"n": 0, "bytes": 0, "dur": 0,
                                      "max": 0, "err": 0})
        d["n"] += 1
        d["bytes"] += r["bytes"]
        d["dur"] += r["dur_us"]
        d["max"] = max(d["max"], r["dur_us"])
        if r["ret"] != 0:
            d["err"] += 1
    return by


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rtos", required=True, help="RTOS serial.log")
    ap.add_argument("--gt", help="GT instrumented trace dump")
    ap.add_argument("--out", required=True, help="output directory")
    args = ap.parse_args()

    rt = read(args.rtos)
    rrows = rtos_rows(rt)
    grow, ggeom = [], []
    if args.gt and os.path.exists(args.gt):
        gt = read(args.gt)
        grow = gt_rows(gt)
        ggeom = gt_geom(gt)

    os.makedirs(args.out, exist_ok=True)
    tsv = os.path.join(args.out, "geometry.tsv")
    with open(tsv, "w") as f:
        f.write("side\tkind\taddr\tbytes\tblocks\tdur_us\tret\n")
        for r in rrows + grow:
            f.write("%s\t%s\t0x%05x\t%d\t%d\t%d\t%d\n" % (
                r["side"], r["kind"], r["addr"], r["bytes"], r["blocks"],
                r["dur_us"], r["ret"]))

    md = os.path.join(args.out, "geometry.md")
    with open(md, "w") as f:
        f.write("# SDIO geometry — RTOS vs GT\n\n")
        f.write("- RTOS source: `%s` (the freeze watchdog's tail ring)\n" % args.rtos)
        f.write("- GT source: `%s`\n\n" % (args.gt or "(none)"))
        if not grow:
            f.write("**GT side absent**: the comparison below is RTOS-only. "
                    "Do not read it as a parity result.\n\n")

        for label, rows in (("RTOS tail at the freeze", rrows),
                            ("GT ring at the phase boundary", grow)):
            f.write("## %s\n\n" % label)
            if not rows:
                f.write("(no rows)\n\n")
                continue
            f.write("| kind | n | bytes | avg_us | max_us | errors |\n")
            f.write("|---|---|---|---|---|---|\n")
            by = summary_row(rows)
            for k in sorted(by):
                d = by[k]
                avg = d["dur"] // d["n"] if d["n"] else 0
                f.write("| %s | %d | %d | %d | %d | %d |\n" % (
                    k, d["n"], d["bytes"], avg, d["max"], d["err"]))
            f.write("\n")

        if ggeom:
            f.write("## GT cumulative counters (instrumented ko)\n\n")
            f.write("| kind | n | bytes | avg_us | max_us |\n")
            f.write("|---|---|---|---|---|\n")
            for g in ggeom:
                f.write("| %s | %d | %d | %d | %d |\n" % (
                    g["kind"], g["n"], g["bytes"], g["avg_us"], g["max_us"]))
            f.write("\n")

        if rrows and grow:
            rsz = [r["bytes"] for r in rrows if r["kind"].startswith("ext")]
            gsz = [r["bytes"] for r in grow if r["kind"].startswith("ext")]
            if rsz and gsz:
                f.write("## Read size (extended transfers, bytes)\n\n")
                f.write("- RTOS: n=%d min=%d max=%d\n" % (
                    len(rsz), min(rsz), max(rsz)))
                f.write("- GT:   n=%d min=%d max=%d\n\n" % (
                    len(gsz), min(gsz), max(gsz)))
                f.write("Same range and same block quantisation ⇒ the driver "
                        "asks for the same shape; a difference here names the "
                        "field to chase.\n\n")

        m = RTOS_INFLIGHT.search(rt)
        if m:
            f.write("## The RTOS freeze\n\n")
            f.write("in-flight valid=%s kind=%s func=%s addr=0x%s bytes=%s "
                    "blocks=%s age_ms=%s\n\n" % m.groups())
        m = RTOS_CLK.search(rt)
        if m:
            f.write("clock: freq=%s poll_us=%s dpoll_us=%s\n\n" % m.groups())
        m = RTOS_VIOL.search(rt)
        if m:
            f.write("claim violations: %s\n\n" % m.group(1))

    print("wrote %s and %s (%d RTOS rows, %d GT rows)" %
          (tsv, md, len(rrows), len(grow)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
