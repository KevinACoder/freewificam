#!/usr/bin/env python3
"""Import a verbatim wireless driver tree into this repository, or verify it.

The driver is carried byte-identical to the GT pin, the same discipline the
stack import uses (tools/build/import-stack.py): the whole upstream directory,
every file, no local edit.  The *compiled* set is a separate answer: it is
re-derived from the driver's own build description under the feature face this
line takes, and it is written into the driver's sources.mk for the platform
Makefile to consume.

  tools/build/import-driver.py --driver iwlwifi
  tools/build/import-driver.py --driver iwlwifi --check
  tools/build/import-driver.py --driver iwlwifi --emit-sources-block
  tools/build/import-driver.py --driver rtl8189fs
  tools/build/import-driver.py --driver rtl8189fs --check

The tree lands in wireless-drivers/<driver>/ (a vendor tree with its own
directory layout lands under src/, so the import stays one directory); IMPORT.tsv
records one row per file (source commit, source path, destination, sha256, lines,
kind, license) and PROVENANCE.md states the pin, the licence and the compiled
set.

Two derivation kinds, because the two trees answer the question differently:

  kbuild       the driver's Makefiles name `obj-y` lists and gate optional units
               on CONFIG_ symbols; the face (FEATURES) decides, and the answer
               is the 72 iwlwifi units.
  vendor-make  the vendor Makefile assigns its own CONFIG_ block and assembles a
               `$(MODULE_NAME)-y` link table from `:=` variables plus an included
               `hal/phydm/phydm.mk`.  Re-implementing that evaluation would be a
               second interpreter; the tool runs the vendor's own `make -pn`
               (the same dump the in-tree Kbuild's comment cites) and expands the
               `8189fs-y` value.
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

# The feature face the iwlwifi compiled set is derived under.  This is the same
# answer include/linux80211/driver_config.h states for the preprocessor: IWLMVM
# on, the optional features off.  If one of those changes, both spellings change
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

# One entry per driver:
#   kind        kbuild | vendor-make (how the compiled set is derived)
#   upstream    where the sources live under the kernel tree
#   dest        subdirectory under wireless-drivers/<driver>/ ("" = flat)
#   want        the TU count the derivation must produce
#   first_party files/directories at the destination that are not the import
#   sources_*   the generated sources.mk's header text
#   provenance  the driver-specific paragraphs of PROVENANCE.md
DRIVERS = {
    "iwlwifi": {
        "kind": "kbuild",
        "upstream": "drivers/net/wireless/intel/iwlwifi",
        "dest": "",
        "makefiles": ("Makefile", "mvm/Makefile"),
        "want": 72,
        "tu_prefix": "",
        "first_party": ("IMPORT.tsv", "PROVENANCE.md", "sources.mk"),
        "sources_header": [
            "# iwlwifi: the compiled set for the AC 7260 (mvm) under the feature",
            "# face include/linux80211/driver_config.h (IWLMVM on; DVM/MLD/MEI/",
            "# DEBUGFS/DEVICE_TRACING/LEDS/PM_SLEEP/ACPI/EFI off).",
        ],
        "sources_symbol": "CONFIG_LIBLINUX80211_IWLWIFI",
        "provenance": {
            "licence_note":
                "The driver is a GPL-2.0 module (`iwl-drv.c` states "
                "`MODULE_LICENSE(\"GPL\")`).\nEvery artifact that links it is "
                "GPL-2.0; see `docs/LICENSES.md` and\n"
                "`components/liblinux80211/docs` (the stack's own licence note).",
            "count_note":
                "## Why 72 and not 74\n\nThe previous line's `sources.mk` listed "
                "74 entries: 72 driver translation units\nplus the compiled-in "
                "firmware blob and the board case. The driver's own\n"
                "Makefiles resolve to 72 under this face; the other two entries "
                "were the\nconsumer's, not the driver's.\n",
            "consumers":
                "`tests/board/rtos/build/wl80211.mk` compiles this set into the\n"
                "`rk3568-iwl7260-mac80211` profile; `tools/build/s1-driver-compile` "
                "compiles it\nstandalone into `runs/S1/driver/` (the S1b measurement).\n",
        },
    },
    "rtl8189fs": {
        "kind": "vendor-make",
        "upstream": "drivers/net/wireless/realtek/rtl8189fs",
        "dest": "src",
        "link_table": "8189fs-y",
        "want": 131,
        "tu_prefix": "src/",
        "first_party": ("IMPORT.tsv", "PROVENANCE.md", "sources.mk",
                        "driver_config.h"),
        "first_party_prefixes": ("ports/",),
        "sources_header": [
            "# rtl8189fs: the vendor RTL8188F SDIO driver's own link table.",
            "#",
            "# The list is `8189fs-y` as the vendor Makefile resolves it (the",
            "# in-tree Kbuild's object table, and `make -pn` on this tree, both",
            "# give these 131 units; tools/build/import-driver.py --check",
            "# re-derives it). Paths are relative to the imported tree root src/.",
            "#",
            "# The RTOS feature face (driver_config.h) departs from the GT build",
            "# in three preprocessor answers, and none removes a unit:",
            "# CONFIG_PROC_DEBUG off (rtw_proc.c becomes the empty #ifdef arm;",
            "# this project has no filesystem), CONFIG_WIFI_MONITOR off (the STA",
            "# round's face), CONFIG_BR_EXT off (as the GT Kbuild itself does:",
            "# the bridge extension needs a bridge-module symbol). Every other",
            "# CONFIG_* the vendor Makefile sets stays as the GT build has it.",
        ],
        "sources_symbol": "CONFIG_LIBLINUX80211_RTL8189FS",
        "provenance": {
            "licence_note":
                "Most vendor files carry the long GPL header rather than an SPDX tag, "
                "so\ntheir row above reads `unknown`; the tree's `LICENSE` (GPL-2.0) "
                "governs.\n\nThe tree is GPL-2.0. The embedded "
                "firmware\narray (`FW_NIC`, 20,306 B) is part of the driver source "
                "and travels under the\nsame terms; it is not a separately "
                "extracted blob. Every artifact that links\nit is GPL-2.0; see "
                "`docs/LICENSES.md`.",
            "count_note":
                "## 131 units, and why the face keeps all of them\n\nThe vendor "
                "Makefile assembles its link table from `:=` lists plus\n"
                "`hal/phydm/phydm.mk`; `make -pn` on this tree resolves "
                "`8189fs-y` to 132\nobjects with one duplicate (`sdio_ops_linux.o` "
                "is named twice), i.e. 131 units.\nThe RTOS face changes two "
                "preprocessor answers (`CONFIG_PROC_DEBUG` off,\n"
                "`CONFIG_WIFI_MONITOR` off) and thereby no unit: `rtw_proc.c` is "
                "one big\n`#ifdef CONFIG_PROC_DEBUG` and compiles empty.\n",
            "consumers":
                "`tests/board/rtos/build/wl80211.mk` compiles this set in the "
                "driver's\nflag world (the `driver_config.h` face is force-included); "
                "`tools/build/\ns1-driver-compile` compiles it standalone as the "
                "S1b measurement for this line.\n",
        },
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


def dest_dir(driver: str) -> Path:
    return DRIVERS_ROOT / driver


def dest_rel(driver: str, rel: str) -> str:
    dest = DRIVERS[driver]["dest"]
    return f"{dest}/{rel}" if dest else rel


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


def _kbuild_tus(src: Path, driver: str) -> list[str]:
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
    return ordered


def _make_database(src: Path) -> str:
    """The vendor Makefile's own variable database (`make -pn`, no target)."""
    result = subprocess.run(
        ["make", "-pn", "KERNELRELEASE=dumptest", "src=."],
        cwd=src, text=True, capture_output=True,
    )
    # `-p` with no target prints the database and then exits 2 ("No targets");
    # the database is the product and the exit status is the missing target.
    if result.returncode != 0 and "No targets" not in result.stderr:
        raise SystemExit(
            f"vendor Makefile database failed ({result.returncode}): "
            f"{result.stderr.strip().splitlines()[-1] if result.stderr.strip() else ''}"
        )
    return result.stdout


