#!/usr/bin/env python3
"""Every Kconfig symbol must be referenced somewhere in the tree.

The 2026-11-16 size round found symbols that had been dead for a while
(``HEAP_SIZE`` was a ThreadX link symbol no FreeRTOS image ever passed;
``USB_STAGE_SIZE`` had no consumer at all).  This check makes an orphaned
symbol a build failure instead of a comment nobody re-reads.

It is a floor, not a proof: a mention in a comment counts as a reference.
The C/header/make/linker/tool files scanned are the ones that can consume a
value; generated files and ``build/`` are excluded.

Usage:
  tools/build/check-config-used.py [--root <repo>] [--json]
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "kconfig"))

import kconfiglib  # noqa: E402

# Symbols that are intentionally not named by a consumer: none at present.
ALLOW = set()

SCAN_ROOTS = (
    "tests/board/rtos",
    "components/liblinux80211",
    "third-party/lwip/ports",
    "third-party/printf",
    "third-party/ulog",
    "tools/build",
    "tools/rtos",
    "wireless-drivers/rtl8189fs",
    "wireless-drivers/rtl8xxxu",
    "wireless-drivers/rtw88",
    "wireless-drivers/rtw89",
    "wireless-drivers/iwlwifi",
)
SUFFIXES = (".c", ".h", ".mk", ".ld", ".py", ".sh", ".Kconfig")
NAMES = {"Makefile", "sources.mk", "Kconfig"}


def source_text(root: Path) -> str:
    parts: list[str] = []
    for rel in SCAN_ROOTS:
        base = root / rel
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if not path.is_file():
                continue
            if "build" in path.parts or "__pycache__" in path.parts:
                continue
            if path.suffix not in SUFFIXES and path.name not in NAMES:
                continue
            try:
                parts.append(path.read_text(errors="ignore"))
            except OSError:
                continue
    return "\n".join(parts)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", default=str(REPO))
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    root = Path(args.root)
    kconf = kconfiglib.Kconfig(str(root / "Kconfig"), warn_to_stderr=False)
    text = source_text(root)

    missing = []
    for sym in kconf.unique_defined_syms:
        if sym.choice is not None:
            continue          # a choice's members are selected by the choice
        if sym.name.startswith("_") or sym.name in ALLOW:
            continue
        pat = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(sym.name) +
                         r"(?![A-Za-z0-9_])")
        if not pat.search(text):
            missing.append(sym.name)

    if args.json:
        print(json.dumps({"symbols": len(kconf.unique_defined_syms),
                          "missing": sorted(missing)}, indent=2))
    elif missing:
        print("check-config-used: symbols with no consumer:", file=sys.stderr)
        for name in sorted(missing):
            print("  " + name, file=sys.stderr)
    else:
        print("ok: every Kconfig symbol has a consumer "
              f"({len(kconf.unique_defined_syms)} symbols)")
    if kconf.warnings:
        print("Kconfig warnings:", *kconf.warnings, sep="\n  ", file=sys.stderr)
        return 1
    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
