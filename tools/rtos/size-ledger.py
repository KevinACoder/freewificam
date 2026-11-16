#!/usr/bin/env python3
"""Size ledger for one RTOS image (or one C0 object directory).

The image is a link product: a per-object ``size`` sum overcounts because the
linker folds ``.rodata.str*`` merge sections and drops ``--gc-sections``
sections.  This tool reads the link map as the authority for what was placed,
attributes every placed input section to its object once (an address-interval
union, so a merged string block is not double counted), and reconciles the
result with the ELF section sizes and the flat ``.bin``.

Two modes:

  image   --elf <elf> [--map <map>] [--bin <bin>] [--profile <name>]
          The map is parsed for placement and discarded-section accounting.
          Without a map only the canonical ELF section sizes are printed.

  objects --objects <dir> [--profile <name>]
          Pre-link C0 accounting: every ``*.o`` under the directory is summed
          with ``size -A``.  This is the mode for a vendored driver that is
          compiled but not yet linked (no board SDIO glue yet).

The ledger is meant to be run before and after a size lever; ``--json`` keeps
the two runs comparable.  ``--max-flash`` is a warning by default and a gate
with ``--strict``.

Usage:
  tools/rtos/size-ledger.py --profile stm32f429-wifi-c6 \
      --elf tests/board/rtos/build/build/rtos-freertos.elf \
      --map tests/board/rtos/build/build/rtos-freertos.map
  tools/rtos/size-ledger.py --objects runs/S1/driver-rtl8189fs/run-1
  tools/rtos/size-ledger.py ... --json runs/2026-11-16-f429-size/ledger.json
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# Input section names that carry executable/constant bytes.  ``.rodata`` is
# folded into the ``.text`` output section by the linker scripts, so the output
# section name alone is not enough: classify by the input section name.
TEXT_PREFIXES = (
    ".text", ".isr_vector", ".lkpi_module", ".lkpi_init",
    ".lkpi_sdio_driver", ".usbh_class_info", ".board_tests",
    ".ARM.exidx", ".ARM.extab", ".init", ".vectors",
)
RODATA_PREFIXES = (".rodata", ".ARM.attributes", ".comment")
DATA_PREFIXES = (".data",)
BSS_PREFIXES = (".bss", ".noncacheable", ".sdram", ".COMMON")
DEBUG_PREFIXES = (".debug",)

# Output sections that are NOLOAD/RAM, not flash.  Used only for the
# reconciliation note; input sections are still classified by their own name.
RAM_OUTPUTS = (".bss", ".noncacheable", ".sdram")

OUT_SEC_RE = re.compile(
    r"^(\.[A-Za-z0-9_.$]+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)"
    r"(?:\s+load address\s+0x[0-9a-fA-F]+)?\s*$"
)
IN_ONE_RE = re.compile(
    r"^\s+(\.?[A-Za-z0-9_.$]+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\S+)\s*$"
)
IN_TWO_NAME_RE = re.compile(r"^\s+(\.[A-Za-z0-9_.$]+)\s*$")
IN_TWO_ROW_RE = re.compile(
    r"^\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\S+)\s*$"
)
FILL_RE = re.compile(r"^\s+\*fill\*\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s*$")
SIZE_A_RE = re.compile(r"^(\S+)\s+(\d+)\s+(\S+)\s*$")
SIZE_LINE_RE = re.compile(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+\d+\s+[0-9a-fA-F]+\s+(\S+)\s*$")


def run(cmd: list[str]) -> str:
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"size-ledger: {' '.join(cmd)} failed:\n{proc.stderr.strip()}")
    return proc.stdout


def classify(in_sec: str) -> str:
    name = in_sec
    if name in ("COMMON", ".COMMON"):
        return "bss"
    for prefix in DEBUG_PREFIXES:
        if name.startswith(prefix):
            return "debug"
    for prefix in BSS_PREFIXES:
        if name.startswith(prefix):
            return "bss"
    for prefix in DATA_PREFIXES:
        if name.startswith(prefix):
            return "data"
    for prefix in RODATA_PREFIXES:
        if name.startswith(prefix):
            return "rodata"
    for prefix in TEXT_PREFIXES:
        if name.startswith(prefix):
            return "text"
    return "other"


def is_merge_rodata(sec: str) -> bool:
    """True for the linker-mergeable string/constant sections.

    GCC emits ``.rodata.str1.1`` and ``.rodata.cst*`` with SHF_MERGE; the
    linker folds equal suffixes into one block, so a per-object sum double
    counts them.  The map's addresses make that visible (several input
    sections share one address); the ledger attributes the whole block to one
    synthetic group instead of pretending the first object owns it.
    """
    return ".rodata" in sec and (".str" in sec or ".cst" in sec)


def group_of(path: str) -> str:
    """The one grouping the round's decisions are made on."""
    p = path.replace(str(REPO) + "/", "")
    # C0 object directories are flat: the S1b tool names each object after its
    # translation unit with '/' replaced by '_'.  Recognise the component
    # tokens before the path-based rules.
    m = re.search(r"wireless-drivers_([a-z0-9]+)_", p)
    if m:
        return f"drv:{m.group(1)}"
    if "components_liblinux80211_stack_mac80211_" in p:
        return "mac80211"
    if "components_liblinux80211_stack_wireless_" in p:
        return "cfg80211"
    if "components_liblinux80211_" in p:
        return "linuxkpi+bus+skb"
    if "third-party_lwip_" in p:
        return "lwIP"
    if "third-party_cherryusb_" in p:
        return "CherryUSB"
    if p.startswith("components/liblinux80211/fw/"):
        return "fw-blobs"
    if "components/liblinux80211/stack/mac80211/" in p:
        return "mac80211"
    if "components/liblinux80211/stack/wireless/" in p:
        return "cfg80211"
    m = re.search(r"wireless-drivers/([^/]+)/", p)
    if m:
        return f"drv:{m.group(1)}"
    if "components/liblinux80211" in p:
        return "linuxkpi+bus+skb"
    if "third-party/lwip" in p:
        return "lwIP"
    if "third-party/cherryusb" in p:
        return "CherryUSB"
    if "third-party/FreeRTOS-Kernel" in p or "CMSIS-FreeRTOS" in p:
        return "FreeRTOS"
    if "third-party/printf" in p or "third-party/ulog" in p:
        return "support"
    if "tests/board/rtos/app" in p:
        return "app/cases"
    if "tests/board/rtos/platform" in p:
        return "platform"
    if "libgcc" in p or "libc.a" in p or "newlib" in p:
        return "libc/libgcc"
    return "other"


