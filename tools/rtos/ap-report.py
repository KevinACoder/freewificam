#!/usr/bin/env python3
"""Compare the GT AP arm with the RTOS AP arm and apply the pre-registered gates.

The pre-registration is runs/2026-11-11-iwl7260-ap/PREREG.md: TCP down and up
each >= 80% of the GT AP arm, UDP 40 Mbit/s downlink loss <= 1%, and the
app-vs-app and air-vs-app sources within 1%/5%.  The topology is the AP round's:
the *board* is the AP, so `down` is AP->STA (the board sends, the Pi receives)
and `up` is STA->AP (the Pi sends, the board receives).  That is the opposite
of the C6 report's board roles with the same phase names; this tool does not
import the station-side report for that reason.

Directory conventions (both produced by ap-round.sh / run-gt-ap.sh):

  run/board/<gt>/gt-ac7260-<gt>/tp-<phase>-board.txt   the GT board's iperf
  run/pi/<gt>/gt-ap-<gt>/peer-<phase>.txt              the Pi's receivers (GT)
  run/pi/<gt>/peer-<phase>.txt                         the Pi's senders (GT)
  run/evidence/<gt>/air-payload-{down,up}.txt          the monitor's IP payload
  run/<rtos>/serial.log                                the RTOS anchors
  run/<rtos>/peer-<phase>.txt                          the Pi's receivers/senders
  run/<rtos>/sink-<phase>.txt                          the raw UDP counting sink
  run/<rtos>/air-payload-{down,up}.txt                 the monitor's IP payload

Usage:
    tools/rtos/ap-report.py --run runs/2026-11-11-iwl7260-ap \
        [--gt-tags gt1,gt2] [--rtos-tags rtos1,rtos2] [--json REPORT.json] \
        [--markdown -]

Exit 0 only when every gate that has data is true; a missing cell is reported
as `missing`, never fabricated.
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from pathlib import Path

MBITS = re.compile(r"(\d+(?:\.\d+)?)\s+Mbits/sec")
BYTES = re.compile(r"(\d+(?:\.\d+)?)\s+(K|M|G)?Bytes")
LOSS = re.compile(r"\((\d+(?:\.\d+)?)%\)")
PHASE_BEGIN = re.compile(
    r"iwl7260_ap: phase \d+ begin proto=\S+ role=\S+ air=(\S+) "
    r"port=\d+ window_ms=\d+ name=(\S+) peer_ms=\d+")
PHASE_REPORT = re.compile(
    r"iwl7260_ap: phase (\d+) report bytes=(\d+) ms=(\d+) kbit=(\d+) "
    r"pkts=(\d+)")
PHASE_NAME = re.compile(
    r"iwl7260_ap: phase (\d+) begin proto=\S+ role=\S+ air=\S+ port=\d+ "
    r"window_ms=\d+ name=(\S+) peer_ms=\d+")

TCP_DOWN = ("tcpdn", "tcpdn2")
UDP_DOWN = ("udpdn", "udpdn2")
TCP_UP = ("tcpup",)
UDP_UP = ("udpup",)


def read(path: Path) -> str:
    try:
        return path.read_text(errors="replace")
    except OSError:
        return ""


def iperf(text: str) -> dict:
    """The last summary of an iperf2 output: Mbit/s, bytes and loss."""
    mbits = MBITS.findall(text)
    mbytes = BYTES.findall(text)
    loss = LOSS.findall(text)
    out: dict = {"mbit": None, "bytes": None, "loss": None}
    if mbits:
        out["mbit"] = float(mbits[-1])
    if mbytes:
        val, unit = mbytes[-1]
        scale = {"K": 1024, "M": 1024 ** 2, "G": 1024 ** 3}.get(unit or "", 1)
        out["bytes"] = int(round(float(val) * scale))
    if loss:
        out["loss"] = float(loss[-1])
    return out


def sink(path: Path) -> dict:
    """The counting sink's JSON line."""
    text = read(path).strip().splitlines()
    for line in reversed(text):
        line = line.strip()
        if line.startswith("{"):
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                continue
    return {}


