#!/usr/bin/env python3
"""Convert the 81 legacy configs/*.mk profiles to explicit defconfigs.

This is a one-shot migration helper, kept in the tree as the record of how
the conversion was made (and so a future profile can be converted the same
way).  It reads each profile through ``tools/build/dump-profile.mk`` - which
includes the legacy fragment exactly as the old Makefile did, including its
``include`` chains and ``+=`` appends - and writes ``configs/<name>.defconfig``.

Rules (docs/BUILD-DESIGN.md):

  * A defconfig carries only keys the old profile really set.  An unset key
    stays unset so the generator's explicit-set rule keeps the fragment's
    ``?=`` default live exactly as before.
  * The four moving values that the old Makefile injected as make variables
    (the 34 direct keys) are written as their final make value.  The 17
    lever-only names, which only ever existed as ``-DCONFIG_*`` tokens inside
    CONFIG_LEVERS, move to their Kconfig symbols and are stripped from the
    residual LEVERS.
  * CONFIG_GMAC_PROF is resolved to its effective value: the profile's own
    make variable is overridden by a later ``-DCONFIG_GMAC_PROF=`` token in
    CONFIG_LEVERS (net-netio-raw-tx256-prof sets 0 then 1).  The duplicate is
    the one allowed textual difference in A1.
  * Header comments from the old .mk are preserved above the config lines.

Usage:
    tools/build/convert-profiles.py            # write all defconfigs
    tools/build/convert-profiles.py --check    # compare, do not write
    tools/build/convert-profiles.py --profile X

The old *.mk files are NOT deleted here; the retirement is a separate,
reviewable step after A1/A3 pass.
"""

from __future__ import annotations

import argparse
import difflib
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CONFIGS = REPO / "tests" / "board" / "rtos" / "configs"
KCONFIG = REPO / "Kconfig"
DUMP_MK = REPO / "tools" / "build" / "dump-profile.mk"

# The five choices and the value their selected member maps to.
CHOICES = {
    "BACKEND": {"freertos": "BACKEND_FREERTOS", "threadx": "BACKEND_THREADX"},
    "BOARD": {
        "rk3568-itx": "BOARD_RK3568_ITX",
        "firefly-v1": "BOARD_FIREFLY_V1",
        "firefly-v2": "BOARD_FIREFLY_V2",
    },
    "OPT": {"-O0": "OPT_O0", "-O2": "OPT_O2", "-Og": "OPT_OG"},
    "PROFILE": {"base": "PROFILE_BASE", "perf": "PROFILE_PERF"},
    "RTL8XXXU_CHIP": {
        "8188eu": "RTL8XXXU_CHIP_8188EU",
        "8723bu": "RTL8XXXU_CHIP_8723BU",
    },
}

# The direct keys and how they render.  "bool" accepts 1/0/empty; "int"
# accepts the legacy spelling (16M, 16777216u) and stores the integer.
DIRECT = {
    "CASES": "string",
    "HEAP_SIZE": "heap_mb",
    "FREERTOS_HEAP_SIZE": "heap_bytes",
    "LEVERS": "residual",
    "RTL8XXXU_AP_SSID": "string",
    "RTL8XXXU_AP_FREQ": "int",
    "RTL8XXXU_EXPECT_MAC": "string",
    "RTL8XXXU_HB": "bool",
    "RTL8XXXU_USB_BUS": "int",
    "RTW89_USB_BUS": "int",
    "USB_MSC_DIAG": "bool",
    "USB_MSC_DIAG_FULL": "bool",
    "UVC_USB_BUS": "int",
    "WLAN_OBS": "bool",
    "WLAN_PING_COUNT": "int",
    "WLAN_PING_INTERVAL_MS": "int",
    "WLAN_PING_TRACE": "bool",
    "WLAN_PUMP_DRAIN": "bool",
    "WLAN_PUMP_PRIO": "string",
    "WLAN_RX_BA": "bool",
    "WLAN_SCAN_BATCH": "int",
    "WLAN_SCAN_DIAG": "bool",
    "WLAN_TCP_SINK": "bool",
    "WLAN_TCP_SINK_SECONDS": "int",
    "WLAN_UDP_PORT": "int",
    "WLAN_UDP_SINK": "bool",
    "WLAN_UDP_SINK_SECONDS": "int",
}

