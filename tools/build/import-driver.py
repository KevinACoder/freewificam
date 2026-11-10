#!/usr/bin/env python3
"""Import a verbatim wireless driver tree into this repository, or verify it.

The driver is carried byte-identical to the GT pin, the same discipline the
stack import uses (tools/build/import-stack.py): the whole upstream directory,
every file, no local edit.  The *compiled* set is a separate answer: it is
re-derived from the driver's own Makefiles under the feature face this line
takes (components/liblinux80211/include/linux80211/driver_config.h), and it is
written into the driver's sources.mk for the platform Makefile to consume.

  tools/build/import-driver.py --driver iwlwifi
  tools/build/import-driver.py --driver iwlwifi --check
  tools/build/import-driver.py --driver iwlwifi --emit-sources-block

The tree lands in wireless-drivers/<driver>/; IMPORT.tsv records one row per
file (source commit, source path, destination, sha256, lines, kind, license)
and PROVENANCE.md states the pin, the licence and the compiled set.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DRIVERS_ROOT = REPO / "wireless-drivers"

# The feature face the compiled set is derived under.  This is the same answer
# include/linux80211/driver_config.h states for the preprocessor: IWLMVM on,
# the optional features off.  If one of those changes, both spellings change
# together (the --check run compares this set against the face header).
FEATURES = {
    "IWLMVM": True,
    "IWLDVM": False,
    "IWLMLD": False,
    "IWLMEI": False,
    "IWLWIFI_DEBUGFS": False,
    "IWLWIFI_DEVICE_TRACING": False,
    "IWLWIFI_LEDS": False,
    "PM_SLEEP": False,
    "ACPI": False,
    "EFI": False,
}

# One entry per driver: where its sources live under the upstream tree, the
# Makefiles that name the translation units, and the subdirectories whose
# Makefiles own a separate list.
DRIVERS = {
    "iwlwifi": {
        "upstream": "drivers/net/wireless/intel/iwlwifi",
        "makefiles": ("Makefile", "mvm/Makefile"),
        "subdirs": ("mvm/",),
        "want": 72,
    },
}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def spdx(path: Path) -> str:
    with path.open("r", errors="replace") as f:
        for _ in range(3):
            line = f.readline()
            if "SPDX-License-Identifier:" in line:
                return line.split("SPDX-License-Identifier:")[1].split("*/")[0].strip()
    return "unknown"


def _config_enabled(symbol: str) -> bool:
    symbol = symbol.removeprefix("CONFIG_")
    return FEATURES.get(symbol, False)


def _objects(text: str, subdir: str) -> list[str]:
    """The .o names a Makefile enables under FEATURES, in file order."""
    out: list[str] = []
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        m = re.match(r"^(?:[\w-]+-objs|\w+-y)\s*\+=\s*(.+)$", line)
        if m:
            names = re.findall(r"([A-Za-z0-9_/-]+)\.o", m.group(1))
            out += [subdir + n for n in names]
            continue
        m = re.match(r"^[\w-]+-\$\(CONFIG_([A-Z0-9_]+)\)\s*\+=\s*(.+)$", line)
        if m:
            if not _config_enabled(m.group(1)):
                continue
            names = re.findall(r"([A-Za-z0-9_/-]+)\.o", m.group(2))
            out += [subdir + n for n in names]
    return out


def upstream_tus(src: Path, driver: str) -> list[str]:
    spec = DRIVERS[driver]
    tus: list[str] = []
    for rel in spec["makefiles"]:
        text = (src / rel).read_text()
        subdir = rel.rsplit("/", 1)[0] + "/" if "/" in rel else ""
        for name in _objects(text, subdir):
            # A Makefile may name a directory's objects as `dvm/` (the
            # `obj-$(CONFIG_IWLDVM) += dvm/` line); those are disabled by the
            # feature face and never reach here.
            tus.append(name + ".c")
    # Drop duplicates while keeping the first position: one unit is listed by
    # two Makefiles only when a feature moved it, and the feature face decided.
    # A module aggregate (`iwlwifi.o`, `iwlmvm.o`) is a link target, not a
    # translation unit: it has no .c of the same name and is filtered here.
    module_names = {"iwlwifi", "iwlmvm", "iwldvm", "iwlmld", "mvm/iwlmvm"}
    seen: set[str] = set()
    ordered: list[str] = []
    for tu in tus:
        if tu in seen:
            continue
        if not (src / tu).is_file():
            if tu.removesuffix(".c") in module_names:
                continue
            raise SystemExit(f"{driver}: Makefile names a missing unit {tu}")
        seen.add(tu)
        ordered.append(tu)
    want = spec["want"]
    if len(ordered) != want:
        raise SystemExit(
            f"{driver}: derived {len(ordered)} TUs, want {want}: {ordered}"
        )
    return ordered


def all_files(src: Path) -> list[str]:
    return sorted(
        str(p.relative_to(src))
        for p in src.rglob("*")
        if p.is_file()
    )


def manifest_rows(upstream: Path, driver: str, files: list[str],
                  tus: list[str]) -> list[str]:
    commit = subprocess.run(
        ["git", "-C", str(upstream), "rev-parse", "HEAD"],
        text=True, capture_output=True, check=True,
    ).stdout.strip()
    rows = [
        "# Imported driver files: source commit, source path, destination, "
        "sha256, lines, kind, license.",
        f"# Written by tools/build/import-driver.py (--driver {driver}); --check re-derives it.",
    ]
    tu_set = set(tus)
    for rel in files:
        src = upstream / DRIVERS[driver]["upstream"] / rel
        kind = "tu" if rel in tu_set else "header-or-build"
        rows.append(
            f"{commit}\t{DRIVERS[driver]['upstream']}/{rel}\t"
            f"{driver}/{rel}\t{sha256(src)}\t{len(src.read_bytes().splitlines())}\t"
            f"{kind}\t{spdx(src)}"
        )
    return rows


def sources_block(driver: str, tus: list[str]) -> str:
    lines = [
        f"# {driver}: the compiled set for the AC 7260 (mvm) under the feature",
        "# face include/linux80211/driver_config.h (IWLMVM on; DVM/MLD/MEI/",
        "# DEBUGFS/DEVICE_TRACING/LEDS/PM_SLEEP/ACPI/EFI off).",
        f"ifeq ($(SRC_TAG),drv)",
        f"obj-$(CONFIG_LIBLINUX80211) += \\",
    ]
    for i, tu in enumerate(tus):
        tail = " \\" if i + 1 < len(tus) else ""
        lines.append(f"\t{tu}{tail}")
    lines.append("endif")
    return "\n".join(lines) + "\n"


def provenance(upstream: Path, driver: str, files: list[str],
               tus: list[str]) -> str:
    commit = subprocess.run(
        ["git", "-C", str(upstream), "rev-parse", "HEAD"],
        text=True, capture_output=True, check=True,
    ).stdout.strip()
    lic: dict[str, int] = {}
    for rel in files:
        lic[spdx(upstream / DRIVERS[driver]["upstream"] / rel)] = \
            lic.get(spdx(upstream / DRIVERS[driver]["upstream"] / rel), 0) + 1
    lic_lines = "\n".join(f"| `{k}` | {v} |" for k, v in sorted(lic.items()))
    enabled = ", ".join(f"{k}=1" for k, v in FEATURES.items() if v)
    disabled = ", ".join(k for k, v in FEATURES.items() if not v)
    return f"""# wireless-drivers/{driver} - provenance

