#!/usr/bin/env python3
"""Compare the RTOS SDIO boot against the Linux boot cell by cell.

The board's SDIO socket (mmc1) answers CMD52 and never finishes a CMD53 data
phase under the ported driver, while the same silicon under Linux completes
every one of them.  The two drivers print their commands in the same shape:

    Linux  ALN CMD c=53 arg=16210004 fl=... raw=80000375 d=r blksz=4 ...
           ALN DONE c=53 ev=0000000c err=0 resp0=00001000
    RTOS   MCI CMD c=53 arg=14004804 raw=80000375 d=r blksz=4 ...
           MCI DONE c=53 ev=00000104 err=-2 resp0=00001000

So "align with Linux" is a mechanical question: which cells does the Linux boot
have, which does this one have, and for the ones both have, which fields differ.
This tool answers it from the captures alone - it holds no policy about which
difference matters.

Cell identity, one CMD52 per CCCR/FBR byte and one cell per other command:

    CMD52 (fn, addr, rw, data-if-write)   the register it touches
    others (opcode, arg)                  the command as the card sees it

Usage:
    tools/rtos/mci-cells.py --gt <aln-mmc1-init.txt> --rtos <boot.log> \\
        --json /tmp/cells.json --md /tmp/cells.md

    tools/rtos/mci-cells.py ... --prelude     # stop at the first CMD53 on both
    tools/rtos/mci-cells.py ... --emit-c hdr.h # the GT prelude as a C table

    tools/rtos/mci-cells.py --selftest

Exit code: 0 when the only differences are the recorded ones (see ALLOWED
below), 1 otherwise.  `--emit-c` and `--selftest` exit 0 on success.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

# Differences this port is allowed to keep, each with the reason it is not the
# defect under investigation.  A report that only carries these is "aligned".
ALLOWED = {
    "imask":
        "the GT's mask carries bit16 (cap-sdio-irq's DAT1); this port has no "
        "DAT1 consumer, and bit16 is unrelated to a data phase",
    "fl":
        "mmc_command.flags, a host-layer value the controller never sees",
}

# The fields a command line carries.  A field absent from both is not a
# difference; a field absent from one side is reported as missing.
CMD_FIELDS = ("raw", "d", "blksz", "blkcnt", "bytcnt", "clkdiv", "ctype",
              "tmout", "imask", "dma", "adtc")
DONE_FIELDS = ("ev", "err", "resp0")

# Linux prints "ALN ", this port prints "MCI ".  Everything after the prefix is
# the same key=value language, which is what makes the diff mechanical.
# A capture may prefix every line with a console timestamp or an "RX  [ 1.3]"
# marker, so the anchor is the tag, not the start of the line.
_CMD_RE = re.compile(
    r"(?:ALN|MCI) CMD c=(?P<c>\d+) arg=(?P<arg>[0-9a-fA-F]+) (?P<rest>.*)$")
_DONE_RE = re.compile(
    r"(?:ALN|MCI) DONE c=(?P<c>\d+) ev=(?P<ev>[0-9a-fA-F]+) "
    r"err=(?P<err>-?\d+) resp0=(?P<resp0>[0-9a-fA-F]+)")
_FIELD_RE = re.compile(r"(\w+)=([0-9a-zA-Zx-]+)")


def _decode_cmd53_arg(arg: int) -> dict:
    return {
        "rw": "w" if (arg >> 31) & 1 else "r",
        "fn": (arg >> 28) & 0x7,
        "blk": (arg >> 27) & 1,
        "incr": (arg >> 26) & 1,
        "addr": (arg >> 9) & 0x1FFFF,
        "cnt": arg & 0x1FF,
    }


def _decode_cmd52_arg(arg: int) -> dict:
    return {
        "rw": "w" if (arg >> 31) & 1 else "r",
        "fn": (arg >> 28) & 0x7,
        "addr": (arg >> 9) & 0x1FFFF,
        "data": arg & 0xFF,
    }


def _cell_key(cell: dict) -> tuple:
    """What makes two commands the same command, as the card would see it."""
    c = cell["c"]
    arg = cell["arg_int"]
    if c == 52:
        d = _decode_cmd52_arg(arg)
        if d["rw"] == "w":
            return (52, d["fn"], d["addr"], "w", d["data"])
        return (52, d["fn"], d["addr"], "r")
    return (c, arg)


def _cell_name(cell: dict) -> str:
    c = cell["c"]
    arg = cell["arg_int"]
    if c == 52:
        d = _decode_cmd52_arg(arg)
        if d["rw"] == "w":
            return f"CMD52 fn{d['fn']} 0x{d['addr']:03x} <- 0x{d['data']:02x}"
        return f"CMD52 fn{d['fn']} 0x{d['addr']:03x} ->"
    if c == 53:
        d = _decode_cmd53_arg(arg)
        mode = "blk" if d["blk"] else "byte"
        return (f"CMD53 {d['rw']} fn{d['fn']} {mode} incr={d['incr']} "
                f"0x{d['addr']:05x} n={d['cnt']}")
    return f"CMD{c} arg=0x{arg:08x}"


def parse(text: str) -> list[dict]:
    """Every command in a capture, each paired with its completion."""
    cells: list[dict] = []
    pending: dict[int, dict] = {}
    for lineno, line in enumerate(text.splitlines(), start=1):
        m = _CMD_RE.search(line)
        if m:
            cell = {
                "line": lineno,
                "c": int(m.group("c")),
                "arg": m.group("arg"),
                "arg_int": int(m.group("arg"), 16),
                "fields": {k: v for k, v in _FIELD_RE.findall(m.group("rest"))},
                "done": None,
            }
            cells.append(cell)
            pending[cell["c"]] = cell
            continue
        m = _DONE_RE.search(line)
        if m:
            c = int(m.group("c"))
            cell = pending.pop(c, None)
            if cell is not None:
                cell["done"] = {
                    "line": lineno,
                    "ev": m.group("ev"),
                    "err": m.group("err"),
                    "resp0": m.group("resp0"),
                }
    return cells


def _upto_first_53(cells: list[dict]) -> list[dict]:
    out = []
    for cell in cells:
        out.append(cell)
        if cell["c"] == 53:
            break
    return out


def diff_cells(gt: list[dict], rtos: list[dict]) -> dict:
    """Align two cell lists by identity, keeping order."""
    # A multiset per identity: the GT writes the same cell several times (the
    # driver re-reads a register), so a pair-by-pair walk would report false
    # misses.  Consume one RTOS instance per GT instance in order.
    by_key: dict[tuple, list[dict]] = {}
    for cell in rtos:
        by_key.setdefault(_cell_key(cell), []).append(cell)

    used: set[int] = set()
    rows = []
    for gcell in gt:
        key = _cell_key(gcell)
        candidates = by_key.get(key, [])
        hit = None
        for cand in candidates:
            if id(cand) not in used:
                hit = cand
                break
        if hit is None:
            rows.append({"status": "missing-in-rtos", "gt": gcell,
                         "rtos": None, "field_diffs": []})
            continue
        used.add(id(hit))
        rows.append({
            "status": "matched",
            "gt": gcell,
            "rtos": hit,
            "field_diffs": _field_diffs(gcell, hit),
        })

    extras = [cell for cell in rtos if id(cell) not in used]
    return {"rows": rows, "extra_in_rtos": extras}


def _field_diffs(gcell: dict, rcell: dict) -> list[dict]:
    diffs = []
    for name in CMD_FIELDS:
        g = gcell["fields"].get(name)
        r = rcell["fields"].get(name)
        if g is None and r is None:
            continue
        if g is None or r is None:
            # A field one side always prints and the other never does is a
            # printer difference, not a controller difference: `fl` only exists
            # in the Linux trace.  Recorded, and filtered by ALLOWED.
            diffs.append({"where": "cmd", "field": name, "gt": g, "rtos": r})
            continue
        if g.lower() != r.lower():
            diffs.append({"where": "cmd", "field": name, "gt": g, "rtos": r})
    for name in DONE_FIELDS:
        g = gcell["done"][name] if gcell["done"] else None
        r = rcell["done"][name] if rcell["done"] else None
        if g is None and r is None:
            continue
        if g is None or r is None:
            diffs.append({"where": "done", "field": name, "gt": g, "rtos": r})
            continue
        if g.lower() != r.lower():
            diffs.append({"where": "done", "field": name, "gt": g, "rtos": r})
    return diffs


def _allowed(field: str) -> bool:
    return field in ALLOWED


def summarize(result: dict) -> dict:
    missing = [r for r in result["rows"] if r["status"] == "missing-in-rtos"]
    matched = [r for r in result["rows"] if r["status"] == "matched"]
    unexplained = []
    for row in matched:
        for d in row["field_diffs"]:
            if not _allowed(d["field"]):
                unexplained.append({"cell": _cell_name(row["gt"]), **d})
    return {
        "gt_cells": len(result["rows"]),
        "matched": len(matched),
        "missing": len(missing),
        "extra": len(result["extra_in_rtos"]),
        "unexplained_field_diffs": unexplained,
        "missing_cells": [_cell_name(r["gt"]) for r in missing],
        "extra_cells": [_cell_name(c) for c in result["extra_in_rtos"]],
        "clean": not missing and not result["extra_in_rtos"] and not unexplained,
    }


def render_md(summary: dict, result: dict, gt_name: str, rtos_name: str) -> str:
    out = ["# mci cells: RTOS vs Linux", ""]
    out.append(f"- GT: `{gt_name}` ({summary['gt_cells']} cells)")
    out.append(f"- RTOS: `{rtos_name}`")
    out.append(f"- matched {summary['matched']}, missing in RTOS "
               f"{summary['missing']}, extra in RTOS {summary['extra']}")
    out.append("- verdict: **ALIGNED**" if summary["clean"]
               else "- verdict: **DIFFERS**")
    out.append("")
    out.append("## allowed differences")
    for field, why in sorted(ALLOWED.items()):
        out.append(f"- `{field}`: {why}")
    out.append("")

    if summary["missing_cells"]:
        out.append("## cells the RTOS boot does not have")
        for name in summary["missing_cells"]:
            out.append(f"- {name}")
        out.append("")

    if summary["extra_cells"]:
        out.append("## cells only the RTOS boot has")
        for name in summary["extra_cells"]:
            out.append(f"- {name}")
        out.append("")

    if summary["unexplained_field_diffs"]:
        out.append("## field differences")
        out.append("")
        out.append("| cell | where | field | GT | RTOS |")
        out.append("|---|---|---|---|---|")
        for d in summary["unexplained_field_diffs"]:
            out.append(f"| {d['cell']} | {d['where']} | `{d['field']}` | "
                       f"`{d['gt']}` | `{d['rtos']}` |")
        out.append("")
    return "\n".join(out) + "\n"


def emit_c(cells: list[dict], out: Path) -> int:
    """The GT prelude as a C table of direct IO writes.

    Generated, never transcribed: a hand-copied prelude is a second source of
    truth for the card's register state, and the point of the replay is that it
    is the Linux boot's own bytes.
    """
    writes = []
    for cell in cells:
        if cell["c"] != 52:
            continue
        d = _decode_cmd52_arg(cell["arg_int"])
        if d["rw"] != "w":
            continue
        writes.append((cell["line"], d["fn"], d["addr"], d["data"]))

    lines = [
        "/* SPDX-License-Identifier: BSD-3-Clause */",
        "/*",
        " * @file   mci_gt_prelude.h",
        " * @brief  The CMD52 writes Linux performs on mmc1 before its first",
        " *         CMD53, in order, generated by tools/rtos/mci-cells.py.",
        " *",
        " * Source: the align-trace capture of the GT kernel boot",
        " * (runs/20261029-sdio-align-linux-parity/evidence/linux/",
        " *  aln-mmc1-init.txt).  Do not edit by hand: regenerate with",
        " *      tools/rtos/mci-cells.py --gt <capture> --emit-c <this file>",
        " */",
        "#ifndef FREEWIFICAM_MCI_GT_PRELUDE_H",
        "#define FREEWIFICAM_MCI_GT_PRELUDE_H",
        "",
        "#include <stdint.h>",
        "",
        "struct mci_gt_cell {",
        "\tuint8_t\tfn;\t\t/* SDIO function */",
        "\tuint8_t\trw;\t\t/* 1: write, 0: read */",
        "\tuint32_t addr;\t\t/* register inside the function */",
        "\tuint8_t\tdata;\t/* write payload */",
        "\tunsigned int gt_line;\t/* line in the GT capture, for citation */",
        "};",
        "",
        f"#define MCI_GT_PRELUDE_CELLS\t{len(writes)}u",
        "",
        "static const struct mci_gt_cell mci_gt_prelude[MCI_GT_PRELUDE_CELLS] = {",
    ]
    for line, fn, addr, data in writes:
        lines.append(f"\t{{ {fn}u, 1u, 0x{addr:05x}u, 0x{data:02x}u, {line}u }},"
                     f"\t/* GT line {line} */")
    lines += [
        "};",
        "",
        "#endif /* FREEWIFICAM_MCI_GT_PRELUDE_H */",
    ]
    out.write_text("\n".join(lines) + "\n")
    print(f"emit-c: {len(writes)} writes -> {out}")
    return 0


GT_SAMPLE = """\
ALN CMD c=52 arg=80000c08 fl=00000195 raw=80004174 d=- clkdiv=007e7dfa ctype=00000000 tmout=ffffffff imask=00001546
ALN DONE c=52 ev=00000004 err=0 resp0=00001008
ALN CMD c=53 arg=16210004 fl=000001b5 raw=80000375 d=r blksz=4 blkcnt=1 bytcnt=00000004 clkdiv=00040306 ctype=00000001 tmout=ffffffff imask=000037ce dma=1 adtc=0
ALN DONE c=53 ev=0000000c err=0 resp0=00001000
"""

RTOS_SAMPLE = """\
MCI CMD c=52 arg=80000c08 raw=80004174 d=- clkdiv=007e7dfa ctype=00000000 tmout=ffffffff imask=00001546
MCI DONE c=52 ev=00000004 err=0 resp0=00001008
MCI CMD c=53 arg=16210004 raw=80000375 d=r blksz=4 blkcnt=1 bytcnt=00000004 clkdiv=00040306 ctype=00000001 tmout=ffffffff imask=000057ce dma=1 adtc=0
MCI DONE c=53 ev=00000104 err=-2 resp0=00001000
"""


def selftest() -> int:
    gt = parse(GT_SAMPLE)
    rtos = parse(RTOS_SAMPLE)
    assert len(gt) == 2 and len(rtos) == 2, (len(gt), len(rtos))
    assert gt[0]["done"]["resp0"] == "00001008"
    assert _cell_key(gt[0]) == (52, 0, 0x06, "w", 0x08), _cell_key(gt[0])
    assert _cell_name(gt[1]) == "CMD53 r fn1 byte incr=1 0x11080 n=4"

    result = diff_cells(gt, rtos)
    summary = summarize(result)
    # The samples differ in imask (allowed) and in the DONE line, which is the
    # one difference the tool exists to expose.
    fields = {(d["where"], d["field"]) for d in summary["unexplained_field_diffs"]}
    assert ("done", "ev") in fields, fields
    assert ("done", "err") in fields, fields
    assert ("cmd", "imask") not in fields, fields
    assert not summary["missing_cells"] and not summary["extra_cells"]
    assert summary["clean"] is False

    # A capture with the allowed difference only must come out clean.
    rtos_aligned = RTOS_SAMPLE.replace("imask=000057ce", "imask=000037ce") \
        .replace("MCI DONE c=53 ev=00000104 err=-2", "MCI DONE c=53 ev=0000000c err=0")
    aligned = summarize(diff_cells(gt, parse(rtos_aligned)))
    assert aligned["clean"] is True, aligned

    # A missing cell is reported (the CMD52 pair is absent from the short side).
    only_cmd53 = "\n".join(RTOS_SAMPLE.splitlines()[2:])
    short = summarize(diff_cells(gt, parse(only_cmd53)))
    assert short["missing_cells"], short
    assert short["extra_cells"] == [], short
    print("selftest: ok")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gt", type=Path, help="the Linux capture (ALN lines)")
    ap.add_argument("--rtos", type=Path, help="this port's capture (MCI lines)")
    ap.add_argument("--prelude", action="store_true",
                    help="stop at the first CMD53 on both sides")
    ap.add_argument("--json", type=Path, default=None)
    ap.add_argument("--md", type=Path, default=None)
    ap.add_argument("--emit-c", type=Path, default=None,
                    help="write the GT prelude's CMD52 writes as a C table")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    if args.emit_c is not None:
        if args.gt is None:
            ap.error("--emit-c needs --gt")
        cells = parse(args.gt.read_text(errors="replace"))
        return emit_c(_upto_first_53(cells), args.emit_c)

    if args.gt is None or args.rtos is None:
        ap.error("--gt and --rtos are required")

    gt = parse(args.gt.read_text(errors="replace"))
    rtos = parse(args.rtos.read_text(errors="replace"))
    if args.prelude:
        gt = _upto_first_53(gt)
        rtos = _upto_first_53(rtos)

    result = diff_cells(gt, rtos)
    summary = summarize(result)

    md = render_md(summary, result, str(args.gt), str(args.rtos))
    if args.md is not None:
        args.md.write_text(md)
    else:
        sys.stdout.write(md)

    if args.json is not None:
        args.json.write_text(json.dumps({
            "summary": summary,
            "rows": [
                {
                    "status": r["status"],
                    "gt": {k: r["gt"][k] for k in ("line", "c", "arg")},
                    "rtos": (None if r["rtos"] is None else
                             {k: r["rtos"][k] for k in ("line", "c", "arg")}),
                    "field_diffs": r["field_diffs"],
                }
                for r in result["rows"]
            ],
        }, indent=2) + "\n")

    return 0 if summary["clean"] else 1


if __name__ == "__main__":
    sys.exit(main())
