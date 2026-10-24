#!/usr/bin/env python3
"""Judge one RTOS board boot from its serial log.

The image prints a fixed anchor language (see rtos/app/cases.h and the case
files); this module turns that text into per-case verdicts.  It reads nothing
but the log, so the same judge runs on a captured file, on the host tier's
stdout, and on a live capture.

Usage:
    tools/rtos/judge.py <serial.log> [--json out.json] [--expect key=value ...]

Exit code: 0 all cases passed, 1 a case failed or an anchor is missing.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

CASES = ("tick", "cmsis2", "net", "emmc")

# Every anchor is one line of a long capture: the patterns are line-oriented.
FLAGS = re.MULTILINE

# One pattern per claim.  The patterns match the anchor text, not a rewrite of
# it: a log line that does not match is an anchor the image did not print.
PATTERNS = {
    "banner": re.compile(r"rtos: banner backend=(?P<backend>\S+) kernel=(?P<kernel>.+?) build=(?P<build>\S+)$", re.MULTILINE),
    "kernel_ready": re.compile(r"rtos: kernel ready tick=(?P<tick>\d+)Hz", re.MULTILINE),
    "result": re.compile(r"rtos: result (?P<result>pass|fail) cases=(?P<ok>\d+)/(?P<total>\d+) backend=(?P<backend>\S+)", re.MULTILINE),
    "tick_ladder": re.compile(r"tick: ladder beats=(?P<beats>\d+) err_max_ms=(?P<err>-?\d+) tol_ms=(?P<tol>\d+)", re.MULTILINE),
    "tick_pass": re.compile(r"tick: PASS", re.MULTILINE),
    "cmsis_checks": re.compile(r"cmsis2: checks=(?P<checks>\d+) failures=(?P<failures>\d+)", re.MULTILINE),
    "cmsis_pass": re.compile(r"cmsis2: PASS", re.MULTILINE),
    "mutex": re.compile(r"cmsis2: mutex acquire=(?P<acq>\d+) wait_us=(?P<wait>\d+) hold_us=(?P<hold>\d+) try_us=(?P<try>\d*)", re.MULTILINE),
    "flags": re.compile(r"cmsis2: flags or=(?P<or>0x[0-9a-f]+) and=(?P<and>0x[0-9a-f]+) timeout=(?P<timeout>0x[0-9a-f]+)", re.MULTILINE),
    "net_link": re.compile(r"net: link up speed=(?P<speed>\d+) duplex=(?P<duplex>\d+)", re.MULTILINE),
    "net_addr": re.compile(r"net: addr (?P<addr>\d+\.\d+\.\d+\.\d+/\d+) mac=(?P<mac>[0-9a-f:]+)", re.MULTILINE),
    "net_arp": re.compile(r"net: arp (?P<ip>\d+\.\d+\.\d+\.\d+) -> (?P<mac>[0-9a-f:]+)", re.MULTILINE),
    "net_ping": re.compile(r"net: ping tx=(?P<tx>\d+) rx=(?P<rx>\d+) of=(?P<of>\d+) seen=(?P<seen>\d+)", re.MULTILINE),
    "net_down": re.compile(r"net: iperf down done=(?P<done>\d+) bytes=(?P<bytes>\d+) ms=(?P<ms>\d+) kbit=(?P<kbit>\d+)", re.MULTILINE),
    "net_up": re.compile(r"net: iperf up done=(?P<done>\d+) bytes=(?P<bytes>\d+) ms=(?P<ms>\d+) kbit=(?P<kbit>\d+)", re.MULTILINE),
    "net_pass": re.compile(r"net: PASS", re.MULTILINE),
    "emmc_cid": re.compile(r"emmc: cid manufacturer=(?P<manufacturer>\S+) oem=(?P<oem>\S+) product=(?P<product>\S*) rev=(?P<rev>\S+) serial=(?P<serial>\S+) date=(?P<date>\S+)", re.MULTILINE),
    "emmc_csd": re.compile(r"emmc: csd structure=(?P<structure>\d+) spec=(?P<spec>\d+) size=(?P<size>\d+) read_bl_len=(?P<rbl>\d+)", re.MULTILINE),
    "emmc_extcsd": re.compile(r"emmc: extcsd sectors=(?P<sectors>\d+) rev=(?P<rev>\d+) structure=(?P<structure>\d+) type=(?P<type>\S+) bus_width=(?P<bus>\d+) hs_timing=(?P<hs>\d+)", re.MULTILINE),
    "emmc_mode": re.compile(r"emmc: cardsize blocks=(?P<blocks>\d+) clock=(?P<clock>\d+)Hz width=(?P<width>\d+) timing=(?P<timing>\d+) cmd23=(?P<cmd23>\d)", re.MULTILINE),
    "emmc_rw": re.compile(r"emmc: rw crc_w=(?P<w>0x[0-9a-f]+) crc_r=(?P<r>0x[0-9a-f]+) verify=(?P<verify>\w+)", re.MULTILINE),
    "emmc_loop": re.compile(r"emmc: loop n=(?P<n>\d+) errors=(?P<errors>\d+)", re.MULTILINE),
    "emmc_badlba": re.compile(r"emmc: badlba lba=\d+ write_rc=(?P<w>-?\d+) read_rc=(?P<r>-?\d+) nohang=(?P<nohang>\w+)", re.MULTILINE),
    "emmc_pass": re.compile(r"emmc: PASS", re.MULTILINE),
}


def parse(text: str) -> dict:
    """Collect the anchors that are present; absence stays absence."""
    found: dict = {"lines": text.splitlines()}
    for name, pattern in PATTERNS.items():
        match = pattern.search(text)
        found[name] = match.groupdict() if match else None
    found["case_verdicts"] = {
        case: re.findall(rf"rtos: case {case} (PASS|FAIL)", text)
        for case in CASES
    }
    return found


def judge(text: str, expect: dict | None = None) -> dict:
    found = parse(text)
    expect = expect or {}
    errors: list[str] = []
    notes: list[str] = []

    banner = found["banner"]
    if not banner:
        errors.append("no banner line")
    else:
        if "backend" in expect and banner["backend"] != expect["backend"]:
            errors.append(f"backend {banner['backend']} != {expect['backend']}")
        if "build" in expect and banner["build"] != expect["build"]:
            errors.append(f"build {banner['build']} != {expect['build']}")

    if not found["kernel_ready"]:
        errors.append("no kernel-ready line")
    elif found["kernel_ready"]["tick"] != "1000":
        errors.append(f"tick frequency {found['kernel_ready']['tick']} Hz")

    cases: dict[str, dict] = {}
    for case in expect.get("cases", CASES):
        verdicts = found["case_verdicts"].get(case, [])
        if not verdicts:
            cases[case] = {"verdict": "missing", "detail": "no verdict line"}
        elif verdicts[-1] != "PASS":
            cases[case] = {"verdict": "fail", "detail": "case printed FAIL"}
        else:
            cases[case] = {"verdict": "pass", "detail": "case printed PASS"}

    # Per-case evidence checks: a PASS without its evidence anchors is a
    # broken image, not a pass.
    if cases.get("tick", {}).get("verdict") == "pass":
        ladder = found["tick_ladder"]
        if not ladder:
            errors.append("tick: PASS without a ladder line")
        else:
            notes.append(f"tick: err_max={ladder['err']}ms of {ladder['tol']}ms")

    if cases.get("cmsis2", {}).get("verdict") == "pass":
        checks = found["cmsis_checks"]
        if not checks:
            errors.append("cmsis2: PASS without a checks line")
        elif int(checks["failures"]) != 0:
            errors.append(f"cmsis2: {checks['failures']} failed checks")
        else:
            notes.append(f"cmsis2: {checks['checks']} checks")

    if cases.get("net", {}).get("verdict") == "pass":
        ping = found["net_ping"]
        if not ping:
            errors.append("net: PASS without a ping line")
        elif int(ping["rx"]) != int(ping["of"]):
            errors.append(f"net: ping {ping['rx']}/{ping['of']}")
        else:
            notes.append(f"net: ping {ping['rx']}/{ping['of']}")
        if found["net_addr"] is None:
            errors.append("net: no address line")
        for direction in ("net_down", "net_up"):
            row = found[direction]
            if not row or int(row["bytes"]) == 0:
                errors.append(f"{direction}: no bytes")
            else:
                notes.append(f"{direction}: {int(row['kbit'])} kbit/s over {row['ms']} ms")

    if cases.get("emmc", {}).get("verdict") == "pass":
        rw = found["emmc_rw"]
        if not rw or rw["verify"] != "ok":
            errors.append("emmc: readback mismatch")
        loop = found["emmc_loop"]
        if not loop or int(loop["errors"]) != 0:
            errors.append("emmc: loop errors")
        bad = found["emmc_badlba"]
        if not bad or bad["nohang"] != "yes":
            errors.append("emmc: error path did not return")
        for key in ("emmc_cid", "emmc_csd", "emmc_extcsd"):
            if found[key] is None:
                errors.append(f"{key}: no identity line")
            else:
                notes.append(f"{key}: {found[key]}")
        if "emmc_product" in expect and found["emmc_cid"]:
            if found["emmc_cid"]["product"] != expect["emmc_product"]:
                errors.append("emmc: product name differs from the Linux readback")

    result = found["result"]
    if not result:
        errors.append("no result line")
    else:
        notes.append(f"image result: {result['result']} {result['ok']}/{result['total']}")
        if result["result"] != "pass":
            errors.append("image reported a fail")

    return {
        "ok": not errors,
        "errors": errors,
        "notes": notes,
        "cases": cases,
        "anchors": {k: v for k, v in found.items() if k not in ("lines",)},
    }


def render(verdict: dict, title: str = "") -> str:
    out = [f"# judge: {title}" if title else "# judge", ""]
    out.append(f"- overall: **{'PASS' if verdict['ok'] else 'FAIL'}**")
    for case, row in verdict["cases"].items():
        out.append(f"- {case}: {row['verdict']} ({row['detail']})")
    for note in verdict["notes"]:
        out.append(f"- {note}")
    if verdict["errors"]:
        out.append("")
        out.append("## errors")
        for error in verdict["errors"]:
            out.append(f"- {error}")
    return "\n".join(out) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", type=Path)
    ap.add_argument("--json", type=Path, default=None)
    ap.add_argument("--title", default="")
    ap.add_argument("--expect", action="append", default=[])
    args = ap.parse_args()

    text = args.log.read_text(errors="replace")
    expect: dict = {}
    for item in args.expect:
        key, _, value = item.partition("=")
        if key == "cases":
            expect["cases"] = value.split(",")
        else:
            expect[key] = value

    verdict = judge(text, expect)
    print(render(verdict, args.title or str(args.log)), end="")
    if args.json:
        args.json.write_text(json.dumps(verdict, indent=2) + "\n")
    return 0 if verdict["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())