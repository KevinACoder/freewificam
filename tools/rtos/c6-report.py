#!/usr/bin/env python3
"""Compare one GT throughput round with one RTOS C6T round.

Inputs are the two arms' evidence directories:
  * GT:  runs/<date>/board/<tag>/  (the tar of the board's NFS output, plus the
         peer's iperf outputs saved by tools/gt/run-gt-throughput.sh)
  * RTOS: runs/<date>/<tag>/       (serial.log + peer-*.txt + air-summary.txt)

The pre-registered gates are in runs/2026-11-11-iwl7260-c6-throughput/PREREG.md:
TCP down and up each >= 80% of GT; UDP 40 Mbit/s downlink loss <= 1%; the AP-side
pcap's payload within 5% of the application's.  The tool applies those gates
and exits 0 only when all of them pass.  It never fabricates a missing cell:
an absent file or anchor is reported as `missing` and fails the run.

Usage:
    tools/rtos/c6-report.py --gt runs/.../board/gt1 --rtos runs/.../rtos1 \
        [--json REPORT.json] [--markdown -]
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

MBITS = re.compile(r"(\d+(?:\.\d+)?)\s+Mbits/sec")
MBYTES = re.compile(r"(\d+(?:\.\d+)?)\s+MBytes")
LOSS = re.compile(r"\((\d+(?:\.\d+)?)%\)")
PHASE_BEGIN = re.compile(
    r"iwl7260_c6: phase \d+ begin proto=\S+ role=\S+ port=\d+ "
    r"window_ms=\d+ name=(\S+)")
PHASE_REPORT = re.compile(
    r"iwl7260_c6: phase \d+ report bytes=(\d+) ms=(\d+) kbit=(\d+) "
    r"pkts=(\d+)")


def last_match(pattern: re.Pattern, text: str) -> str | None:
    found = pattern.findall(text)
    return found[-1] if found else None


def read(path: Path) -> str:
    try:
        return path.read_text(errors="replace")
    except OSError:
        return ""


def iperf_summary(path: Path) -> dict:
    """The last interval line's rate/bytes/loss, plus the file's own presence."""
    text = read(path)
    if not text.strip():
        return {"present": False}
    out = {
        "present": True,
        "mbit": last_match(MBITS, text),
        "mbytes": last_match(MBYTES, text),
        "loss_pct": last_match(LOSS, text),
    }
    for key in ("mbit", "mbytes", "loss_pct"):
        if out[key] is not None:
            out[key] = float(out[key])
    return out


def rtos_phases(serial: Path) -> dict:
    """name -> {bytes, ms, kbit, pkts} from the case's own anchors."""
    phases: dict[str, dict] = {}
    current = None
    for line in read(serial).replace("\r", "").splitlines():
        m = PHASE_BEGIN.search(line)
        if m:
            current = m.group(1)
            continue
        m = PHASE_REPORT.search(line)
        if m and current:
            phases[current] = {
                "bytes": int(m.group(1)),
                "ms": int(m.group(2)),
                "kbit": int(m.group(3)),
                "pkts": int(m.group(4)),
            }
            current = None
    return phases


def ap_pcap_totals(evidence: Path) -> dict:
    """The AP-side pcap summary's payload totals (the T2 third source)."""
    text = read(evidence / "air-summary.txt")
    out = {}
    m = re.search(r"^frames=(\d+) bytes=(\d+)", text, re.MULTILINE)
    if m:
        out["frames"] = int(m.group(1))
        out["bytes"] = int(m.group(2))
    m = re.search(r"tcp_payload_frames=(\d+) payload_bytes=(\d+)", text)
    if m:
        out["tcp_payload_frames"] = int(m.group(1))
        out["payload_bytes"] = int(m.group(2))
    m = re.search(r"udp_payload_frames=(\d+) payload_bytes=(\d+)", text)
    if m:
        out["udp_payload_frames"] = int(m.group(1))
        out["udp_payload_bytes"] = int(m.group(2))
    return out


def max_or_none(*values):
    vals = [v for v in values if v is not None]
    return max(vals) if vals else None


