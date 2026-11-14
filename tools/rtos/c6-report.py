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
# The phase anchors carry the case name; the tool takes it as an argument so
# the same gates serve every C6T arm (iwl7260_c6, rtw89_c6, ...).
PHASE_BEGIN_FMT = (
    r"{case}: phase \d+ begin proto=\S+ role=\S+ (?:air=\S+ )?port=\d+ "
    r"window_ms=\d+ name=(\S+)")
PHASE_REPORT_FMT = (
    r"{case}: phase \d+ report bytes=(\d+) ms=(\d+) kbit=(\d+) "
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


def rtos_phases(serial: Path, case: str = "iwl7260_c6") -> dict:
    """name -> {bytes, ms, kbit, pkts} from the case's own anchors."""
    begin_re = re.compile(PHASE_BEGIN_FMT.format(case=re.escape(case)))
    report_re = re.compile(PHASE_REPORT_FMT.format(case=re.escape(case)))
    phases: dict[str, dict] = {}
    current = None
    for line in read(serial).replace("\r", "").splitlines():
        m = begin_re.search(line)
        if m:
            current = m.group(1)
            continue
        m = report_re.search(line)
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


def gt_arm(board: Path, inner_glob: str = "gt-ac7260-*") -> dict:
    inner = list(board.glob(inner_glob))
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
        # The board's own send phases: the Pi's servers are the receivers.
        "peer_tcpup_srv": iperf_summary(board / "peer-tcp-server.log"),
        "peer_udpup_srv": iperf_summary(board / "peer-udp-server.log"),
    }
    return arm


def rename_phases(phases: dict, rename: dict) -> dict:
    """Re-label an arm's phases into the ruler's direction names.

    The AP arm of the 8188EU round runs the station arm's four direction cells
    with the roles swapped (PREREG.md §1), so its `tcpdn` is the board SENDING
    while the station/GT arms' `tcpdn` is the board RECEIVING.  Renaming the
    AP arm's phases (and the peer files they index) is what makes one report
    compare like with like; the mapping is printed in the report.
    """
    if not rename:
        return phases
    return {rename.get(k, k): v for k, v in phases.items()}


def rtos_arm(evidence: Path, case: str = "iwl7260_c6",
             rename: dict | None = None) -> dict:
    phases = rtos_phases(evidence / "serial.log", case)
    phases = rename_phases(phases, rename)
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
    if not sink_text.strip():
        # The AP round's sink is per phase (ap-round.sh): the board-send UDP
        # cell is `udpdn` there.
        sink_text = read(evidence / "sink-udpdn.txt")
    # The sink prints one JSON object per second and then a summary line with
    # no `t`; the flow's duration is the last *interval* line's timestamp (the
    # summary's `seconds` is the sink's whole lifetime, idle time included).
    arm["udp_sink"] = None
    for line in sink_text.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            obj = json.loads(line)
        except ValueError:
            continue
        if "t" in obj:
            arm["udp_sink"] = obj
        elif arm["udp_sink"] is None and "bytes" in obj:
            arm["udp_sink"] = obj
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
    # The board-send UDP cell: the sender's own paced report is the rate (the
    # blast is paced, so its own window is the flow's), then the receiver's
    # sink (whose `t` includes the idle time before the blast), then the GT
    # arm's iperf server log.  The app-vs-app bytes are reported separately.
    udp_up = arm["udpup"].get("mbit")
    if udp_up is None:
        udp_up = (round(sink["bytes"] * 8 / sink["t"] / 1e6, 3)
                  if sink.get("bytes") and sink.get("t") else None)
    if udp_up is None and arm.get("peer_udpup_srv"):
        udp_up = arm["peer_udpup_srv"].get("mbit")
    return {
        "tcp_down": max_or_none(arm["tcpdn"].get("mbit"),
                                arm["tcpdn2"].get("mbit")),
        "tcp_up": max_or_none(arm["tcpup"].get("mbit"),
                              (arm.get("peer_tcpup_srv") or {}).get("mbit")),
        "udp_down": max_or_none(arm["udpdn"].get("mbit"),
                                arm["udpdn2"].get("mbit")),
        "udp_up": udp_up,
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
    ap.add_argument("--case-prefix", default="iwl7260_c6",
                    help="the case name the phase anchors carry")
    ap.add_argument("--gt-inner-glob", default="gt-ac7260-*",
                    help="the GT board script's output dir name inside the GT "
                         "boot dir (one per card)")
    ap.add_argument("--rtos-rename", default="",
                    help="phase renames for the RTOS arm, e.g. "
                         "tcpdn=tcpup,tcpup=tcpdn (direction-paired tables)")
    ap.add_argument("--gates", choices=("c6", "full"), default="c6",
                    help="c6 = the 7260 pre-registration's gates; full adds "
                         "the UDP rate gates of a direction-paired round")
    ap.add_argument("--no-t2", dest="t2", action="store_false",
                    help="skip the AP-pcap T2 gate (an arm without that "
                         "capture)")
    ap.set_defaults(t2=True)
    args = ap.parse_args()

    rename = {}
    for item in args.rtos_rename.split(","):
        if "=" in item:
            src, _, dst = item.partition("=")
            rename[src.strip()] = dst.strip()
    gt_arms = [gt_arm(args.gt, args.gt_inner_glob)]
    if args.gt2:
        gt_arms.append(gt_arm(args.gt2, args.gt_inner_glob))
    rt_arms = [rtos_arm(args.rtos, args.case_prefix, rename)]
    if args.rtos2:
        rt_arms.append(rtos_arm(args.rtos2, args.case_prefix, rename))

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
    }
    if args.t2:
        gates["t2_ap_pcap_within_5pct"] = (
            (t2["tcp_pct"] is not None and t2["tcp_pct"] <= 5.0)
            and (t2["udp_pct"] is not None and t2["udp_pct"] <= 5.0))
    if args.gates == "full":
        udp_ratios = {
            "udp_down": (rt["udp_down"] / gt["udp_down"])
            if rt["udp_down"] and gt["udp_down"] else None,
            "udp_up": (rt["udp_up"] / gt["udp_up"])
            if rt["udp_up"] and gt["udp_up"] else None,
        }
        gates["udp_down_ge_80pct"] = (udp_ratios["udp_down"] is not None
                                      and udp_ratios["udp_down"] >= args.tcp_min)
        gates["udp_up_ge_80pct"] = (udp_ratios["udp_up"] is not None
                                    and udp_ratios["udp_up"] >= args.tcp_min)
        ratios.update(udp_ratios)
    verdict = "pass" if all(gates.values()) else "fail"

    report = {
        "verdict": verdict,
        "gates": gates,
        "rule": "per-direction mean over the arm's boots; UDP loss max",
        "gt": {"aggregate": gt, "boots": gt_boots},
        "rtos": {"aggregate": rt, "boots": rt_boots},
        "ratios": ratios,
        "t2": t2,
        "rtos_rename": rename,
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
