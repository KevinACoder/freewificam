#!/usr/bin/env python3
"""Count the four legs of the ICMP exchange from the AP's own capture.

Input is the evidence directory of one `rtw88-round.sh` boot: `icmp-ap.csv` is
the tshark extract of the AP interface's 802.3 capture (field order: ip.src,
ip.dst, icmp.type) taken in parallel with the boot.

Why: on this bench the co-located monitor sees the AP's beacons but not its
unicast data (the 2026-11-14 D1 boot and the GT round's air CSVs are empty), so
"the board lost 8 of 20 pings" could be either the board's request never
reaching the AP or the AP's reply never reaching the board.  The AP's kernel is
the receiver of record for the first, and its own send counter for the second:

    board -> AP request   ip.src=board  icmp.type=8
    AP -> board reply     ip.dst=board  icmp.type=0
    AP -> board request   ip.src=ap     icmp.type=8
    board -> AP reply     ip.dst=ap     icmp.type=0

The same capture also gives the RTT of the board's own ping round (request to
reply at the AP is not the RTT; it is the AP's turnaround), so this script only
counts.

Usage: tools/rtos/icmp-ledger.py <evidence-dir>
"""

from __future__ import annotations

import sys
from pathlib import Path

BOARD = "192.168.99.2"
AP = "192.168.99.1"


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: icmp-ledger.py <evidence-dir>", file=sys.stderr)
        return 2
    ev = Path(argv[1])
    csv = ev / "icmp-ap.csv"
    rows = []
    if csv.is_file():
        for line in csv.read_text(errors="replace").splitlines():
            cols = line.split("\t")
            if len(cols) < 3:
                continue
            rows.append(cols[:3])

    def count(src: str, dst: str, typ: str) -> int:
        return sum(1 for s, d, t in rows if s == src and d == dst and t == typ)

    board_req = count(BOARD, AP, "8")
    ap_rep = count(AP, BOARD, "0")
    ap_req = count(AP, BOARD, "8")
    board_rep = count(BOARD, AP, "0")

    print(f"source=icmp-ap.csv ap_if_rows={len(rows)} board={BOARD} ap={AP}")
    print(f"board->AP request={board_req}")
    print(f"AP->board reply  ={ap_rep}")
    print(f"AP->board request={ap_req}")
    print(f"board->AP reply  ={board_rep}")
    print("reading: the board's own ping is board->AP request / AP->board reply; "
          "the peer's ping is AP->board request / board->AP reply.  A leg whose "
          "count falls short of the other is where that direction's loss is.")
    if board_req and ap_rep < board_req:
        print(f"note: the AP replied to {ap_rep}/{board_req} requests "
              "(the shortfall is the board's uplink or the AP's reply path)")
    if ap_req and board_rep < ap_req:
        print(f"note: the board replied to {board_rep}/{ap_req} requests "
              "(the shortfall is the board's downlink or its reply path)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