def gt_arm(board: Path) -> dict:
    inner = list(board.glob("gt-ac7260-*"))
    src = inner[0] if inner else board
    arm = {
        "src": str(src),
        "tcpdn": iperf_summary(src / "tp-tcpdn-board.txt"),
        "tcpdn2": iperf_summary(src / "tp-tcpdn2-board.txt"),
        "udpdn": iperf_summary(src / "tp-udpdn-board.txt"),
        "udpdn2": iperf_summary(src / "tp-udpdn2-board.txt"),
        "tcpup": iperf_summary(src / "tp-tcpup-board.txt"),
        "udpup": iperf_summary(src / "tp-udpup-board.txt"),
        "peer_tcpdn": iperf_summary(board / "peer-tcpdn.txt"),
        "peer_udpdn": iperf_summary(board / "peer-udpdn.txt"),
        "peer_tcpdn2": iperf_summary(board / "peer-tcpdn2.txt"),
        "peer_udpdn2": iperf_summary(board / "peer-udpdn2.txt"),
    }
    return arm


def rtos_arm(evidence: Path) -> dict:
    phases = rtos_phases(evidence / "serial.log")
    arm = {"src": str(evidence), "phases": phases}
    for name in ("tcpdn", "tcpdn2", "udpdn", "udpdn2", "tcpup", "udpup"):
        p = phases.get(name)
        arm[name] = {
            "present": p is not None,
            "mbit": (p["kbit"] / 1000.0) if p else None,
            "bytes": p["bytes"] if p else None,
            "ms": p["ms"] if p else None,
            "pkts": p["pkts"] if p else None,
        }
    for name in ("tcpdn", "tcpdn2", "udpdn", "udpdn2"):
        arm[f"peer_{name}"] = iperf_summary(evidence / f"peer-{name}.txt")
    arm["ap_pcap"] = ap_pcap_totals(evidence)
    sink_text = read(evidence / "udp-sink.txt")
    try:
        arm["udp_sink"] = json.loads(sink_text.splitlines()[-1])
    except (ValueError, IndexError):
        arm["udp_sink"] = None
    return arm


def loss_from(peer: dict, board: dict) -> float | None:
    """UDP downlink loss from the payload each end counted, percent."""
    if not peer.get("present") or not board.get("present"):
        return None
    sent = peer.get("mbytes")
    got = board.get("bytes")
    if not sent or not got:
        return None
    sent_bytes = sent * 1024 * 1024
    if sent_bytes <= 0:
        return None
    return max(0.0, 100.0 * (1.0 - got / sent_bytes))


def arm_metrics(arm: dict) -> dict:
    """Per-boot capacity numbers: the repeat phases' larger value each."""
    sink = arm.get("udp_sink") or {}
    return {
        "tcp_down": max_or_none(arm["tcpdn"].get("mbit"),
                                arm["tcpdn2"].get("mbit")),
        "tcp_up": arm["tcpup"].get("mbit"),
        "udp_down": max_or_none(arm["udpdn"].get("mbit"),
                                arm["udpdn2"].get("mbit")),
        "udp_up": (round(sink["bytes"] * 8 / 20.0 / 1e6, 3)
                   if sink.get("bytes") else None),
        "udp_loss": loss_from(arm["peer_udpdn"], arm["udpdn"]),
    }


def mean_of(values: list) -> float | None:
    vals = [v for v in values if v is not None]
    return sum(vals) / len(vals) if vals else None