# The 17 lever-only names: -DCONFIG_<name>=<value> inside CONFIG_LEVERS.
CFG_ONLY = {
    "GMAC_RX_BATCH": "bool",
    "GMAC_RX_BH": "bool",
    "GMAC_RX_HWCSUM": "bool",
    "GMAC_RX_ZC": "bool",
    "GMAC_STALL_PROBE": "bool",
    "GMAC_SWEEP_WIDE": "bool",
    "GMAC_TCP_PROBE": "bool",
    "GMAC_TX_CSUM": "bool",
    "GMAC_TX_RING": "int",
    "GMAC_TX_SWEEP_GUARD": "bool",
    "GMAC_TX_TAIL_IDLE": "bool",
    "LKPI_USB_OBS": "bool",
    "NETIO_CPU_KHZ": "int",
    "NETIO_RAW_UDP": "bool",
    "NETIO_RXPATH": "bool",
    "NETIO_TCP_PROBE": "bool",
    "NETIO_TX_STALL_PROBE": "bool",
}

TOKEN = re.compile(r"^-DCONFIG_([A-Z0-9_]+)=(.*)$")


def run_dump(profile: str) -> dict[str, str]:
    proc = subprocess.run(
        ["make", "-s", "--no-print-directory", "-f", str(DUMP_MK),
         f"PROFILE={profile}", "dump"],
        cwd=REPO, capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"convert: dump failed for {profile}:\n{proc.stderr}")
    out: dict[str, str] = {}
    for line in proc.stdout.splitlines():
        if line.startswith("__KEY "):
            key, _, value = line[6:].partition("=")
            out[key] = value
    missing = [k for k in ("BACKEND", "OPT", "PROFILE") if k not in out]
    if missing:
        raise SystemExit(f"convert: {profile}: missing {missing}")
    return out


def parse_bool(raw: str) -> bool | None:
    if raw == "":
        return None
    if raw in ("1", "y", "Y", "true"):
        return True
    if raw in ("0", "n", "N", "false"):
        return False
    raise SystemExit(f"convert: not a bool: {raw!r}")


def parse_heap_mb(raw: str) -> int:
    m = re.fullmatch(r"(\d+)M", raw.strip())
    if not m:
        raise SystemExit(f"convert: bad HEAP_SIZE: {raw!r}")
    return int(m.group(1))


def parse_heap_bytes(raw: str) -> int:
    m = re.fullmatch(r"(\d+)u?", raw.strip())
    if not m:
        raise SystemExit(f"convert: bad FREERTOS_HEAP_SIZE: {raw!r}")
    return int(m.group(1))


def old_header(profile: str) -> list[str]:
    """Keep the leading comment block of the old .mk (the rationale)."""
    text = (CONFIGS / f"{profile}.mk").read_text().splitlines()
    lines: list[str] = []
    for line in text:
        if line.startswith("#"):
            lines.append(line)
        elif line.strip() == "" and lines:
            lines.append("")
        else:
            break
    while lines and lines[-1] == "":
        lines.pop()
    return lines