def parse_map(path: Path) -> tuple[list[dict], list[dict], dict[str, int]]:
    """Return (placed rows, discarded rows, output origins).

    A placed row is ``{out, sec, addr, size, path}``; a discarded row has
    ``out=None``.  The parser only trusts the map's structural lines: output
    section headers at column 0, input rows in the two shapes ld emits, and
    the one-line ``*fill*`` marker.
    """
    lines = path.read_text(errors="ignore").splitlines()
    try:
        start = next(i for i, line in enumerate(lines)
                     if line.startswith("Linker script and memory map"))
    except StopIteration:
        raise SystemExit(f"size-ledger: {path} has no 'Linker script and memory map'")

    discarded_start = next((i for i, line in enumerate(lines)
                            if line.startswith("Discarded input sections")), None)
    discarded_end = next((i for i, line in enumerate(lines)
                          if line.startswith("Memory Configuration")), len(lines))

    def rows_from(block: list[str], out_sec: str | None) -> list[dict]:
        rows: list[dict] = []
        pending: str | None = None
        for line in block:
            m = IN_ONE_RE.match(line)
            if m and m.group(4).startswith("/"):
                rows.append({
                    "out": out_sec, "sec": m.group(1),
                    "addr": int(m.group(2), 16), "size": int(m.group(3), 16),
                    "path": m.group(4),
                })
                pending = None
                continue
            m = IN_TWO_NAME_RE.match(line)
            if m:
                pending = m.group(1)
                continue
            m = IN_TWO_ROW_RE.match(line)
            if m and pending is not None:
                addr = int(m.group(1), 16)
                size = int(m.group(2), 16)
                # A two-line row is an input section only when its size is
                # non-zero and it names an object; symbol rows have no path.
                if m.group(3).startswith("/") and size:
                    rows.append({
                        "out": out_sec, "sec": pending,
                        "addr": addr, "size": size, "path": m.group(3),
                    })
                pending = None
                continue
        return rows

    discarded = rows_from(lines[discarded_start:discarded_end], None) if discarded_start else []

    # The memory map: an output section header at column 0, then its input
    # rows until the next output section header.
    placed: list[dict] = []
    origins: dict[str, int] = {}
    out_sec = None
    block: list[str] = []

    def flush() -> None:
        nonlocal block
        if out_sec is not None:
            placed.extend(rows_from(block, out_sec))
        block = []

    for line in lines[start:]:
        m = OUT_SEC_RE.match(line)
        if m and not line.startswith(" "):
            flush()
            out_sec = m.group(1)
            origins[out_sec] = int(m.group(2), 16)
            block = []
            continue
        if out_sec is not None:
            fm = FILL_RE.match(line)
            if fm:
                # Keep fill as a row with no path; the interval union accounts
                # for the bytes that are not covered by any input section.
                placed.append({
                    "out": out_sec, "sec": "*fill*",
                    "addr": int(fm.group(1), 16), "size": int(fm.group(2), 16),
                    "path": None,
                })
                continue
            block.append(line)
    flush()
    return placed, discarded, origins


