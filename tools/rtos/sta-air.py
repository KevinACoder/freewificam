#!/usr/bin/env python3
"""Summarise the monitor capture's view of the station under test.

Input is the evidence directory of one `rtw88-round.sh` boot: it holds
`air-board.csv`, the tshark extract the runner writes after the boot (field
order: time_relative, wlan.fc.type_subtype, wlan.fc.retry,
wlan_radio.data_rate, wlan_radio.11n.mcs_index, wlan_radio.signal_dbm,
wlan.ta, wlan.ra, frame.len) and `pcap-facts.txt` (the capture's path, hash and
the station MAC the filter used).

Why this instrument exists: the 2026-11-14 EHCI round measured the board at
-48 dBm at the AP (GT, same card/port: -24) while its own TX-power probe read
CCK index 63 / OFDM index 3, and the air capture only counted frame classes.
Three different mechanisms - a wrong per-rate power index, a rate control stuck
at a low OFDM rate, or an RX drop on the board - all look the same from the
AP's single station-dump number.  This summary separates them with the only
place that sees the frames themselves: the monitor.

The tool does NOT claim byte truth: the MTK monitor is not the receiver and
cannot see every A-MPDU member.  It reports the shape the round compares
between arms and labels itself `instrument=monitor-sample`.

Usage: tools/rtos/sta-air.py <evidence-dir>
"""

from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path

# wlan.fc.type_subtype is (type << 4) | subtype in tshark's display form:
# management 0x0X, control 0x1X, data 0x2X (see tools/rtos/air-report.py, which
# already relies on 0x0028 = QoS data and 0x0019 = BlockAck).
NAMES = {
    "0x00": "assoc-req", "0x01": "assoc-resp", "0x02": "reassoc-req",
    "0x03": "reassoc-resp", "0x04": "probe-req", "0x05": "probe-resp",
    "0x08": "beacon", "0x09": "atim", "0x0a": "disassoc", "0x0b": "auth",
    "0x0c": "deauth", "0x0d": "action", "0x0e": "action-noack",
    "0x18": "bar", "0x19": "ba", "0x1a": "ps-poll", "0x1b": "rts",
    "0x1c": "cts", "0x1d": "ack", "0x1e": "cf-end",
    "0x20": "data", "0x24": "null", "0x28": "qos-data", "0x2c": "qos-null",
    "0x29": "qos-data-cfack", "0x30": "data-cfack",
}

# the subtypes that say the station is dozing (PS is on)
PS_SLEEP_SIGNS = {"0x1a", "0x24", "0x2c"}
DATA_LIKE = {"0x20", "0x28", "0x29", "0x30"}


def load(path: Path) -> list[list[str]]:
    rows: list[list[str]] = []
    if not path.is_file():
        return rows
    for line in path.read_text(errors="replace").splitlines():
        cols = line.split("\t")
        if len(cols) < 9:
            continue
        # a tshark error line landed in the same file (the runner merges
        # stderr): keep only rows whose first column is the relative time
        try:
            float(cols[0])
        except ValueError:
            continue
        rows.append(cols)
    return rows


def note(rows: list[list[str]], mac: str) -> dict:
    out = {
        "rows": len(rows),
        "from_board": 0,
        "to_board": 0,
        "subtypes_from": Counter(),
        "subtypes_to": Counter(),
        "retry": 0,
        "data_from": 0,
        "mcs": Counter(),
        "rate": Counter(),
        "rssi": [],
        "len": [],
        "ps_signs": 0,
    }
    for cols in rows:
        _t, subtype, retry_bit, rate, mcs, dbm, ta, ra, flen = cols[:9]
        try:
            length = int(flen)
        except ValueError:
            length = 0
        is_retry = retry_bit.strip() in ("1", "True")
        if is_retry:
            out["retry"] += 1
        if ta == mac and mac:
            out["from_board"] += 1
            out["subtypes_from"][subtype] += 1
            if subtype in PS_SLEEP_SIGNS:
                out["ps_signs"] += 1
            if subtype in DATA_LIKE:
                out["data_from"] += 1
                out["len"].append(length)
                if mcs.strip():
                    out["mcs"][mcs.strip()] += 1
                if rate.strip():
                    try:
                        out["rate"][f"{float(rate):.1f}"] += 1
                    except ValueError:
                        pass
                if dbm.strip():
                    try:
                        out["rssi"].append(float(dbm))
                    except ValueError:
                        pass
        elif ra == mac and mac:
            out["to_board"] += 1
            out["subtypes_to"][subtype] += 1
    return out


def fmt_subtypes(c: Counter) -> str:
    if not c:
        return "-"
    parts = [f"{NAMES.get(k, k)}={v}" for k, v in c.most_common()]
    return ",".join(parts)


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    ev = Path(argv[1])
    facts = ev / "pcap-facts.txt"
    mac = ""
    if facts.is_file():
        for line in facts.read_text(errors="replace").splitlines():
            if line.startswith("pcap="):
                for token in line.split():
                    if token.startswith("sta_mac="):
                        value = token.split("=", 1)[1]
                        mac = "" if value == "unknown" else value
    rows = load(ev / "air-board.csv")
    s = note(rows, mac)

    print(f"instrument=monitor-sample source=air-board.csv rows={s['rows']} "
          f"sta_mac={mac or 'unknown'}")
    print(f"frames from_board={s['from_board']} to_board={s['to_board']} "
          f"retry={s['retry']}")
    print(f"from_board subtypes: {fmt_subtypes(s['subtypes_from'])}")
    print(f"to_board   subtypes: {fmt_subtypes(s['subtypes_to'])}")
    print(f"ps_signs={s['ps_signs']} (ps-poll+null+qos-null from the board: "
          f"non-zero means the station is dozing)")
    if s["data_from"]:
        rssi = s["rssi"]
        rssi_txt = (f"{min(rssi):.0f}/{sum(rssi) / len(rssi):.0f}/{max(rssi):.0f}"
                    if rssi else "none")
        print(f"board data frames={s['data_from']} "
              f"len_min={min(s['len'])} len_max={max(s['len'])} "
              f"rssi_dbm_min/avg/max={rssi_txt}")
        print(f"board mcs histogram: "
              f"{dict(sorted(s['mcs'].items(), key=lambda kv: kv[0])) or '-'}")
        print(f"board rate histogram (Mbit/s): "
              f"{dict(sorted(s['rate'].items(), key=lambda kv: float(kv[0]))) or '-'}")
    else:
        print("board data frames=0 (no data frame from the station in this "
              "capture: the ping round is not in the window)")
    print("reading: GT on this card/port reads -24 dBm at MCS7; a board at "
          "rssi<=-40 with mcs>=4 is a power-index problem, a board at OFDM "
          "rates (no mcs) is a rate-control problem, ps_signs>0 is power save.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