def _pi_file(d: Path, name: str) -> Path | None:
    """The Pi's files land either beside the serial log or under pi-ap-<tag>/."""
    for p in (d / name, d / f"pi-ap-{d.name}" / name):
        if p.exists():
            return p
    return None


def rtos_boot(run: Path, tag: str) -> dict:
    """One RTOS AP boot: the serial phases, the Pi's files and the air sums."""
    d = run / tag
    serial = read(d / "serial.log")
    phases: dict[str, dict] = {}
    names = {m.group(1): m.group(2) for m in PHASE_NAME.finditer(serial)}
    for m in PHASE_REPORT.finditer(serial):
        name = names.get(m.group(1))
        if name is None:
            continue
        phases[name] = {
            "bytes": int(m.group(2)),
            "ms": int(m.group(3)),
            "kbit": int(m.group(4)),
            "pkts": int(m.group(5)),
        }

    def peer_text(name: str) -> str:
        p = _pi_file(d, name)
        return read(p) if p is not None else ""

    def sink_json(name: str) -> dict:
        p = _pi_file(d, name)
        return sink(p) if p is not None else {}

    pi_tcp_down = [iperf(peer_text(f"peer-{p}.txt")) for p in TCP_DOWN]
    pi_udp_down = [sink_json(f"sink-{p}.txt") for p in UDP_DOWN]
    pi_tcp_up = [iperf(peer_text(f"peer-{p}.txt")) for p in TCP_UP]
    pi_udp_up = [iperf(peer_text(f"peer-{p}.txt")) for p in UDP_UP]

    # down: the board sends; the Pi's server/sink is the receiver.
    down_rates = [x["mbit"] for x in pi_tcp_down if x["mbit"] is not None]
    down_udp_rates = []
    down_udp_loss = []
    down_offered = 0
    down_received = 0
    for name, s in zip(UDP_DOWN, pi_udp_down):
        ph = phases.get(name)
        if ph is None or ph["ms"] == 0:
            continue
        if s.get("bytes") is not None:
            down_udp_rates.append(s["bytes"] * 8.0 / ph["ms"])
            down_received += int(s["bytes"])
            down_offered += ph["bytes"]
            if ph["bytes"] > 0:
                down_udp_loss.append(
                    max(0.0, 100.0 * (1.0 - s["bytes"] / ph["bytes"])))
        else:
            down_udp_rates.append(ph["kbit"])

    # up: the Pi sends; this board's server/sink is the receiver.
    up_rates = []
    up_loss = []
    up_received = 0
    for name in TCP_UP:
        ph = phases.get(name)
        if ph is not None:
            up_rates.append(ph["kbit"])
            up_received += ph["bytes"]
    for name, peer in zip(UDP_UP, pi_udp_up):
        ph = phases.get(name)
        if ph is not None:
            up_rates.append(ph["kbit"])
            up_received += ph["bytes"]
            if peer.get("loss") is not None:
                up_loss.append(peer["loss"])
            elif ph["bytes"] > 0 and peer.get("bytes"):
                up_loss.append(
                    max(0.0, 100.0 * (1.0 - ph["bytes"] / peer["bytes"])))

    down_mbit = _mean(down_rates + down_udp_rates)
    up_mbit = _mean(up_rates)

    # app-vs-app: the board's own count against the peer's.
    down_sent = sum(phases[p]["bytes"] for p in TCP_DOWN + UDP_DOWN
                    if p in phases)
    down_app = _ratio(down_sent, down_received if down_received else
                      sum(x["bytes"] or 0 for x in pi_tcp_down))
    up_app = _ratio(sum(phases[p]["bytes"] for p in TCP_UP + UDP_UP
                        if p in phases),
                    sum((x["bytes"] or 0) for x in pi_tcp_up + pi_udp_up))

    return {
        "tag": tag,
        "phases": phases,
        "tcp_down_mbit": _mean([x["mbit"] for x in pi_tcp_down]),
        "udp_down_mbit": _mean(down_udp_rates),
        "tcp_up_mbit": _mean([phases[p]["kbit"] for p in TCP_UP
                              if p in phases]),
        "udp_up_mbit": _mean([phases[p]["kbit"] for p in UDP_UP
                              if p in phases]),
        "down_mbit": down_mbit,
        "up_mbit": up_mbit,
        "udp_down_loss": max(down_udp_loss) if down_udp_loss else None,
        "udp_up_loss": max(up_loss) if up_loss else None,
        "app_vs_app_down_pct": down_app,
        "app_vs_app_up_pct": up_app,
        "air_down_bytes": _air(d / "air-payload-down.txt"),
        "air_up_bytes": _air(d / "air-payload-up.txt"),
        "receiver_down_bytes": down_received or None,
        "receiver_up_bytes": up_received or None,
        "valid": bool(phases),
    }