| Item | Value |
|---|---|
| Upstream | Linux `{DRIVERS[driver]["upstream"]}/` |
| Pin | commit `{commit}` |
| Vendoring | flat import, byte-identical: {len(files)} files. Digests in `IMPORT.tsv`; verify with `tools/build/import-driver.py --driver {driver} --check`. |
| Local differences | none (zero patch) |
| Compiled set | **{len(tus)} TUs** under the feature face below; named by `sources.mk` |
| Feature face | on: {enabled} |
| | off: {disabled} |

## License

| SPDX expression | files |
|---|---|
{lic_lines}

The driver is a GPL-2.0 module (`iwl-drv.c` states `MODULE_LICENSE("GPL")`).
Every artifact that links it is GPL-2.0; see `docs/LICENSES.md` and
`components/liblinux80211/docs` (the stack's own licence note).

## Why 72 and not 74

The previous line's `sources.mk` listed 74 entries: 72 driver translation units
plus the compiled-in firmware blob and the board case. The driver's own
Makefiles resolve to 72 under this face; the other two entries were the
consumer's, not the driver's.

## Consumers

`tests/board/rtos/build/wl80211.mk` compiles this set into the
`rk3568-iwl7260-mac80211` profile; `tools/build/s1-driver-compile` compiles it
standalone into `runs/S1/driver/` (the S1b measurement).
"""


def do_import(upstream: Path, driver: str) -> int:
    spec = DRIVERS[driver]
    src = upstream / spec["upstream"]
    dest = DRIVERS_ROOT / driver
    if not src.is_dir():
        raise SystemExit(f"no {spec['upstream']} under {upstream}")
    tus = upstream_tus(src, driver)
    files = all_files(src)
    for rel in files:
        s = src / rel
        d = dest / rel
        d.parent.mkdir(parents=True, exist_ok=True)
        if not d.exists() or sha256(d) != sha256(s):
            shutil.copyfile(s, d)
    (dest / "IMPORT.tsv").write_text(
        "\n".join(manifest_rows(upstream, driver, files, tus)) + "\n")
    (dest / "PROVENANCE.md").write_text(provenance(upstream, driver, files, tus))
    (dest / "sources.mk").write_text(sources_block(driver, tus))
    print(f"imported {driver}: {len(files)} files, {len(tus)} TUs -> {dest}")
    print(f"manifest: {dest / 'IMPORT.tsv'}")
    return 0


def do_check(upstream: Path, driver: str) -> int:
    spec = DRIVERS[driver]
    src = upstream / spec["upstream"]
    dest = DRIVERS_ROOT / driver
    tus = upstream_tus(src, driver)
    files = all_files(src)
    bad = 0
    for rel in files:
        s = src / rel
        d = dest / rel
        if not d.exists():
            print(f"MISSING {d.relative_to(REPO)}", file=sys.stderr)
            bad += 1
        elif sha256(d) != sha256(s):
            print(f"DIFFERS {d.relative_to(REPO)}", file=sys.stderr)
            bad += 1
    extra = sorted(
        str(p.relative_to(dest)) for p in dest.rglob("*")
        if p.is_file() and str(p.relative_to(dest)) not in set(files)
        and p.name not in ("IMPORT.tsv", "PROVENANCE.md", "sources.mk")
    )
    for rel in extra:
        print(f"EXTRA {driver}/{rel}", file=sys.stderr)
        bad += 1
    mk = dest / "sources.mk"
    carried = set(re.findall(r"^\s+([A-Za-z0-9_/.-]+\.c)", mk.read_text(), re.M))
    want = set(tus)
    for missing in sorted(want - carried):
        print(f"sources.mk lacks {missing}", file=sys.stderr)
        bad += 1
    for extra_tu in sorted(carried - want):
        print(f"sources.mk carries unknown {extra_tu}", file=sys.stderr)
        bad += 1
    if bad:
        print(f"check: FAIL ({bad} problem(s))", file=sys.stderr)
        return 1
    commit = subprocess.run(
        ["git", "-C", str(upstream), "rev-parse", "--short", "HEAD"],
        text=True, capture_output=True,
    ).stdout.strip()
    print(f"check: ok ({len(files)} files, {len(tus)} TUs byte-identical to {commit})")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--driver", required=True, choices=sorted(DRIVERS))
    ap.add_argument("--upstream", type=Path,
                    default=REPO.parent / "linux-upstream")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--emit-sources-block", action="store_true")
    args = ap.parse_args()
    upstream = args.upstream.resolve()
    if args.emit_sources_block:
        tus = upstream_tus(upstream / DRIVERS[args.driver]["upstream"], args.driver)
        sys.stdout.write(sources_block(args.driver, tus))
        return 0
    if args.check:
        return do_check(upstream, args.driver)
    return do_import(upstream, args.driver)


if __name__ == "__main__":
    raise SystemExit(main())