def elf_sections(elf: Path, size_tool: str) -> dict[str, int]:
    out = run([size_tool, "-A", str(elf)])
    secs: dict[str, int] = {}
    for line in out.splitlines():
        m = SIZE_A_RE.match(line)
        if not m:
            continue
        name, size = m.group(1), int(m.group(2))
        if name.startswith("."):
            secs[name] = size
    return secs


def elf_totals(elf: Path, size_tool: str) -> tuple[int, int, int]:
    out = run([size_tool, str(elf)])
    for line in out.splitlines():
        m = SIZE_LINE_RE.match(line)
        if m:
            return int(m.group(1)), int(m.group(2)), int(m.group(3))
    raise SystemExit(f"size-ledger: cannot read text/data/bss from {size_tool} {elf}")


def image_ledger(elf: Path, map_path: Path | None, bin_path: Path | None,
                 size_tool: str, profile: str, top: int) -> dict:
    secs = elf_sections(elf, size_tool)
    text, data, bss = elf_totals(elf, size_tool)
    bin_size = bin_path.stat().st_size if bin_path and bin_path.is_file() else None

    groups: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    out_sizes: dict[str, int] = {}
    flash_outputs: set[str] = set()
    placed: list[dict] = []
    discarded: list[dict] = []
    origins: dict[str, int] = {}
    if map_path is not None:
        placed, discarded, origins = parse_map(map_path)
        for row in placed:
            out = row["out"]
            if out is not None:
                out_sizes.setdefault(out, 0)
        # Output section header sizes, from the ELF sections themselves: the
        # map header text is authoritative but parsing the duplicate header
        # line is fragile; the ELF section table has the same number.
        for name, size in secs.items():
            out_sizes[name] = size
        # Flash-bearing output sections: the ELF's `.text` (which folds in
        # .rodata and .ARM) plus the small registry sections the linker
        # script places right after it.  `.data` is flash-backed through its
        # LMA; `.bss`/`.sdram` are NOLOAD and never flash.
        for name in out_sizes:
            if name in (".text", ".isr_vector", ".ARM", ".lkpi_module",
                        ".lkpi_init", ".lkpi_sdio_driver", ".usbh_class_info",
                        ".board_tests", ".glue_7", ".glue_7t", ".vfp11_veneer",
                        ".v4_bx", ".iplt", ".rel.dyn", ".data"):
                flash_outputs.add(name)

    # Interval union per output section.  A bytearray at the section's own
    # offset is exact and makes merged `.rodata.str*` blocks count once.
    covered: dict[str, bytearray] = {}
    for row in placed:
        out = row["out"]
        if out not in out_sizes or row["sec"] == "*fill*":
            continue
        if out not in covered:
            covered[out] = bytearray(out_sizes[out])
    fill_by_out: dict[str, int] = defaultdict(int)
    dedup_by_out: dict[str, int] = defaultdict(int)
    overlap_by_out: dict[str, int] = defaultdict(int)

    for row in placed:
        out = row["out"]
        sec = row["sec"]
        if out not in covered:
            continue
        if sec == "*fill*":
            # Fill is not an input section; it is accounted at reconciliation.
            continue
        base = out_sizes.get(out, 0)
        start = row["addr"] - origins.get(out, 0)
        size = row["size"]
        if start < 0 or start >= base:
            continue
        end = min(start + size, base)
        new = 0
        buf = covered[out]
        for i in range(start, end):
            if buf[i] == 0:
                buf[i] = 1
                new += 1
        cat = classify(sec)
        if new == 0:
            continue
        if size > new:
            if cat == "rodata":
                dedup_by_out[out] += size - new
            else:
                overlap_by_out[out] += size - new
        if cat == "rodata" and is_merge_rodata(sec):
            groups["merged-rodata"][cat] += new
        else:
            groups[group_of(row["path"])][cat] += new
        groups["_total"][cat] += new

    # Bytes not covered by any input row are alignment/fill.
    fill_total = 0
    ram_fill = 0
    for out, buf in covered.items():
        fill = len(buf) - sum(buf)
        if out in flash_outputs:
            fill_total += fill
        else:
            ram_fill += fill

    disc_by_group: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    disc_total = 0
    for row in discarded:
        cat = classify(row["sec"])
        disc_by_group[group_of(row["path"])][cat] += row["size"]
        disc_total += row["size"]

    # ELF canonical categories: `.text` includes folded .rodata and .ARM
    # sections.  The ledger's categories are input-section based, so compare
    # text+rodata+data+other(flash) with the ELF text+data, and say so.
    group_rows = []
    for name, cats in groups.items():
        if name == "_total":
            continue
        flash = sum(cats.get(k, 0) for k in ("text", "rodata", "data", "other"))
        group_rows.append({
            "group": name,
            "text": cats.get("text", 0),
            "rodata": cats.get("rodata", 0),
            "data": cats.get("data", 0),
            "other": cats.get("other", 0),
            "bss": cats.get("bss", 0),
            "flash": flash,
        })
    group_rows.sort(key=lambda r: (-r["flash"], r["group"]))
    total = groups.get("_total", {})
    flash_sum = sum(total.get(k, 0) for k in ("text", "rodata", "data", "other"))

    return {
        "schema": 1,
        "mode": "image",
        "profile": profile,
        "elf": str(elf),
        "map": str(map_path) if map_path else None,
        "bin": str(bin_path) if bin_path else None,
        "bin_size": bin_size,
        "canonical": {"text": text, "data": data, "bss": bss},
        "sections": secs,
        "groups": group_rows,
        "group_totals": {
            "text": total.get("text", 0),
            "rodata": total.get("rodata", 0),
            "data": total.get("data", 0),
            "other": total.get("other", 0),
            "bss": total.get("bss", 0),
            "flash": flash_sum,
        },
        "reconciliation": {
            "elf_text_plus_data": text + data,
            "ledger_flash": flash_sum,
            "ledger_flash_with_fill": flash_sum + fill_total,
            "delta": (text + data) - (flash_sum + fill_total),
            "fill_unattributed": fill_total,
            "ram_fill": ram_fill,
            "merge_dedup": sum(dedup_by_out.values()),
            "overlap_nonmerge": sum(overlap_by_out.values()),
        },
        "discarded": {
            "total": disc_total,
            "flash": sum(
                row["size"] for row in discarded
                if classify(row["sec"]) in ("text", "rodata", "data", "other")
            ),

            "groups": [
                {"group": g, "bytes": sum(c.values()),
                 "text": c.get("text", 0), "rodata": c.get("rodata", 0)}
                for g, c in sorted(disc_by_group.items(), key=lambda kv: -sum(kv[1].values()))
            ],
        },
        "top": top,
    }


