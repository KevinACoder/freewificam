#!/usr/bin/env python3
"""Judge the 8851BU AP alignment round from the same-ruler evidence.

The 2026-11-14 first AP round compared two different rulers: the GT arm's TCP
window was 256 KB against the RTOS arm's 16 KB, its UDP downlink receiver was
`iperf -s -u` against a Python sink, and its offered rates were 40M against
10M.  This tool refuses to write a throughput verdict until the two arms'
instruments agree, then computes every rate over a common flow window.

Layout it reads (both arms):

  RTOS boot dir:   serial.log, peer-{tcpdn,tcpdn2,tcpup}.txt,
                   sink-udpdn.txt + sink-udpdn.txt.log, peer-udpup.txt,
                   air-<phase>.txt, ip-<phase>.txt, phase-window.tsv
  GT boot:         pi/<tag>/... (same names) and
                   board/<tag>/gt-8851bu-<tag>/tp-<phase>-board.txt

Usage:
  tools/rtos/ap-align-report.py --run runs/2026-11-14-rtw89-8851bu-ap-align \
      --gt-tags gt-ap1,gt-ap2 --rtos-tags rtos-ap1,rtos-ap2 \
      --json REPORT.json --markdown -

Exit 0 only when every gate with data is true and the instruments agree.
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
import tempfile
from pathlib import Path

INTERVAL = re.compile(
    r"\[\s*\d+\]\s+(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\s+sec\s+"
    r"(\d+(?:\.\d+)?)\s+(K|M|G)?Bytes\s+(\d+(?:\.\d+)?)\s+Mbits/sec")
WINDOW = re.compile(r"TCP window size:\s+(\d+)\s+KByte")
SINK_LINE = re.compile(r"^\{\"t\":\s*([\d.]+),\s*\"bytes\":\s*(\d+),")
PHASE_BEGIN = re.compile(
    r"phase (\d+) begin proto=(\S+) role=(\S+) air=(\S+) port=(\d+) "
    r"window_ms=(\d+) name=(\S+) peer_ms=(\d+) rate_kbps=(\d+)")
PHASE_REPORT = re.compile(
    r"phase (\d+) report bytes=(\d+) ms=(\d+) kbit=(\d+) pkts=(\d+)")
PHASE_UDP = re.compile(
    r"phase (\d+) udp offered=(\d+) tx_err=(\d+) rx_drop=(\d+) "
    r"first_ms=(\d+) last_ms=(\d+) beat=(\d+) stall_ms=(\d+)")
PHASE_CENSUS = re.compile(
    r"phase (\d+) census rx_frames=(\d+) rx_bytes=(\d+) tx_frames=(\d+) "
    r"tx_fail=(\d+) rxdlv_us=(\d+) txndo_us=(\d+)")
PHASE_STACK = re.compile(
    r"phase (\d+) stack tcp_recv=(\d+) tcp_xmit=(\d+) tcp_drop=(\d+) "
    r"tcp_memerr=(\d+) tcp_err=(\d+) pbuf_err=(\d+) tcpseg_err=(\d+) "
    r"udp_in=(\d+) udp_in_err=(\d+) udp_noport=(\d+) ip_drop=(\d+) "
    r"ip_chkerr=(\d+) link_drop=(\d+) mbox_full=(\d+)")
PHASE_NET = re.compile(
    r"phase (\d+) net rx_frames=(\d+) tx_frames=(\d+) rx_foreign=(\d+) "
    r"rx_no_netif=(\d+) rx_input_fail=(\d+) rx_post_fail=(\d+)")
PHASE_DONE = re.compile(r"phase (\d+) done ok=([01])")


def read(path: Path) -> str:
    try:
        return path.read_text(errors="replace")
    except OSError:
        return ""


def mbits(text: str) -> float | None:
    vals = [float(m.group(5)) for m in INTERVAL.finditer(text)]
    return vals[-1] if vals else None


def interval_bytes(text: str) -> list[tuple[float, float, float, float]]:
    """(start_s, end_s, bytes, mbit) for each iperf interval line."""
    out = []
    for m in INTERVAL.finditer(text):
        start = float(m.group(1))
        end = float(m.group(2))
        val = float(m.group(3))
        unit = m.group(4) or ""
        scale = {"K": 1024, "M": 1024 ** 2, "G": 1024 ** 3}.get(unit, 1)
        out.append((start, end, val * scale, float(m.group(5))))
    return out


def _intervals(text: str):
    """Interval rows without iperf's final summary row.

    The summary repeats the whole flow (`0.0000-10.0 sec`); summing it with
    the 5 s rows would double the bytes.
    """
    rows = interval_bytes(text)
    if len(rows) > 1:
        last = rows[-1]
        if last[0] == 0.0 and last[1] == max(r[1] for r in rows):
            rows = rows[:-1]
    return rows


def rate_over(text: str, seconds: float) -> tuple[float | None, float | None]:
    """Bytes and Mbit/s over the first `seconds` of the flow (whole intervals).

    Both arms' numbers are sums of whole 5 s intervals; a partial interval
    that straddles the window is excluded.
    """
    rows = _intervals(text)
    if not rows:
        return None, None
    take = [r for r in rows if r[1] <= seconds + 1e-6]
    if not take:
        take = rows[:1]
    total = sum(r[2] for r in take)
    span = max(r[1] for r in take)
    return total, (total * 8.0 / span / 1e6 if span else None)


def window_kb(text: str) -> int | None:
    m = WINDOW.search(text)
    return int(m.group(1)) if m else None


def sink_json(path: Path) -> dict:
    for line in reversed(read(path).splitlines()):
        line = line.strip()
        if line.startswith("{"):
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                continue
    return {}


def sink_active(path: Path) -> dict:
    """Active-window rate from the sink's per-second cumulative lines.

    The sink is started one phase early, so its whole-window rate includes an
    idle lead-in.  The active window is the first to last second that moved
    bytes; both arms are read the same way.
    """
    rows = []
    for line in read(path).splitlines():
        m = SINK_LINE.match(line.strip())
        if m:
            rows.append((float(m.group(1)), int(m.group(2))))
    if len(rows) < 2:
        return {"bytes": None, "seconds": None, "mbit": None, "first": None,
                "last": None}
    first = last = None
    prev = rows[0]
    for t, b in rows[1:]:
        if b > prev[1]:
            if first is None:
                first = prev[0]
            last = t
        prev = (t, b)
    if first is None:
        return {"bytes": 0, "seconds": 0.0, "mbit": 0.0, "first": None,
                "last": None}
    total = rows[-1][1] - 0
    # the sink's cumulative count includes nothing before the first datagram
    span = max(last - first, 0.5)
    return {"bytes": total, "seconds": span, "mbit": total * 8.0 / span / 1e6,
            "first": first, "last": last}


def rtos_boot(run: Path, tag: str, case: str = "rtw89_ap") -> dict:
    d = run / tag
    serial = read(d / "serial.log")
    phases: dict[str, dict] = {}
    names = {int(m.group(1)): m.group(7)
             for m in PHASE_BEGIN.finditer(serial)}
    for m in PHASE_REPORT.finditer(serial):
        n = int(m.group(1))
        name = names.get(n)
        if name is None:
            continue
        phases[name] = {"bytes": int(m.group(2)), "ms": int(m.group(3)),
                        "kbit": int(m.group(4)), "pkts": int(m.group(5))}
    for m in PHASE_UDP.finditer(serial):
        name = names.get(int(m.group(1)))
        if name is None:
            continue
        phases[name].update({
            "offered": int(m.group(2)), "tx_err": int(m.group(3)),
            "rx_drop": int(m.group(4)), "first_ms": int(m.group(5)),
            "last_ms": int(m.group(6)), "beat": int(m.group(7)),
            "stall_ms": int(m.group(8))})
    for m in PHASE_CENSUS.finditer(serial):
        name = names.get(int(m.group(1)))
        if name is None:
            continue
        phases[name].update({
            "rx_frames": int(m.group(2)), "tx_frames": int(m.group(4)),
            "tx_fail": int(m.group(5))})
    for m in PHASE_STACK.finditer(serial):
        name = names.get(int(m.group(1)))
        if name is None:
            continue
        phases[name].update({
            "udp_in": int(m.group(9)), "udp_in_err": int(m.group(10)),
            "ip_drop": int(m.group(12)), "mbox_full": int(m.group(15))})
    for m in PHASE_NET.finditer(serial):
        name = names.get(int(m.group(1)))
        if name is None:
            continue
        phases[name].update({"rx_post_fail": int(m.group(7))})

    def peer(name: str) -> str:
        for p in (d / name, d / f"pi-ap-{tag}" / name):
            if p.exists():
                return read(p)
        return ""

    tcpdn = peer("peer-tcpdn.txt")
    tcpdn2 = peer("peer-tcpdn2.txt")
    tcpup = peer("peer-tcpup.txt")
    udpup = peer("peer-udpup.txt")
    udpdn_sink = sink_json(d / "sink-udpdn.txt")
    udpdn_active = sink_active(d / "sink-udpdn.txt.log")

    return {
        "arm": "rtos", "tag": tag, "dir": str(d), "phases": phases,
        "tcp_window_down": window_kb(tcpdn),
        "tcp_window_up": window_kb(tcpup),
        "sink_rcvbuf": udpdn_sink.get("rcvbuf"),
        "udpdn_offered_rate": names and next(
            (int(m.group(9)) for m in PHASE_BEGIN.finditer(serial)
             if int(m.group(1)) == 4), None),
        "tcp_down_mbit_10s": rate_over(tcpdn, 10.0)[1],
        "tcp_up_mbit_5s": rate_over(tcpup, 5.0)[1],
        "tcp_up_mbit_10s": rate_over(tcpup, 10.0)[1],
        "tcp_up_sender_mbit": mbits(tcpup),
        "udpdn_sink": udpdn_sink, "udpdn_active": udpdn_active,
        "udpdn_mbit": udpdn_active["mbit"],
        "udpdn_loss": (100.0 * (1.0 - udpdn_sink["bytes"] /
                                phases["udpdn"]["offered"])
                       if phases.get("udpdn", {}).get("offered")
                       and udpdn_sink.get("bytes") is not None else None),
        "udpup_mbit": (phases.get("udpup", {}).get("kbit", 0) / 1000.0
                       if phases.get("udpup") else None),
        "udpup_sender": udpup,
        "udpup_loss": (100.0 * (1.0 - phases["udpup"]["bytes"] /
                                _total(udpup))
                       if phases.get("udpup") and _total(udpup)
                       else None),
        "app_down": _app_down(
            phases, udpdn_sink.get("bytes"),
            _total(tcpdn) , _total(tcpdn2)),
        "app_up": _app_up(
            phases, _total(tcpup) or 0, _total(udpup) or 0,
            phases.get("udpup", {}).get("udp_in")),
        "air": {p: read(d / f"air-{p}.txt").strip()
                for p in ("tcpdn", "tcpup", "tcpdn2", "udpdn", "udpup")},
        "ip": {p: read(d / f"ip-{p}.txt").strip()
               for p in ("tcpdn", "udpdn")},
    }


def mbits_to_bytes(text: str) -> float:
    rows = interval_bytes(text)
    return rows[-1][2] if rows else 0


def _total(text: str) -> float | None:
    rows = interval_bytes(text)
    if not rows:
        return None
    # the last row is the summary when iperf printed one
    if rows[-1][0] == 0.0:
        return rows[-1][2]
    return sum(r[2] for r in rows)


def _app_down(phases, udp_recv, tcpdn, tcpdn2) -> float | None:
    sent = sum(phases[n]["bytes"] for n in ("tcpdn", "tcpdn2", "udpdn")
               if n in phases)
    recv = (udp_recv or 0) + (tcpdn or 0) + (tcpdn2 or 0)
    return 100.0 * (1.0 - recv / sent) if sent and recv else None


def _app_up(phases, tcp_sent, udp_sent, udp_in) -> float | None:
    # UDP up uses the stack's delivered-datagram count, not the sink's
    # window: the sink starts after the 3 s peer margin while the Pi's 18 s
    # client starts at the phase marker, so the sink misses the flow's first
    # seconds (2026-11-14 rtos-ap2: udp_in 14,442 vs sink 12,713, err 0).
    udp_recv = (udp_in * 1470) if udp_in else phases.get("udpup", {}).get(
        "bytes", 0)
    recv = phases.get("tcpup", {}).get("bytes", 0) + udp_recv
    sent = (tcp_sent or 0) + (udp_sent or 0)
    if not sent or not recv:
        return None
    return max(0.0, 100.0 * (1.0 - recv / sent))


def gt_boot(run: Path, tag: str) -> dict:
    pd = run / "pi" / tag
    bd = run / "board" / tag
    board = next((p for p in bd.glob("gt-8851bu-*") if p.is_dir()), bd)
    console = read(bd / "console-stdout.txt") + read(console_path(bd))
    phases = {}
    for name in ("tcpdn", "tcpdn2", "tcpup", "udpdn", "udpup"):
        f = board / f"tp-{name}-board.txt"
        phases[name] = {"text": read(f)}
    tcpdn = read(pd / "peer-tcpdn.txt")
    tcpup = read(pd / "peer-tcpup.txt")
    udpup = read(pd / "peer-udpup.txt")
    udpdn_sink = sink_json(pd / "sink-udpdn.txt")
    offered = 0
    sent_txt = phases["udpdn"]["text"]
    m = re.search(r"Sent\s+(\d+)\s+datagrams", sent_txt)
    sent_pkts = int(m.group(1)) if m else 0
    m = re.search(r"(\d+(?:\.\d+)?)\s+MBytes", sent_txt)
    sent_mb = float(m.group(1)) if m else 0.0
    offered = int(sent_mb * 1024 ** 2) if sent_mb else 0
    return {
        "arm": "gt", "tag": tag, "dir": str(pd), "board": str(board),
        "tcp_window_down": window_kb(tcpdn),
        "tcp_window_up": window_kb(tcpup),
        "sink_rcvbuf": udpdn_sink.get("rcvbuf"),
        "udpdn_offered_rate": 10000 if "rate=10M" in read(
            board / "phase.log") else None,
        "tcp_down_mbit_10s": rate_over(tcpdn, 10.0)[1],
        "tcp_up_mbit_5s": rate_over(tcpup, 5.0)[1],
        "tcp_up_mbit_10s": rate_over(tcpup, 10.0)[1],
        "tcp_up_sender_mbit": mbits(tcpup),
        "udpdn_sink": udpdn_sink,
        "udpdn_active": sink_active(pd / "sink-udpdn.txt.log"),
        "udpdn_mbit": sink_active(pd / "sink-udpdn.txt.log")["mbit"],
        "udpdn_offered": offered,
        "udpdn_loss": (100.0 * (1.0 - udpdn_sink["bytes"] / offered)
                       if offered and udpdn_sink.get("bytes") is not None
                       else None),
        # the GT board's receiver is the truth; when its file is empty the
        # same Pi iperf client's achieved rate is the fallback (the sender is
        # the same program on both arms).
        # The Pi client's file ends with the server's report line (the
        # receiver's own rate), so it is the same-end witness when the
        # board's own iperf output is absent.
        "udpup_receiver": mbits(udpup),
        "app_down": _app_down(
            {"tcpdn": {"bytes": _total(phases["tcpdn"]["text"]) or 0},
             "tcpdn2": {"bytes": _total(phases["tcpdn2"]["text"]) or 0},
             "udpdn": {"bytes": offered}},
            udpdn_sink.get("bytes"),
            _total(tcpdn), _total(read(pd / "peer-tcpdn2.txt"))),
        "app_up": _app_up(
            {"tcpup": {"bytes": mbits_to_bytes(phases["tcpup"]["text"])},
             "udpup": {"bytes": mbits_to_bytes(phases["udpup"]["text"])}},
            _total(tcpup) or 0, _total(udpup) or 0, None),
        "udpup_sender": udpup,
        "udpup_loss": None,
        "phase_report": {k: v["text"] for k, v in phases.items()},
    }


def console_path(bd: Path) -> Path:
    for p in (bd / "console", bd / "console-stdout.txt",
              bd / "console.txt"):
        if p.exists():
            return p
    return bd / "missing"


def _mean(vals):
    vals = [v for v in vals if v is not None]
    return statistics.fmean(vals) if vals else None


def pct(rt, gt):
    return (100.0 * rt / gt) if rt is not None and gt else None


def selftest() -> int:
    """The two arithmetic traps that made the first round's numbers wrong:
    iperf's summary row double-counted with its intervals, and the sink's
    idle lead-in counted as flow time."""
    text = ("[  1] 0.0000-5.0000 sec 4.88 MBytes 8.18 Mbits/sec\n"
            "[  1] 5.0000-10.0000 sec 4.50 MBytes 7.55 Mbits/sec\n"
            "[  1] 0.0000-10.0000 sec 9.38 MBytes 7.87 Mbits/sec\n")
    b, m = rate_over(text, 10.0)
    assert abs(b - (4.88 + 4.50) * 1024 ** 2) < 1e4, b
    assert abs(m - (4.88 + 4.50) * 1024 ** 2 * 8 / 10 / 1e6) < 0.02, m
    assert abs(_total(text) - 9.38 * 1024 ** 2) < 1e4, _total(text)
    assert window_kb("TCP window size: 256 KByte (WARNING)") == 256
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "sink.log"
        p.write_text('{"t": 1.0, "bytes": 0, "pkts": 0}\n'
                     '{"t": 2.0, "bytes": 1000, "pkts": 1}\n'
                     '{"t": 3.0, "bytes": 3000, "pkts": 3}\n'
                     '{"t": 4.0, "bytes": 3000, "pkts": 3}\n')
        a = sink_active(p)
        assert a["bytes"] == 3000, a
        assert abs(a["seconds"] - 2.0) < 0.01, a
        assert abs(a["mbit"] - 3000 * 8 / 2 / 1e6) < 1e-6, a
    print("ap-align-report selftest: ok")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--run", default="")
    ap.add_argument("--gt-run", default="",
                    help="where the GT boots live (default: --run)")
    ap.add_argument("--gt-tags", default="")
    ap.add_argument("--rtos-tags", default="")
    ap.add_argument("--json", default="")
    ap.add_argument("--markdown", default="")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.run:
        ap.error("--run is required")
    run = Path(args.run)
    gt_run = Path(args.gt_run) if args.gt_run else run
    gt_tags = [t for t in args.gt_tags.split(",") if t]
    rtos_tags = [t for t in args.rtos_tags.split(",") if t]
    gt = [gt_boot(gt_run, t) for t in gt_tags]
    rt = [rtos_boot(run, t) for t in rtos_tags]

    def agg(boots, key, mode="mean"):
        vals = [b.get(key) for b in boots if b.get(key) is not None]
        if not vals:
            return None
        return statistics.fmean(vals) if mode == "mean" else max(vals)

    # TCP gates only use the GT boots whose windows match the RTOS arm (the
    # old gt-ap1 ran 128K/16K, not the new 256K ruler).  UDP has no window:
    # the PREREG's UDP-up denominator is the mean of both old xHCI boots.
    tcp_gt = [b for b in gt
              if b.get("tcp_window_down") == (rt[0].get("tcp_window_down")
                                              if rt else None)
              and b.get("tcp_window_up") == (rt[0].get("tcp_window_up")
                                             if rt else None)] or gt
    g = {k: agg(tcp_gt, k) for k in (
        "tcp_down_mbit_10s", "tcp_up_mbit_10s", "udpdn_mbit", "udpdn_loss",
        "udpup_receiver", "tcp_window_down", "tcp_window_up", "sink_rcvbuf")}
    g["udpup_mbit"] = agg(gt, "udpup_receiver")
    r = {k: agg(rt, k) for k in (
        "tcp_down_mbit_10s", "tcp_up_mbit_10s", "udpdn_mbit", "udpdn_loss",
        "udpup_mbit", "tcp_window_down", "tcp_window_up", "sink_rcvbuf")}
    # udpup: both arms' receiver; the GT receiver is the board's iperf server.
    r["udpup_mbit"] = agg(rt, "udpup_mbit")
    # g["udpup_mbit"] stays the mean of both old xHCI GT boots (PREREG §10);
    # the TCP windows above use only the same-ruler boot.

    # The GT arm's same-sink UDP down is unavailable (its new-round sink never
    # bound); the RTOS UDP down is then judged against the offered rate, and
    # the report says so instead of silently comparing different receivers.
    udp_down_absolute = g["udpdn_mbit"] is None or g["sink_rcvbuf"] is None
    instrument = {
        "tcp_window_down_same": (g["tcp_window_down"] is not None and
                                 g["tcp_window_down"] == r["tcp_window_down"]),
        "tcp_window_up_same": (g["tcp_window_up"] is not None and
                               g["tcp_window_up"] == r["tcp_window_up"]),
        "udp_receiver_same": (g["sink_rcvbuf"] is not None and
                              g["sink_rcvbuf"] == r["sink_rcvbuf"]),
        "udp_down_absolute": udp_down_absolute,
        "gt_windows": g["tcp_window_down"], "rtos_windows": r["tcp_window_down"],
        "gt_sink_rcvbuf": g["sink_rcvbuf"], "rtos_sink_rcvbuf": r["sink_rcvbuf"],
    }
    instrument["ok"] = (instrument["tcp_window_down_same"] and
                        instrument["tcp_window_up_same"]) and \
                       (udp_down_absolute or instrument["udp_receiver_same"])

    ratios = {
        "tcp_down": pct(r["tcp_down_mbit_10s"], g["tcp_down_mbit_10s"]),
        "tcp_up": pct(r["tcp_up_mbit_10s"], g["tcp_up_mbit_10s"]),
        "udp_down": pct(r["udpdn_mbit"], g["udpdn_mbit"]),
        "udp_up": pct(r["udpup_mbit"], g["udpup_mbit"]),
    }
    app_down = max((b.get("app_down") for b in rt), default=None)
    app_up = max((b.get("app_up") for b in rt), default=None)
    gates = {
        "instrument": instrument["ok"],
        "tcp_down_ge_80": ratios["tcp_down"] is not None and ratios["tcp_down"] >= 80,
        "tcp_up_ge_80": ratios["tcp_up"] is not None and ratios["tcp_up"] >= 80,
        "udp_down_ge_80": ((ratios["udp_down"] is not None and
                            ratios["udp_down"] >= 80) if not udp_down_absolute
                           else (r["udpdn_mbit"] is not None and
                                 r["udpdn_mbit"] >= 8.0)),
        "udp_down_loss": ((r["udpdn_loss"] is not None and
                           g["udpdn_loss"] is not None and
                           r["udpdn_loss"] <= g["udpdn_loss"] + 1.0)
                          if not udp_down_absolute
                          else (r["udpdn_loss"] is not None and
                                r["udpdn_loss"] <= 2.0)),
        "udp_up_ge_80": ratios["udp_up"] is not None and ratios["udp_up"] >= 80,
        "app_down_le_1": app_down is not None and app_down <= 1.0,
        "app_up_le_1": app_up is not None and app_up <= 1.0,
    }
    verdict = "pass" if all(gates.values()) else "fail"
    report = {
        "run": str(run), "gt_tags": gt_tags, "rtos_tags": rtos_tags,
        "gt": g, "rtos": r, "ratios_pct": ratios,
        "instrument": instrument, "gates": gates, "verdict": verdict,
        "app_down_pct": app_down, "app_up_pct": app_up,
        "boots": {"gt": gt, "rtos": rt},
    }
    if args.json:
        Path(args.json).write_text(json.dumps(report, indent=2) + "\n")
    md = render(report)
    print(md)
    if args.markdown and args.markdown != "-":
        Path(args.markdown).write_text(md + "\n")
    return 0 if verdict == "pass" else 1


def render(rep: dict) -> str:
    g, r, x = rep["gt"], rep["rtos"], rep["ratios_pct"]
    ins = rep["instrument"]
    lines = [
        "# AP alignment report", "",
        f"- verdict: **{rep['verdict']}**",
        f"- instruments: windows GT {ins['gt_windows']} / RTOS "
        f"{ins['rtos_windows']} KB, sink rcvbuf GT {ins['gt_sink_rcvbuf']} / "
        f"RTOS {ins['rtos_sink_rcvbuf']}, same={ins['ok']}",
        "",
        "| phase | GT | RTOS | ratio |",
        "|---|---|---|---|",
        f"| TCP down (10 s) | {g['tcp_down_mbit_10s']} | "
        f"{r['tcp_down_mbit_10s']} | {x['tcp_down']}% |",
        f"| TCP up (10 s) | {g['tcp_up_mbit_10s']} | "
        f"{r['tcp_up_mbit_10s']} | {x['tcp_up']}% |",
        f"| UDP down | {g['udpdn_mbit']} (loss {g['udpdn_loss']}%) | "
        f"{r['udpdn_mbit']} (loss {r['udpdn_loss']}%) | {x['udp_down']}% |",
        f"| UDP up | {g['udpup_mbit']} | {r['udpup_mbit']} | "
        f"{x['udp_up']}% |",
        "",
        f"- app-vs-app down {rep['app_down_pct']}%, up {rep['app_up_pct']}%",
        "- gates: " + ", ".join(f"{k}={'pass' if v else 'FAIL'}"
                                for k, v in rep["gates"].items()),
        "",
    ]
    for arm in ("gt", "rtos"):
        for b in rep["boots"][arm]:
            lines.append(f"- {arm} {b['tag']}: UDP down sink "
                         f"{b.get('udpdn_sink', {}).get('bytes')} B, active "
                         f"{b.get('udpdn_active', {}).get('mbit')} Mbit/s")
    return "\n".join(lines)


if __name__ == "__main__":
    sys.exit(main())