_ASSIGN = re.compile(r"^([A-Za-z0-9_.-]+)\s*(?:::|:|\+=|\?=|=)\s*(.*)$")
_VAR = re.compile(r"\$\(([A-Za-z0-9_]+)\)")


def _database_vars(db: str) -> dict[str, str]:
    """Variable name -> last value the database printed for it."""
    out: dict[str, str] = {}
    for line in db.splitlines():
        if line.startswith("#") or line.startswith("\t"):
            continue
        m = _ASSIGN.match(line)
        if m:
            out[m.group(1)] = m.group(2).strip()
    return out


def _expand(value: str, variables: dict[str, str], depth: int = 0) -> str:
    if depth > 8:
        return ""
    return _VAR.sub(
        lambda mo: _expand(variables.get(mo.group(1), ""), variables, depth + 1),
        value,
    )


def _vendor_tus(src: Path, driver: str) -> list[str]:
    spec = DRIVERS[driver]
    variables = _database_vars(_make_database(src))
    expr = variables.get(spec["link_table"], "")
    if not expr:
        raise SystemExit(f"{driver}: the vendor Makefile names no {spec['link_table']}")
    objects = re.findall(r"([A-Za-z0-9_/-]+)\.o", _expand(expr, variables))
    ordered = list(dict.fromkeys(objects))
    for tu in ordered:
        if not (src / f"{tu}.c").is_file():
            raise SystemExit(f"{driver}: link table names a missing unit {tu}.c")
    return [tu + ".c" for tu in ordered]


def upstream_tus(src: Path, driver: str) -> list[str]:
    spec = DRIVERS[driver]
    if spec["kind"] == "kbuild":
        ordered = _kbuild_tus(src, driver)
    else:
        ordered = _vendor_tus(src, driver)
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
            f"{driver}/{dest_rel(driver, rel)}\t{sha256(src)}\t"
            f"{len(src.read_bytes().splitlines())}\t"
            f"{kind}\t{spdx(src)}"
        )
    return rows


