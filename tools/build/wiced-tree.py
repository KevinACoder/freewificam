#!/usr/bin/env python3
"""Materialize the pinned Cypress WICED tree the AP6181 driver port compiles against.

The WWD driver core is Cypress source under the per-file WICED license (use with
Cypress ICs only, no redistribution).  It is therefore *not* tracked here: this
tool copies the exact subset the port needs from a vendor tree outside the
repository into ``wireless-drivers/ap6181/vendor/`` (git-ignored) and records a
sha256 manifest, so a working tree is reproducible while the public repository
keeps no Cypress byte.

    tools/build/wiced-tree.py                 copy from $WICED_ROOT (or the default)
    tools/build/wiced-tree.py --check         verify pin + every manifest digest
    tools/build/wiced-tree.py --root <dir>    another vendor tree
    tools/build/wiced-tree.py --print-files   the file list the pin resolves to

The pin is the WICED version string in ``WiFi_SDK/version.txt`` and the WICED
commit id it carries.  A tree that does not match is refused: the port records
one vendor tree, not "whatever was on disk".
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
COMPONENT = REPO / "wireless-drivers" / "ap6181"
DEST = COMPONENT / "vendor"
DEFAULT_ROOT = Path.home() / "workspace" / "stm32_lab" / "os" / "wifi-ap6181-f429-code"

PIN_VERSION = "Wiced_006.002.001.0002"
PIN_WICED_COMMIT = "7947be1d4e41e9ee5c35b74a4c044ef9c30dfa35"

# What the port compiles or includes.  The WWD core is the driver; the platform
# files are the generic resource/NVRAM layer that WWD calls (the port supplies
# the SDIO host, RTOS, buffers, network seam and power/reset instead); the
# libraries are the ones WWD's own link table names.  Nothing else is copied.
WHITELIST = (
    "WiFi_SDK/WICED/WWD",
    "WiFi_SDK/WICED/network/LwIP/WWD/*.h",
    "WiFi_SDK/WICED/platform/include",
    "WiFi_SDK/WICED/platform/MCU/*.h",
    "WiFi_SDK/WICED/platform/MCU/wwd_resources.c",
    "WiFi_SDK/WICED/platform/MCU/platform_resource.c",
    "WiFi_SDK/WICED/platform/MCU/STM32F4xx/*.h",
    "WiFi_SDK/WICED/platform/MCU/STM32F4xx/peripherals/*.h",
    "WiFi_SDK/WICED/platform/MCU/STM32F4xx/peripherals/platform_gpio.c",
    "WiFi_SDK/WICED/platform/MCU/STM32F4xx/peripherals/platform_mcu_powersave.c",
    "wifi_lwip_AP/User/platform_toolchain.h",
    "wifi_lwip_AP/User/stm32f4xx_conf.h",
    "wifi_lwip_AP/User/wifi_base_config.h",
    "wifi_lwip_AP/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_gpio.c",
    "wifi_lwip_AP/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_rcc.c",
    "wifi_lwip_AP/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_syscfg.c",
    "wifi_lwip_AP/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_exti.c",
    "wifi_lwip_AP/Libraries/STM32F4xx_StdPeriph_Driver/src/misc.c",
    "wifi_lwip_AP/Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c",
    "WiFi_SDK/WICED/internal",
    "WiFi_SDK/include",
    "WiFi_SDK/libraries/utilities/TLV",
    "WiFi_SDK/libraries/utilities/ring_buffer",
    "WiFi_SDK/libraries/utilities/linked_list",
    "WiFi_SDK/WICED/security/BESL/include/besl_structures.h",
    "WiFi_SDK/libraries/utilities/crc",
    "WiFi_SDK/WICED/RTOS/FreeRTOS/WICED/*.h",
    "WiFi_SDK/WICED/RTOS/FreeRTOS/WWD/*.h",
    "WiFi_SDK/WICED/RTOS/FreeRTOS/WWD/ARM_CM3/*.h",
    # The vendor's own SDIO host and the two layers it stands on: the
    # StdPeriph SDIO driver and the CMSIS device headers.  The port compiled
    # its own host for a while; the vendor file is the one that is known to
    # work with this chip, so it is the one that ships.
    "WiFi_SDK/WICED/platform/MCU/STM32F4xx/WWD/wwd_SDIO.c",
    "wifi_lwip_AP/Libraries/STM32F4xx_StdPeriph_Driver/inc/*.h",
    "wifi_lwip_AP/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_sdio.c",
    "wifi_lwip_AP/Libraries/CMSIS/Device/ST/STM32F4xx/Include/stm32f4xx.h",
    "wifi_lwip_AP/Libraries/CMSIS/Device/ST/STM32F4xx/Include/system_stm32f4xx.h",
    "wifi_lwip_AP/Libraries/CMSIS/Include/core_cm4.h",
    "wifi_lwip_AP/Libraries/CMSIS/Include/core_cmFunc.h",
    "wifi_lwip_AP/Libraries/CMSIS/Include/core_cmInstr.h",
    "wifi_lwip_AP/Libraries/CMSIS/Include/core_cmSimd.h",
    "WiFi_SDK/resources/firmware/43362",
    "wifi_lwip_AP/User/43362A2_bin.c",
    "WiFi_SDK/generated_mac_address.txt",
    "WiFi_SDK/version.txt",
    "WiFi_SDK/LICENSE.txt",
)

NOTICE = """\
This directory is a materialized copy of Cypress WICED sources, not part of
this repository (git-ignored).  The sources are under the WICED license in
WiFi_SDK/LICENSE.txt: use with Cypress integrated circuits only, no
redistribution.  See ../../PROVENANCE.md for the pin and the manifest.
"""


def die(msg: str) -> "NoReturn":  # noqa: F821
    print(f"wiced-tree: {msg}", file=sys.stderr)
    raise SystemExit(2)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def version_lines(root: Path) -> tuple[str, str]:
    path = root / "WiFi_SDK" / "version.txt"
    if not path.is_file():
        die(f"{path} is missing (is --root a wifi-ap6181-f429-code tree?)")
    lines = [ln.strip() for ln in path.read_text(errors="replace").splitlines() if ln.strip()]
    version = lines[0].split(":", 1)[1].strip() if ":" in lines[0] else lines[0]
    commit = lines[1] if len(lines) > 1 else ""
    return version, commit


def tree_head(root: Path) -> str:
    try:
        out = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            check=True, capture_output=True, text=True,
        )
        return out.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def check_pin(root: Path) -> tuple[str, str, str]:
    version, commit = version_lines(root)
    if version != PIN_VERSION:
        die(f"vendor tree version is {version!r}, this port pins {PIN_VERSION!r}")
    if commit != PIN_WICED_COMMIT:
        die(f"vendor tree carries WICED commit {commit!r}, this port pins {PIN_WICED_COMMIT!r}")
    return version, commit, tree_head(root)


def expand(root: Path) -> list[Path]:
    files: set[Path] = set()
    for entry in WHITELIST:
        path = root / entry
        if path.is_dir():
            files |= {f for f in path.rglob("*") if f.is_file()}
        elif "*" in entry:
            files |= {f for f in root.glob(entry) if f.is_file()}
        elif path.is_file():
            files.add(path)
        else:
            die(f"whitelist entry not found in the vendor tree: {entry}")
    return sorted(files)


def rel(root: Path, path: Path) -> str:
    return path.relative_to(root).as_posix()


def materialize(root: Path) -> int:
    version, commit, head = check_pin(root)
    files = expand(root)
    if DEST.exists():
        shutil.rmtree(DEST)
    DEST.mkdir(parents=True)
    rows = []
    total = 0
    for path in files:
        target = DEST / rel(root, path)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        digest = sha256(target)
        rows.append(f"{digest}  {rel(root, path)}")
        total += target.stat().st_size
    (DEST / "MANIFEST.sha256").write_text("\n".join(rows) + "\n")
    (DEST / "NOTICE.txt").write_text(NOTICE)
    pin = [
        "# Materialized WICED tree: the pin and the count, re-checked by --check.",
        f"source_root={root}",
        f"wiced_version={version}",
        f"wiced_commit={commit}",
        f"tree_head={head or '(not a git tree)'}",
        f"files={len(rows)}",
        f"bytes={total}",
    ]
    (DEST / "PIN.txt").write_text("\n".join(pin) + "\n")
    print(f"wiced-tree: materialized {len(rows)} files, {total} bytes -> {rel(REPO, DEST)}")
    print(f"wiced-tree: pin {version} / {commit[:12]} / tree {head[:12] or 'n/a'}")
    print("wiced-tree: Cypress license - use with Cypress ICs only, do not redistribute")
    return 0


def check(root: Path) -> int:
    version, commit, head = check_pin(root)
    manifest = DEST / "MANIFEST.sha256"
    if not manifest.is_file():
        die(f"{rel(REPO, manifest)} is missing; run tools/build/wiced-tree.py first")
    bad = 0
    rows = 0
    for line in manifest.read_text().splitlines():
        if not line.strip():
            continue
        digest, name = line.split("  ", 1)
        rows += 1
        target = DEST / name
        if not target.is_file():
            print(f"wiced-tree: missing {name}", file=sys.stderr)
            bad += 1
            continue
        got = sha256(target)
        if got != digest:
            print(f"wiced-tree: drift {name} (want {digest[:12]}, have {got[:12]})", file=sys.stderr)
            bad += 1
    pin = (DEST / "PIN.txt").read_text() if (DEST / "PIN.txt").is_file() else ""
    want = {
        "wiced_version": version,
        "wiced_commit": commit,
        "tree_head": head or "(not a git tree)",
    }
    for key, value in want.items():
        line = next((ln for ln in pin.splitlines() if ln.startswith(key + "=")), "")
        if line and line != f"{key}={value}":
            print(f"wiced-tree: pin drift {key}: manifest has {line!r}, tree has {value!r}",
                  file=sys.stderr)
            bad += 1
    expected = expand(root)
    have = {p.relative_to(DEST).as_posix() for p in DEST.rglob("*") if p.is_file()} - {
        "MANIFEST.sha256", "NOTICE.txt", "PIN.txt",
    }
    want_set = {rel(root, p) for p in expected}
    for name in sorted(want_set - have):
        print(f"wiced-tree: not materialized: {name}", file=sys.stderr)
        bad += 1
    for name in sorted(have - want_set):
        print(f"wiced-tree: extra file not in the whitelist: {name}", file=sys.stderr)
        bad += 1
    if bad:
        print(f"wiced-tree: FAIL ({bad} problem(s))", file=sys.stderr)
        return 1
    print(f"wiced-tree: ok ({rows} files, pin {version}, tree {head[:12] or 'n/a'})")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path,
                        default=Path(os.environ.get("WICED_ROOT", DEFAULT_ROOT)))
    parser.add_argument("--check", action="store_true",
                        help="verify the pin and every materialized file")
    parser.add_argument("--print-files", action="store_true",
                        help="print the resolved file list and stop")
    args = parser.parse_args()
    root = args.root.expanduser().resolve()
    if not root.is_dir():
        die(f"vendor root not found: {root} (set WICED_ROOT or pass --root)")
    if args.print_files:
        for path in expand(root):
            print(rel(root, path))
        return 0
    if args.check:
        return check(root)
    return materialize(root)


if __name__ == "__main__":
    sys.exit(main())