def objects_ledger(root: Path, size_tool: str, profile: str, top: int) -> dict:
    objects = sorted(root.rglob("*.o"))
    if not objects:
        raise SystemExit(f"size-ledger: no *.o under {root}")
    # S1b writes summary.tsv beside obj/; its `tu` column is the real source
    # path, which is what the grouping rules understand.
    tu_by_obj: dict[str, str] = {}
    summary = root.parent / "summary.tsv"
    if summary.is_file():
        for line in summary.read_text(errors="ignore").splitlines()[1:]:
            cells = line.split("\t")
            if len(cells) < 2:
                continue
            tu = cells[0]
            tu_by_obj[tu.replace("/", "_") + ".o"] = tu
    groups: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    total: dict[str, int] = defaultdict(int)
    failures = 0
    for obj in objects:
        try:
            out = run([size_tool, "-A", str(obj)])
        except SystemExit:
            failures += 1
            continue
        g = group_of(tu_by_obj.get(obj.name, str(obj)))
        for line in out.splitlines():
            m = SIZE_A_RE.match(line)
            if not m:
                continue
            sec, size = m.group(1), int(m.group(2))
            if not sec.startswith("."):
                continue
            cat = classify(sec)
            if cat == "debug":
                continue
            groups[g][cat] += size
            total[cat] += size
    rows = []
    for name, cats in groups.items():
        flash = sum(cats.get(k, 0) for k in ("text", "rodata", "data", "other"))
        rows.append({
            "group": name, "text": cats.get("text", 0),
            "rodata": cats.get("rodata", 0), "data": cats.get("data", 0),
            "bss": cats.get("bss", 0), "flash": flash,
        })
    rows.sort(key=lambda r: (-r["flash"], r["group"]))
    return {
        "schema": 1, "mode": "objects", "profile": profile,
        "root": str(root), "object_count": len(objects),
        "read_failures": failures,
        "group_totals": {
            "text": total.get("text", 0), "rodata": total.get("rodata", 0),
            "data": total.get("data", 0), "bss": total.get("bss", 0),
            "flash": sum(total.get(k, 0) for k in ("text", "rodata", "data", "other")),
        },
        "groups": rows,
    }