def sources_block(driver: str, tus: list[str]) -> str:
    spec = DRIVERS[driver]
    lines = list(spec["sources_header"]) + [
        "ifeq ($(SRC_TAG),drv)",
        f"obj-$({spec['sources_symbol']}) += \\",
    ]
    for i, tu in enumerate(tus):
        tail = " \\" if i + 1 < len(tus) else ""
        lines.append(f"\t{dest_rel(driver, tu)}{tail}")
    lines.append("endif")
    return "\n".join(lines) + "\n"


def provenance(upstream: Path, driver: str, files: list[str],
               tus: list[str]) -> str:
    spec = DRIVERS[driver]
    commit = subprocess.run(
        ["git", "-C", str(upstream), "rev-parse", "HEAD"],
        text=True, capture_output=True, check=True,
    ).stdout.strip()
    lic: dict[str, int] = {}
    for rel in files:
        key = spdx(upstream / spec["upstream"] / rel)
        lic[key] = lic.get(key, 0) + 1
    lic_lines = "\n".join(f"| `{k}` | {v} |" for k, v in sorted(lic.items()))
    if spec["kind"] == "kbuild":
        enabled = ", ".join(f"{k}=1" for k, v in FEATURES.items() if v)
        disabled = ", ".join(k for k, v in FEATURES.items() if not v)
        face = f"| Feature face | on: {enabled} |\n| | off: {disabled} |"
        vendoring = (f"flat import, byte-identical: {len(files)} files. "
                     "Digests in `IMPORT.tsv`; verify with "
                     f"`tools/build/import-driver.py --driver {driver} --check`.")
    else:
        face = ("| Feature face | the vendor Makefile's own CONFIG_ block (the GT "
                "build's), with `CONFIG_PROC_DEBUG`,\n| | `CONFIG_WIFI_MONITOR` and "
                "`CONFIG_BR_EXT` off; stated in `driver_config.h` |")
        vendoring = (f"import under `src/`, byte-identical: {len(files)} files. "
                     "Digests in `IMPORT.tsv`; verify with "
                     f"`tools/build/import-driver.py --driver {driver} --check`.")
    note = spec["provenance"]
    return f"""# wireless-drivers/{driver} - provenance

| Item | Value |
|---|---|
| Upstream | Linux `{spec['upstream']}/` |
| Pin | commit `{commit}` |
| Vendoring | {vendoring} |
| Local differences | none (zero patch) |
| Compiled set | **{len(tus)} TUs** under the feature face below; named by `sources.mk` |
{face}

## License

| SPDX expression | files |
|---|---|
{lic_lines}

{note['licence_note']}

{note['count_note']}
## Consumers

{note['consumers']}"""


def do_import(upstream: Path, driver: str) -> int:
    spec = DRIVERS[driver]
    src = upstream / spec["upstream"]
    dest = dest_dir(driver)
    if not src.is_dir():
        raise SystemExit(f"no {spec['upstream']} under {upstream}")
    tus = upstream_tus(src, driver)
    files = all_files(src)
    for rel in files:
        s = src / rel
        d = dest / dest_rel(driver, rel)
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


def _first_party(driver: str) -> tuple[set[str], tuple[str, ...]]:
    spec = DRIVERS[driver]
    return set(spec["first_party"]), tuple(spec.get("first_party_prefixes", ()))


def do_check(upstream: Path, driver: str) -> int:
    spec = DRIVERS[driver]
    src = upstream / spec["upstream"]
    dest = dest_dir(driver)
    tus = upstream_tus(src, driver)
    files = all_files(src)
    first_party, first_party_prefixes = _first_party(driver)
    bad = 0
    for rel in files:
        s = src / rel
        d = dest / dest_rel(driver, rel)
        if not d.exists():
            print(f"MISSING {d.relative_to(REPO)}", file=sys.stderr)
            bad += 1
        elif sha256(d) != sha256(s):
            print(f"DIFFERS {d.relative_to(REPO)}", file=sys.stderr)
            bad += 1
    imported = set(files)
    extra = sorted(
        str(p.relative_to(dest)) for p in dest.rglob("*")
        if p.is_file()
        and str(p.relative_to(dest)).removeprefix(spec["dest"] + "/" if spec["dest"] else "") not in imported
        and p.name not in first_party
        and not str(p.relative_to(dest)).startswith(first_party_prefixes)
    )
    for rel in extra:
        print(f"EXTRA {driver}/{rel}", file=sys.stderr)
        bad += 1
    mk = dest / "sources.mk"
    carried = set(re.findall(r"^\s+([A-Za-z0-9_/.-]+\.c)", mk.read_text(), re.M))
    carried = {c for c in carried if c.startswith(spec["tu_prefix"])}
    want = {dest_rel(driver, tu) for tu in tus}
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