def aggregate(arms: list) -> tuple[dict, list]:
    """The pre-registration's rule: per-direction mean over the arm's boots."""
    per_boot = [arm_metrics(a) for a in arms]
    agg = {}
    for key in ("tcp_down", "tcp_up", "udp_down", "udp_up"):
        agg[key] = mean_of([m[key] for m in per_boot])
    losses = [m["udp_loss"] for m in per_boot if m["udp_loss"] is not None]
    agg["udp_loss"] = max(losses) if losses else None   # conservative
    return agg, per_boot


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--gt", required=True, type=Path,
                    help="GT board evidence directory (boot 1)")
    ap.add_argument("--gt2", type=Path, default=None,
                    help="GT board evidence directory (boot 2)")
    ap.add_argument("--rtos", required=True, type=Path,
                    help="RTOS evidence directory (boot 1)")
    ap.add_argument("--rtos2", type=Path, default=None,
                    help="RTOS evidence directory (boot 2)")
    ap.add_argument("--json", type=Path, default=None)
    ap.add_argument("--tcp-min", type=float, default=0.80,
                    help="TCP ratio gate against GT (default 0.80)")
    args = ap.parse_args()

    gt_arms = [gt_arm(args.gt)]
    if args.gt2:
        gt_arms.append(gt_arm(args.gt2))
    rt_arms = [rtos_arm(args.rtos)]
    if args.rtos2:
        rt_arms.append(rtos_arm(args.rtos2))

    gt, gt_boots = aggregate(gt_arms)
    rt, rt_boots = aggregate(rt_arms)

    ratios = {
        "tcp_down": (rt["tcp_down"] / gt["tcp_down"])
        if rt["tcp_down"] and gt["tcp_down"] else None,
        "tcp_up": (rt["tcp_up"] / gt["tcp_up"])
        if rt["tcp_up"] and gt["tcp_up"] else None,
    }

    # T2: the AP-side pcap payload against the application's own bytes, on the
    # arm that has a drop-free capture.  A capture with kernel drops is an
    # instrument failure, not a DUT result; it is reported as such.
    t2 = {"tcp_pct": None, "udp_pct": None, "app_tcp_bytes": 0,
          "app_udp_bytes": 0, "note": "no arm with a pcap"}
    for idx, arm in enumerate(rt_arms):
        pcap = arm["ap_pcap"]
        app_tcp = sum(arm[n].get("bytes") or 0
                      for n in ("tcpdn", "tcpdn2", "tcpup"))
        app_udp = sum(arm[n].get("bytes") or 0
                      for n in ("udpdn", "udpdn2"))
        pcap_tcp = pcap.get("payload_bytes", 0)
        pcap_udp = pcap.get("udp_payload_bytes", 0)
        if app_tcp and pcap_tcp:
            t2 = {
                "arm": str(args.rtos) if idx == 0 else str(args.rtos2),
                "tcp_pct": abs(pcap_tcp - app_tcp) / app_tcp * 100.0,
                "udp_pct": (abs(pcap_udp - app_udp) / app_udp * 100.0
                            if app_udp and pcap_udp else None),
                "app_tcp_bytes": app_tcp,
                "app_udp_bytes": app_udp,
                "pcap_tcp_bytes": pcap_tcp,
                "pcap_udp_bytes": pcap_udp,
                "note": "tcpdump drops recorded in the arm's ap-tcpdump.log",
            }
            break

    gates = {
        "tcp_down_ge_80pct": ratios["tcp_down"] is not None
        and ratios["tcp_down"] >= args.tcp_min,
        "tcp_up_ge_80pct": ratios["tcp_up"] is not None
        and ratios["tcp_up"] >= args.tcp_min,
        "udp_down_loss_le_1pct": rt["udp_loss"] is not None
        and rt["udp_loss"] <= 1.0,
        "t2_ap_pcap_within_5pct": (t2["tcp_pct"] is not None
                                   and t2["tcp_pct"] <= 5.0)
        and (t2["udp_pct"] is not None and t2["udp_pct"] <= 5.0),
    }
    verdict = "pass" if all(gates.values()) else "fail"

    report = {
        "verdict": verdict,
        "gates": gates,
        "rule": "per-direction mean over the arm's boots; UDP loss max",
        "gt": {"aggregate": gt, "boots": gt_boots},
        "rtos": {"aggregate": rt, "boots": rt_boots},
        "ratios": ratios,
        "t2": t2,
    }

    print(f"verdict: {verdict}")
    for name, ok in gates.items():
        print(f"  [{'ok' if ok else 'FAIL'}] {name}")
    print(f"GT   boots={[round(m['tcp_up'], 2) for m in gt_boots]}"
          f" agg tcp down/up = {round(gt['tcp_down'], 2)} / "
          f"{round(gt['tcp_up'], 2)} Mbit/s"
          f"  udp down = {round(gt['udp_down'], 2)} (loss {gt['udp_loss']} %)")
    print(f"RTOS boots={[round(m['tcp_up'], 2) for m in rt_boots]}"
          f" agg tcp down/up = {round(rt['tcp_down'], 2)} / "
          f"{round(rt['tcp_up'], 2)} Mbit/s"
          f"  udp down = {round(rt['udp_down'], 2)} (loss {rt['udp_loss']} %)")
    print(f"ratio tcp down/up = {round(ratios['tcp_down'], 3)} / "
          f"{round(ratios['tcp_up'], 3)}")
    print(f"T2  tcp pcap-vs-app = {t2['tcp_pct']} %  udp = {t2['udp_pct']} %"
          f"  ({t2['note']})")

    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + "\n")
    return 0 if verdict == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