def gt_boot(run: Path, tag: str) -> dict:
    """One GT AP boot: the board's iperf outputs and the Pi's peer files."""
    bd = run / "board" / tag / f"gt-ac7260-{tag}"
    pd = run / "pi" / tag
    ed = run / "evidence" / tag

    def peer(name: str) -> dict:
        for p in (pd / f"peer-{name}.txt", pd / f"gt-ap-{tag}" / f"peer-{name}.txt"):
            if p.exists():
                return iperf(read(p))
        return {}

    board = {name: iperf(read(bd / f"tp-{name}-board.txt"))
             for name in TCP_DOWN + UDP_DOWN + TCP_UP + UDP_UP}
    pe = {name: peer(name) for name in TCP_DOWN + UDP_DOWN + TCP_UP + UDP_UP}

    down_rates = [pe[p]["mbit"] for p in TCP_DOWN if pe[p]["mbit"] is not None]
    down_udp = [pe[p] for p in UDP_DOWN]
    down_rates += [u["mbit"] for u in down_udp if u["mbit"] is not None]
    up_rates = [board[p]["mbit"] for p in TCP_UP
                if board[p]["mbit"] is not None]
    up_udp = [board[p] for p in UDP_UP]
    up_rates += [u["mbit"] for u in up_udp if u["mbit"] is not None]

    down_loss = [u["loss"] for u in down_udp if u["loss"] is not None]
    up_loss = [u["loss"] for u in up_udp if u["loss"] is not None]

    down_sent = sum(board[p]["bytes"] or 0 for p in TCP_DOWN + UDP_DOWN)
    down_recv = sum(pe[p]["bytes"] or 0 for p in TCP_DOWN + UDP_DOWN)
    up_recv = sum(board[p]["bytes"] or 0 for p in TCP_UP + UDP_UP)
    up_sent = sum(pe[p]["bytes"] or 0 for p in TCP_UP + UDP_UP)

    return {
        "tag": tag,
        "tcp_down_mbit": _mean([pe[p]["mbit"] for p in TCP_DOWN]),
        "udp_down_mbit": _mean([u["mbit"] for u in down_udp]),
        "tcp_up_mbit": _mean([board[p]["mbit"] for p in TCP_UP]),
        "udp_up_mbit": _mean([u["mbit"] for u in up_udp]),
        "down_mbit": _mean(down_rates),
        "up_mbit": _mean(up_rates),
        "udp_down_loss": max(down_loss) if down_loss else None,
        "udp_up_loss": max(up_loss) if up_loss else None,
        "app_vs_app_down_pct": _ratio(down_sent, down_recv),
        "app_vs_app_up_pct": _ratio(up_sent, up_recv),
        "air_down_bytes": _air(ed / "air-payload-down.txt"),
        "air_up_bytes": _air(ed / "air-payload-up.txt"),
        "receiver_down_bytes": down_recv or None,
        "receiver_up_bytes": up_recv or None,
        "valid": any(v.get("mbit") is not None for v in board.values()),
    }


def _mean(values) -> float | None:
    vals = [v for v in values if v is not None]
    return round(statistics.fmean(vals), 3) if vals else None


def _ratio(a, b) -> float | None:
    if not b:
        return None
    return round(100.0 * abs(a - b) / b, 4)


