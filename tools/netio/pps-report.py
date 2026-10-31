#!/usr/bin/env python3
"""Summarize one net_pps cell from the board console log and the sender log.

The board's net_pps case prints one `pps: win` / `win2` / `win3` group per
window; ppsgen prints one `PPSGEN END` line.  This tool joins them into one
result line and applies the validity gates used by docs/NET-TESTING.md:

  sender_fidelity = |offered_pps - requested_pps| / requested_pps  <= 2%
  sender_error    = |sent - offered| / sent                        <= 1%
  window          = |sender secs - target|                         <= 0.05 s
  closure         = |accounted - offered| / offered                <= 0.1%

Exit 0 when every gate holds, 2 when a gate fails, 3 on a missing field.

Usage:
  pps-report.py --serial console.log --sender sender.log --tag cell
  pps-report.py --serial console.log --sender sender.log --tag cell --json
"""
import argparse
import json
import re
import sys

FIELDS = (
    "ovf", "rx_dropped", "pool_fail", "input_fail", "udp_drop", "ip_drop",
    "link_drop", "mem_err", "pbuf_err", "ru", "irqs", "sweeps", "pkts",
    "ru_ppm", "ovf_ppm",
)


def to_int(value, default=0):
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def kv(line):
    return dict(re.findall(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)", line))


def last_line_for(lines, tag, kind):
    prefix = "pps: %s tag=%s " % (kind, tag)
    found = None
    for line in lines:
        if prefix in line:
            found = line
    return found


def parse(serial_path, sender_path, tag):
    with open(serial_path, "r", errors="replace") as fh:
        lines = fh.readlines()
    win = last_line_for(lines, tag, "win")
    win2 = last_line_for(lines, tag, "win2")
    win3 = last_line_for(lines, tag, "win3")
    if win is None:
        raise SystemExit("pps-report: no 'pps: win tag=%s' line in %s" % (tag, serial_path))

    sender = None
    with open(sender_path, "r", errors="replace") as fh:
        for line in fh:
            if "PPSGEN END" in line:
                sender = line
    if sender is None:
        raise SystemExit("pps-report: no 'PPSGEN END' line in %s" % sender_path)

    w = kv(win)
    w2 = kv(win2) if win2 else {}
    w3 = kv(win3) if win3 else {}
    s = kv(sender)

    missing = [key for key in ("offered", "delivered") if key not in w]
    if "sent" not in s or "secs" not in s:
        missing.append("sender sent/secs")
    if missing:
        raise SystemExit("pps-report: missing field(s): %s" % ", ".join(missing))

    secs = float(s["secs"])
    sent = to_int(s["sent"])
    req = to_int(s.get("req"))
    offered = to_int(w["offered"])
    delivered = to_int(w["delivered"])
    frames = to_int(w.get("frames"))
    mhz = to_int(w3.get("mhz_x100")) / 100.0 if w3.get("mhz_x100") else 0.0

    accounted = delivered
    for key in ("ovf", "ru", "rx_dropped", "pool_fail", "input_fail",
                "udp_drop", "ip_drop", "link_drop", "mem_err", "pbuf_err"):
        accounted += to_int(w2.get(key))

    offered_pps = offered / secs
    delivered_pps = delivered / secs
    loss = (offered - delivered) / offered if offered else 0.0
    closure = abs(accounted - offered) / offered if offered else 1.0
    c0_us = 1e6 / delivered_pps if delivered_pps else 0.0
    c0_cycles = c0_us * mhz if mhz else 0.0

    checks = {}
    if req:
        checks["sender_fidelity"] = abs(offered_pps - req) / req <= 0.02
    checks["sender_error"] = abs(sent - offered) / sent <= 0.01 if sent else False
    checks["window"] = abs(secs - 10.0) <= 0.05
    checks["closure"] = closure <= 0.001
    checks["loss_nonnegative"] = loss >= -0.0005
    checks["delivered_le_offered"] = delivered <= offered

    failed = [name for name, ok in checks.items() if not ok]
    result = {
        "tag": tag,
        "req_pps": req,
        "sent": sent,
        "offered": offered,
        "delivered": delivered,
        "offered_pps": round(offered_pps, 1),
        "delivered_pps": round(delivered_pps, 1),
        "loss_pct": round(loss * 100.0, 4),
        "c0_us": round(c0_us, 4),
        "c0_cycles": round(c0_cycles, 1),
        "freq_mhz": round(mhz, 1) if mhz else None,
        "ru_ppm": to_int(w2.get("ru_ppm")),
        "ovf_ppm": to_int(w2.get("ovf_ppm")),
        "closure_pct": round(closure * 100.0, 4),
        "window_s": round(secs, 3),
        "frames_per_irq": round(frames / to_int(w2.get("irqs")), 2) if to_int(w2.get("irqs")) else None,
        "frames_per_sweep": round(frames / to_int(w2.get("sweeps")), 2) if to_int(w2.get("sweeps")) else None,
        "callbacks": to_int(w3.get("callbacks")),
        "cores": w3.get("cores"),
        "checks": "ok" if not failed else ",".join(failed),
        "failed": failed,
    }
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--serial", required=True, help="board console log")
    ap.add_argument("--sender", required=True, help="ppsgen stdout log")
    ap.add_argument("--tag", required=True, help="window tag used with go/stop")
    ap.add_argument("--json", action="store_true", help="print the result as JSON")
    args = ap.parse_args()

    result = parse(args.serial, args.sender, args.tag)
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print(
            "PPS tag=%(tag)s offered_pps=%(offered_pps)s delivered_pps=%(delivered_pps)s "
            "loss=%(loss_pct)s%% c0_us=%(c0_us)s C0_cyc=%(c0_cycles)s "
            "freq_mhz=%(freq_mhz)s ru_ppm=%(ru_ppm)s closure=%(closure_pct)s%% "
            "window=%(window_s)ss checks=%(checks)s" % result
        )
    return 0 if not result["failed"] else 2


if __name__ == "__main__":
    sys.exit(main())
