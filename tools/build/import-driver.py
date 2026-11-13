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

# Each driver carries its own feature face under `features`: the answer its
# `driver_config.h` states for the preprocessor, spelled again here so the
# compiled set and the header cannot drift (each --check re-derives the set).
# A symbol absent from the map is off; the faces list the offs explicitly
# because the generated PROVENANCE.md prints them as the face statement.
IWLWIFI_FEATURES = {
    "IWLWIFI": True,
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

# The RTL8851BU world: the driver's own Makefile under the same four symbols
# the GT build selects, and every optional unit off (debug/LED/PM/other chips).
# This is the face the previous line's board-proven image compiled.
RTW89_FEATURES = {
    "RTW89_CORE": True,
    "RTW89_USB": True,
    "RTW89_8851B": True,
    "RTW89_8851BU": True,
    "RTW89_PCI": False,
    "RTW89_8851BE": False,
    "RTW89_8852A": False,
    "RTW89_8852AE": False,
    "RTW89_8852AU": False,
    "RTW89_8852B_COMMON": False,
    "RTW89_8852B": False,
    "RTW89_8852BE": False,
    "RTW89_8852BU": False,
    "RTW89_8852BT": False,
    "RTW89_8852BTE": False,
    "RTW89_8852C": False,
    "RTW89_8852CE": False,
    "RTW89_8852CU": False,
    "RTW89_8922A": False,
    "RTW89_8922AE": False,
    "RTW89_8922AU": False,
    "RTW89_8922D": False,
    "RTW89_8922DE": False,
    "RTW89_DEBUG": False,
    "RTW89_DEBUGMSG": False,
    "RTW89_DEBUGFS": False,
    "RTW89_LEDS": False,
    "RTW89_LEDS_MC": False,
    "PM": False,
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
        "features": IWLWIFI_FEATURES,
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
    "rtw89": {
        "kind": "kbuild",
        "upstream": "drivers/net/wireless/realtek/rtw89",
        "dest": "",
        "features": RTW89_FEATURES,
        "makefiles": ("Makefile",),
        "want": 24,
        "tu_prefix": "",
        "first_party": ("IMPORT.tsv", "PROVENANCE.md", "sources.mk",
                        "driver_config.h"),
        "sources_header": [
            "# rtw89: the compiled set for the RTL8851BU (USB) under the feature",
            "# face wireless-drivers/rtw89/driver_config.h (CORE/USB/8851B/8851BU",
            "# on; PCI, every other chip, DEBUG/DEBUGMSG/DEBUGFS/LEDS/LEDS_MC/PM",
            "# off).",
        ],
        "sources_symbol": "CONFIG_LIBLINUX80211_RTW89",
        "provenance": {
            "licence_note":
                "The tree is `GPL-2.0 OR BSD-3-Clause` in every file. The "
                "previous line took\nthe BSD option for its net80211 profile; "
                "this line links the driver with the\nGPL-2.0-only "
                "mac80211/cfg80211 stack, so every artifact that carries it is a\n"
                "GPL-2.0 link product (see `docs/LICENSES.md`). The driver bytes "
                "are\nunmodified, which is why the licence table above is the "
                "tree's own.",
            "count_note":
                "## Why 24\n\nThe driver's own Makefile resolves to 24 translation "
                "units under this face:\n18 core units (its `rtw89_core-y` names "
                "the three `*_be` units\nunconditionally), the four 8851B chip "
                "units, the USB bus unit and the\n8851BU device unit. The Wi-Fi 7 "
                "`*_be` units are compiled because the\nMakefile lists them; the "
                "chip selection never reaches them, and the image's\nzero-undefined "
                "gate is the check. The GT build compiles more: `debug.c`,\n"
                "`led.c`, `led_mc.c` and `wow.c` ride CONFIG_RTW89_DEBUG/LEDS/PM, "
                "which this\nface keeps out (logging, LED and suspend units, not "
                "the data path).\n",
            "consumers":
                "`tests/board/rtos/build/wl80211.mk` compiles this set in the "
                "driver's\nflag world (`driver_config.h` is force-included); "
                "`tools/build/s1-driver-compile`\ncompiles it standalone with "
                "`S1_DRIVER=rtw89` as the S1b measurement.\n",
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


def _config_enabled(driver: str, symbol: str) -> bool:
    symbol = symbol.removeprefix("CONFIG_")
    return DRIVERS[driver]["features"].get(symbol, False)


def _aggregates(text: str) -> set[str]:
    """The module aggregate stems a Makefile's `-objs`/`-y` lists define.

    `obj-$(CONFIG_RTW89_CORE) += rtw89_core.o` names the link aggregate, not a
    translation unit; the aggregate exists because `rtw89_core-y` is assigned.
    """
    out: set[str] = set()
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        m = re.match(r"^([\w-]+)-(?:objs|y)\s*(?::=|\+=|\?=|=)", line)
        if m:
            out.add(m.group(1))
    return out


def _logical_lines(text: str) -> list[str]:
    """A Makefile's lines with `\\` continuations joined (one assignment each).

    A driver that groups its objects as one `rtw89_core-y += a.o b.o ...`
    assignment over several physical lines only names its units on the first
    line, so matching physical lines would derive one TU per group.
    """
    out: list[str] = []
    pending = ""
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].rstrip()
        if line.endswith("\\"):
            pending += line[:-1] + " "
            continue
        out.append(pending + line)
        pending = ""
    if pending:
        out.append(pending)
    return out


def _objects(text: str, subdir: str, driver: str) -> list[str]:
    """The .o names a Makefile enables under the driver's face, in file order.

    Kbuild names units two ways, and a driver may use both in one Makefile:
    an aggregate (`obj-$(CONFIG_RTW89_CORE) += rtw89_core.o` plus
    `rtw89_core-y := core.o ...`), or the unit directly
    (`obj-$(CONFIG_FOO) += foo.o`).  Only the aggregate's list is gated by the
    `obj-*` line that names it - writing every `-y` list in the file would
    compile every chip in a multi-chip driver.
    """
    lines = _logical_lines(text)

    # Pass 1: the object lists, keyed by the aggregate stem that owns them,
    # including the CONFIG-gated additions (`rtw89_core-$(CONFIG_RTW89_DEBUG)+=`).
    lists: dict[str, list[str]] = {}
    for line in lines:
        line = line.split("#", 1)[0].strip()
        m = re.match(r"^([\w-]+)-(?:objs|y)\s*(?::=|\+=|\?=|=)\s*(.+)$", line)
        if m:
            lists.setdefault(m.group(1), []).extend(
                re.findall(r"([A-Za-z0-9_/-]+)\.o", m.group(2)))
            continue
        m = re.match(r"^([\w-]+)-\$\(CONFIG_([A-Z0-9_]+)\)\s*\+=\s*(.+)$",
                     line)
        if m and _config_enabled(driver, m.group(2)):
            lists.setdefault(m.group(1), []).extend(
                re.findall(r"([A-Za-z0-9_/-]+)\.o", m.group(3)))

    # Pass 2: the `obj-*` lines decide which aggregates (or direct units) are
    # in the compiled set, in file order.
    out: list[str] = []
    for line in lines:
        line = line.split("#", 1)[0].strip()
        m = re.match(r"^obj-\$\(CONFIG_([A-Z0-9_]+)\)\s*\+=\s*(.+)$", line)
        if m:
            if not _config_enabled(driver, m.group(1)):
                continue
            names = re.findall(r"([A-Za-z0-9_/-]+)\.o", m.group(2))
        else:
            m = re.match(r"^obj-y\s*\+=\s*(.+)$", line)
            if not m:
                continue
            names = re.findall(r"([A-Za-z0-9_/-]+)\.o", m.group(1))
        for name in names:
            stem = name.rsplit("/", 1)[-1]
            if stem in lists:
                out += [subdir + n for n in lists[stem]]
            else:
                out.append(subdir + name)
    return out


def _kbuild_tus(src: Path, driver: str) -> list[str]:
    spec = DRIVERS[driver]
    tus: list[str] = []
    aggregates: set[str] = set()
    for rel in spec["makefiles"]:
        text = (src / rel).read_text()
        subdir = rel.rsplit("/", 1)[0] + "/" if "/" in rel else ""
        aggregates |= _aggregates(text)
        for name in _objects(text, subdir, driver):
            # A Makefile may name a directory's objects as `dvm/` (the
            # `obj-$(CONFIG_IWLDVM) += dvm/` line); those are disabled by the
            # feature face and never reach here.
            tus.append(name + ".c")
    # Drop duplicates while keeping the first position: one unit is listed by
    # two Makefiles only when a feature moved it, and the feature face decided.
    # A module aggregate (`iwlwifi.o`, `rtw89_core.o`) is a link target, not a
    # translation unit: it has no .c of the same name and is filtered here by
    # the `-objs`/`-y` list that defines it.
    module_names = {"iwlwifi", "iwlmvm", "iwldvm", "iwlmld", "mvm/iwlmvm"}
    seen: set[str] = set()
    ordered: list[str] = []
    for tu in tus:
        if tu in seen:
            continue
        if not (src / tu).is_file():
            stem = tu.removesuffix(".c")
            if stem in module_names or Path(stem).name in aggregates:
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
        feats = spec["features"]
        enabled = ", ".join(f"{k}=1" for k, v in feats.items() if v)
        disabled = ", ".join(k for k, v in feats.items() if not v)
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