def _air(path: Path) -> int | None:
    text = read(path)
    total = 0
    seen = False
    for line in text.splitlines():
        for key in ("tcp_bytes=", "udp_bytes="):
            pos = line.find(key)
            if pos >= 0:
                try:
                    total += int(line[pos + len(key):].split()[0])
                    seen = True
                except (ValueError, IndexError):
                    pass
    return total if seen else None


def aggregate(boots: list[dict], field: str, mode: str = "mean") -> float | None:
    vals = [b[field] for b in boots if b.get(field) is not None]
    if not vals:
        return None
    if mode == "max":
        return round(max(vals), 4)
    return _mean(vals)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", required=True)
    ap.add_argument("--gt-tags", default="gt1,gt2")
    ap.add_argument("--rtos-tags", default="rtos1,rtos2")
    ap.add_argument("--json", default=None)
    ap.add_argument("--markdown", default=None)
    args = ap.parse_args()

    run = Path(args.run)
    gt = [gt_boot(run, t) for t in args.gt_tags.split(",") if t]
    rtos = [rtos_boot(run, t) for t in args.rtos_tags.split(",") if t]

    gt_v = [b for b in gt if b["valid"]]
    rtos_v = [b for b in rtos if b["valid"]]

    gt_agg = {
        "tcp_down": aggregate(gt_v, "tcp_down_mbit"),
        "tcp_up": aggregate(gt_v, "tcp_up_mbit"),
        "udp_down": aggregate(gt_v, "udp_down_mbit"),
        "udp_up": aggregate(gt_v, "udp_up_mbit"),
        "udp_down_loss": aggregate(gt_v, "udp_down_loss", "max"),
    }
    rtos_agg = {
        "tcp_down": aggregate(rtos_v, "tcp_down_mbit"),
        "tcp_up": aggregate(rtos_v, "tcp_up_mbit"),
        "udp_down": aggregate(rtos_v, "udp_down_mbit"),
        "udp_up": aggregate(rtos_v, "udp_up_mbit"),
        "udp_down_loss": aggregate(rtos_v, "udp_down_loss", "max"),
    }

    def pct(rt: float | None, g: float | None) -> float | None:
        if rt is None or not g:
            return None
        return round(100.0 * rt / g, 2)

    ratios = {
        "tcp_down": pct(rtos_agg["tcp_down"], gt_agg["tcp_down"]),
        "tcp_up": pct(rtos_agg["tcp_up"], gt_agg["tcp_up"]),
        "udp_down": pct(rtos_agg["udp_down"], gt_agg["udp_down"]),
    }

    # Air: the monitor's IP payload against the receiver's app bytes, over the
    # two-phase window.  Reported, and a gate when the instrument produced it.
    air = {}
    air_note = None
    for direction in ("down", "up"):
        air_bytes = sum(b[f"air_{direction}_bytes"] or 0 for b in rtos_v)
        app_bytes = sum(b[f"receiver_{direction}_bytes"] or 0 for b in rtos_v)
        coverage = (air_bytes / app_bytes) if app_bytes else None
        # A monitor that dissects 0.2% of the payload is not a byte source:
        # at 80 Mbit/s the 8812AU sees A-MPDU members as truncated/short frames
        # (C6's ADR already says it cannot see every member).  Below half the
        # app bytes the delta is reported, never gated.
        usable = coverage is not None and coverage >= 0.5
        air[direction] = {
            "air_bytes": air_bytes or None,
            "app_bytes": app_bytes or None,
            "coverage": round(coverage, 4) if coverage is not None else None,
            "delta_pct": _ratio(air_bytes, app_bytes) if usable else None,
        }
        if coverage is not None and not usable and air_note is None:
            air_note = ("monitor payload coverage %.1f%%: the A-MPDU frames are "
                        "not fully dissected, so the air source is reference "
                        "only" % (100.0 * coverage))

    app_down = aggregate(rtos_v, "app_vs_app_down_pct", "max")
    app_up = aggregate(rtos_v, "app_vs_app_up_pct", "max")

    gates = {
        "tcp_down_ge_80pct": (ratios["tcp_down"] is not None and
                              ratios["tcp_down"] >= 80.0),
        "tcp_up_ge_80pct": (ratios["tcp_up"] is not None and
                            ratios["tcp_up"] >= 80.0),
        "udp_down_loss_le_1pct": (rtos_agg["udp_down_loss"] is not None and
                                  rtos_agg["udp_down_loss"] <= 1.0),
        "app_vs_app_le_1pct": (
            app_down is not None and app_up is not None and
            app_down <= 1.0 and app_up <= 1.0),
        "air_within_5pct": (
            None if air["down"]["delta_pct"] is None else
            (air["down"]["delta_pct"] <= 5.0 and
             air["up"]["delta_pct"] is not None and
             air["up"]["delta_pct"] <= 5.0)),
    }
    mandatory = ("tcp_down_ge_80pct", "tcp_up_ge_80pct",
                 "udp_down_loss_le_1pct", "app_vs_app_le_1pct")
    verdict = "pass" if all(gates[g] for g in mandatory) else "fail"
    if not gt_v or not rtos_v:
        verdict = "missing"

    report = {
        "verdict": verdict,
        "topology": "board=AP, pi=STA; down=AP->STA (pi receives), "
                    "up=STA->AP (board receives)",
        "rule": "per-direction mean over valid boots; UDP loss max",
        "gates": gates,
        "gt": {"aggregate": gt_agg, "boots": gt},
        "rtos": {"aggregate": rtos_agg, "boots": rtos},
        "ratios_pct": ratios,
        "app_vs_app_pct": {"down": app_down, "up": app_up},
        "air": air,
        "air_note": air_note,
    }

    text = json.dumps(report, indent=2)
    if args.json:
        Path(args.json).write_text(text + "\n")
    if args.markdown:
        print(markdown(report), file=sys.stdout if args.markdown == "-"
              else open(args.markdown, "w"))
    if not args.markdown:
        print(text)
    return 0 if verdict == "pass" else 1