def print_image(doc: dict, limit: int | None, strict: bool) -> int:
    c = doc["canonical"]
    bin_size = doc["bin_size"]
    print(f"profile: {doc['profile']}")
    print(f"mode: image")
    print(f"elf: {doc['elf']}")
    if doc["map"]:
        print(f"map: {doc['map']}")
    print(f"canonical: text={c['text']} data={c['data']} bss={c['bss']} "
          f"text+data={c['text'] + c['data']}")
    if bin_size is not None:
        free = limit - bin_size if limit else None
        tail = f" free={free}" if free is not None else ""
        print(f"bin: {bin_size} bytes ({bin_size / 1024.0:.1f} KiB){tail}")
    print()
    print(f"{'group':22s} {'text':>9s} {'rodata':>9s} {'data':>7s} "
          f"{'flash':>9s} {'bss':>9s}")
    for row in doc["groups"]:
        print(f"{row['group']:22s} {row['text']:9d} {row['rodata']:9d} "
              f"{row['data']:7d} {row['flash']:9d} {row['bss']:9d}")
    t = doc["group_totals"]
    print(f"{'TOTAL':22s} {t['text']:9d} {t['rodata']:9d} {t['data']:7d} "
          f"{t['flash']:9d} {t['bss']:9d}")
    r = doc["reconciliation"]
    print()
    print(f"reconcile: elf_text+data={r['elf_text_plus_data']} "
          f"ledger_flash={r['ledger_flash']} (+fill {r['fill_unattributed']} = "
          f"{r['ledger_flash_with_fill']}) delta={r['delta']:+d} "
          f"merge_dedup={r['merge_dedup']} "
          f"nonmerge_overlap={r['overlap_nonmerge']} ram_fill={r['ram_fill']}")
    d = doc["discarded"]
    print(f"discarded: {d['total']} bytes (flash-bearing {d['flash']})")
    for row in d["groups"][:8]:
        print(f"  {row['group']:22s} {row['bytes']:8d}")
    if limit is not None and bin_size is not None:
        over = bin_size > limit
        if over:
            print(f"BUDGET: FAIL bin {bin_size} > {limit} bytes")
            return 1 if strict else 0
        print(f"BUDGET: ok bin {bin_size} <= {limit} bytes")
    return 0


