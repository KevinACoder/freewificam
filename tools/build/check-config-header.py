#!/usr/bin/env python3
"""Check that freewificam_config.h defines every symbol of the Kconfig tree.

The generated header is the only C-side configuration channel: a missing
symbol must be a compile error, not a silent #ifndef fallback.  This checks the
generated file structurally (it is not a replacement for compiling the image).
"""
from __future__ import annotations
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "kconfig"))
import kconfiglib  # noqa: E402

def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check-config-header.py <freewificam_config.h>", file=sys.stderr)
        return 2
    header = Path(sys.argv[1])
    if not header.is_file():
        print(f"missing header: {header}", file=sys.stderr)
        return 1
    text = header.read_text()
    kconf = kconfiglib.Kconfig(str(REPO / "Kconfig"), warn_to_stderr=False)
    missing = []
    for sym in kconf.unique_defined_syms:
        if sym.name.startswith("_"):
            continue
        if not re.search(rf"^(#define CONFIG_{sym.name}\b.*|/\* CONFIG_{sym.name} is not set \*/)$",
                         text, flags=re.M):
            missing.append(sym.name)
    if kconf.warnings:
        print("Kconfig warnings:", *kconf.warnings, sep="\n  ", file=sys.stderr)
        return 1
    if missing:
        print("missing symbols in generated header:", ", ".join(sorted(missing)),
              file=sys.stderr)
        return 1
    print(f"ok: {header} covers {len(kconf.unique_defined_syms)} Kconfig symbols")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