def markdown(r: dict) -> str:
    g = r["gates"]
    lines = [
        "# AP arm comparison",
        "",
        f"- verdict: **{r['verdict']}**",
        f"- topology: {r['topology']}",
        "",
        "| direction | GT (Mbit/s) | RTOS (Mbit/s) | ratio |",
        "|---|---|---|---|",
    ]
    names = {"tcp_down": "TCP down (AP->STA)",
             "tcp_up": "TCP up (STA->AP)",
             "udp_down": "UDP down 40M"}
    for key, label in names.items():
        gv = r["gt"]["aggregate"].get(key)
        rv = r["rtos"]["aggregate"].get(key)
        ratio = r["ratios_pct"].get(key)
        lines.append(
            f"| {label} | {gv if gv is not None else 'missing'} | "
            f"{rv if rv is not None else 'missing'} | "
            f"{str(ratio) + '%' if ratio is not None else 'missing'} |")
    lines += [
        "",
        f"- UDP down loss (max): RTOS "
        f"{r['rtos']['aggregate']['udp_down_loss']}%, GT "
        f"{r['gt']['aggregate']['udp_down_loss']}%",
        f"- app-vs-app: down {r['app_vs_app_pct']['down']}%, "
        f"up {r['app_vs_app_pct']['up']}%",
        f"- air-vs-app: down {r['air']['down']['delta_pct']}% "
        f"(coverage {r['air']['down']['coverage']}), up "
        f"{r['air']['up']['delta_pct']}% "
        f"(coverage {r['air']['up']['coverage']})",
        f"- air note: {r.get('air_note') or 'usable'}",
        "",
        "| gate | value |",
        "|---|---|",
    ]
    for k, v in g.items():
        lines.append(f"| {k} | {v} |")
    for arm in ("gt", "rtos"):
        lines.append("")
        lines.append(f"## {arm} boots")
        for b in r[arm]["boots"]:
            lines.append(
                f"- {b['tag']}: tcp_down={b['tcp_down_mbit']} "
                f"tcp_up={b['tcp_up_mbit']} "
                f"udp_down={b['udp_down_mbit']} "
                f"udp_down_loss={b['udp_down_loss']} valid={b['valid']}")
    return "\n".join(lines)


if __name__ == "__main__":
    sys.exit(main())