def convert(profile: str, kconf, kconfiglib) -> str:
    raw = run_dump(profile)
    # Start from the defaults, then set what the old profile set.  A fresh
    # Kconfig object per profile is not needed: load_config(replace=True) in
    # the caller clears the user values, and set_value() below only touches
    # the keys this profile sets.
    setv = {}

    # Structural choices that were always present.
    for key, rawkey in (("BACKEND", "BACKEND"), ("OPT", "OPT"),
                        ("PROFILE", "PROFILE")):
        if raw.get(rawkey, "") != "":
            setv[CHOICES[key][raw[rawkey]]] = "y"
    if raw.get("BOARD", "") != "":
        setv[CHOICES["BOARD"][raw["BOARD"]]] = "y"
    if raw.get("RTL8XXXU_CHIP", "") != "":
        setv[CHOICES["RTL8XXXU_CHIP"][raw["RTL8XXXU_CHIP"]]] = "y"

    # CONFIG_GMAC_PROF: the profile's own value can be overridden by a later
    # -D in CONFIG_LEVERS; the last occurrence is the compiler's answer.
    gmac_prof = raw.get("GMAC_PROF", "")
    if gmac_prof == "":
        gmac_prof = "1"  # Makefile default (CONFIG_GMAC_PROF ?= 1)
    residual: list[str] = []
    channel_b_token: dict[str, str] = {}
    for token in raw.get("LEVERS", "").split():
        m = TOKEN.match(token)
        if not m:
            residual.append(token)
            continue
        name, value = m.group(1), m.group(2)
        if name == "GMAC_PROF":
            gmac_prof = value  # last occurrence wins
            continue
        if name in CFG_ONLY:
            if CFG_ONLY[name] == "bool":
                setv[name] = "y" if value == "1" else "n"
            else:
                setv[name] = value
            continue
        # A direct symbol that the old profile set both ways: the make
        # variable is the source of truth, the token is the same value.  Drop
        # the token here; the defconfig sets the symbol once.
        if name in ("WLAN_RX_BA", "NET_WATCH"):
            channel_b_token[name] = value
            continue
        if name in DIRECT:
            continue
        # Not modeled (CONFIG_NETIO_CPU_KHZ_LATE, a lab patch, ...): keep it.
        residual.append(token)

    if gmac_prof in ("0", "1"):
        setv["GMAC_PROF"] = "y" if gmac_prof == "1" else "n"

    for name, kind in DIRECT.items():
        value = raw.get(name, "")
        if value == "":
            continue
        if kind == "string":
            setv[name] = value
        elif kind == "int":
            setv[name] = str(int(value))
        elif kind == "bool":
            b = parse_bool(value)
            if b is not None:
                setv[name] = "y" if b else "n"
        elif kind == "heap_mb":
            setv[name] = str(parse_heap_mb(value))
        elif kind == "heap_bytes":
            n = parse_heap_bytes(value)
            if n > 0:
                setv[name] = str(n)
        elif kind == "residual":
            continue  # handled below
        else:
            raise SystemExit(f"convert: bad kind {kind}")

    # WLAN_RX_BA / NET_WATCH are direct make variables too; the DIRECT map
    # deliberately does not carry them (they are channel B and are set once
    # above).  NET_WATCH's bare -DNET_WATCH stays in the residual LEVERS.
    for name in ("WLAN_RX_BA", "NET_WATCH"):
        b = parse_bool(raw.get(name, ""))
        if b is None and name in channel_b_token:
            b = parse_bool(channel_b_token[name])
        if b is not None:
            setv[name] = "y" if b else "n"

    if residual:
        setv["LEVERS"] = " ".join(residual)

    # Apply and render in Kconfig definition order.
    for name, value in setv.items():
        sym = kconf.syms.get(name)
        if sym is None:
            raise SystemExit(f"convert: {profile}: no Kconfig symbol {name}")
        sym.set_value(value)

    lines: list[str] = []
    lines.append(f"# Converted from configs/{profile}.mk on 2026-11-02 (BUILD-01 M1).")
    lines.append("# Edit this file or run ./make.rtos menuconfig, then regenerate.")
    header = old_header(profile)
    if header:
        lines.append("")
        lines.extend(header)
    lines.append("")
    for sym in kconf.unique_defined_syms:
        if sym.user_value is None:
            continue
        text = sym.config_string
        if text:
            lines.append(text.rstrip("\n"))
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--profile", action="append", default=[])
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    sys.path.insert(0, str(REPO / "tools" / "kconfig"))
    import kconfiglib

    profiles = args.profile or sorted(p.stem for p in CONFIGS.glob("*.mk"))
    bad = 0
    for profile in profiles:
        kconf = kconfiglib.Kconfig(str(KCONFIG), warn_to_stderr=False)
        if kconf.warnings:
            raise SystemExit("convert: Kconfig warnings:\n  " + "\n  ".join(kconf.warnings))
        text = convert(profile, kconf, kconfiglib)
        path = CONFIGS / f"{profile}.defconfig"
        if args.check:
            if not path.exists():
                print(f"convert: check {profile}: missing {path}")
                bad += 1
                continue
            old = path.read_text()
            if old != text:
                print("\n".join(difflib.unified_diff(
                    old.splitlines(), text.splitlines(),
                    fromfile=str(path), tofile=str(path) + " (converted)", lineterm="")))
                bad += 1
            continue
        path.write_text(text)
        print(f"convert: {profile} -> {path.relative_to(REPO)}")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