def print_objects(doc: dict) -> int:
    print(f"profile: {doc['profile']}")
    print(f"mode: objects  root={doc['root']}  objects={doc['object_count']} "
          f"read_failures={doc['read_failures']}")
    print(f"{'group':28s} {'text':>9s} {'rodata':>9s} {'data':>7s} {'flash':>9s}")
    for row in doc["groups"]:
        print(f"{row['group']:28s} {row['text']:9d} {row['rodata']:9d} "
              f"{row['data']:7d} {row['flash']:9d}")
    t = doc["group_totals"]
    print(f"{'TOTAL':28s} {t['text']:9d} {t['rodata']:9d} {t['data']:7d} "
          f"{t['flash']:9d}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--profile", default="unknown")
    ap.add_argument("--elf")
    ap.add_argument("--map")
    ap.add_argument("--bin")
    ap.add_argument("--objects")
    ap.add_argument("--size-tool", default=os.environ.get("SIZE", "arm-none-eabi-size"))
    ap.add_argument("--max-flash", type=int, default=None,
                    help="flash limit in KiB")
    ap.add_argument("--strict", action="store_true",
                    help="return 1 when the bin exceeds --max-flash")
    ap.add_argument("--check-budget", action="store_true",
                    help="alias of --strict: the budget check is the exit status")
    ap.add_argument("--json", help="write the ledger JSON here")
    ap.add_argument("--top", type=int, default=20)
    args = ap.parse_args()

    if args.objects:
        doc = objects_ledger(Path(args.objects), args.size_tool, args.profile, args.top)
        rc = print_objects(doc)
    elif args.elf:
        elf = Path(args.elf)
        if not elf.is_file():
            raise SystemExit(f"size-ledger: no ELF {elf}")
        bin_path = Path(args.bin) if args.bin else elf.with_suffix(".bin")
        doc = image_ledger(elf, Path(args.map) if args.map else None,
                           bin_path, args.size_tool, args.profile, args.top)
        limit = args.max_flash * 1024 if args.max_flash else None
        rc = print_image(doc, limit, args.strict or args.check_budget)
    else:
        ap.error("pass --elf (image mode) or --objects (C0 mode)")

    if args.json:
        out = Path(args.json)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(doc, indent=2, sort_keys=True) + "\n")
        print(f"json: {out}", file=sys.stderr)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
