#!/usr/bin/env python3
"""Summarise the monitor capture's 802.11 view for one throughput arm.

The input is the compact CSV tools/rtos/c6-throughput.sh extracts on the Pi:

    wlan.fc.type_subtype, wlan.fc.retry, wlan_radio.11n.mcs_index,
    radiotap.ampdu.flags, frame.len

one file per direction (`air-fields-down.csv`, `air-fields-up.csv`).  The tool
does NOT claim byte truth from the monitor: the MTK monitor is not the receiver
and cannot see every A-MPDU member.  It reports the shape the round compares
between arms - BlockAck control frames, ADDBA action frames, retry fraction,
A-MPDU-flagged frames and the MCS histogram - and says `instrument=monitor-
sample` so a reader knows what the numbers are.

Usage: tools/rtos/c6-air.py DIR [DIR2 ...]
"""

from __future__ import annotations

import json
import sys
from collections import Counter
from pathlib import Path

SUBTYPE_BA = {"0x0019", "25"}          # BlockAck control frame
SUBTYPE_BAR = {"0x0018", "24"}         # BlockAckReq
SUBTYPE_ADDBA = {"0x000d", "13"}       # Action (ADDBA lives here)
SUBTYPE_QOS_DATA = {"0x0028", "40"}    # QoS Data


def summarise(path: Path) -> dict:
    rows = 0
    ba = bar = addba = qos = 0
    retry = 0
    ampdu = 0
    lengths = 0
    length_sum = 0
    mcs: Counter = Counter()
    for line in path.read_text(errors="replace").splitlines():
        cols = line.split("\t")
        if len(cols) < 5:
            continue
        rows += 1
        subtype, retry_bit, mcs_idx, ampdu_flag, flen = cols[:5]
        if subtype in SUBTYPE_BA:
            ba += 1
        elif subtype in SUBTYPE_BAR:
            bar += 1
        elif subtype in SUBTYPE_ADDBA:
            addba += 1
        elif subtype in SUBTYPE_QOS_DATA:
            qos += 1
        if retry_bit == "1":
            retry += 1
        if ampdu_flag.strip() != "":
            ampdu += 1
        if mcs_idx.strip() != "":
            mcs[int(mcs_idx)] += 1
        try:
            lengths += 1
            length_sum += int(flen)
        except ValueError:
            pass
    return {
        "file": str(path),
        "instrument": "monitor-sample",
        "frames": rows,
        "qos_data": qos,
        "blockack": ba,
        "blockack_req": bar,
        "addba_action": addba,
        "retry": retry,
        "retry_pct": round(100.0 * retry / rows, 2) if rows else None,
        "ampdu_flagged": ampdu,
        "ampdu_pct": round(100.0 * ampdu / rows, 2) if rows else None,
        "mcs_hist": dict(sorted(mcs.items())),
        "len_avg": round(length_sum / lengths, 1) if lengths else None,
    }


def main() -> int:
    dirs = [Path(a) for a in sys.argv[1:]]
    if not dirs:
        print(__doc__, file=sys.stderr)
        return 2
    out = {}
    for d in dirs:
        out[str(d)] = {
            "down": summarise(d / "air-fields-down.csv")
            if (d / "air-fields-down.csv").exists() else None,
            "up": summarise(d / "air-fields-up.csv")
            if (d / "air-fields-up.csv").exists() else None,
        }
    print(json.dumps(out, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
