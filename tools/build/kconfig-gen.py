#!/usr/bin/env python3
"""Generate the one configuration pair for a profile.

Inputs: the Kconfig tree and one defconfig (or a freewificam_config already
saved from menuconfig).  Outputs, and only these:

  build/kconfig/<profile>/freewificam_config     Kconfig output, make-includable
  build/kconfig/<profile>/freewificam_config.h   generated C header
  build/kconfig/<profile>/manifest.json          inputs and values

The build reads the make file directly and force-includes the header.  There
is no CFG_DEFS channel and no -D token: every macro the C side sees is in a
header (this generated one, or a fixed header that includes it).

Usage:
  tools/build/kconfig-gen.py --profile <name>
  tools/build/kconfig-gen.py --profile <name> --check
  tools/build/kconfig-gen.py --check-all
  tools/build/kconfig-gen.py --from-config build/kconfig/<p>/freewificam_config --out ...
  tools/build/kconfig-gen.py --from-config ... --savedefconfig configs/<p>.defconfig
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CONFIGS = REPO / "tests" / "board" / "rtos" / "configs"
KCONFIG = REPO / "Kconfig"
DEFAULT_OUT = REPO / "build" / "kconfig"

KCONFIG_FILES = [
    "Kconfig",
    "tests/board/rtos/Kconfig",
    "tests/board/rtos/platform/board/Kconfig",
    "tests/board/rtos/app/Kconfig",
    "components/linuxkpi/Kconfig",
    "components/net80211/Kconfig",
    "components/shell/Kconfig",
    "wireless-drivers/Kconfig",
    "third-party/Kconfig",
]

# Case list copied from the runner table / all defconfigs.  The hidden
# CASE_<NAME> symbols are derived by this generator from CONFIG_CASES; they are
# not edited by a user.
CASES = """tick cmsis2 net net_loop net_mdk_netio net_pps net_xmac nvme pcie
pcie_7260 pcie_bench pcie_ecam emmc rtl8xxxu rtl8xxxu_dataplane rtl8xxxu_join
rtl8xxxu_net rtl8xxxu_netio rtl8xxxu_scan rtw88 rtw88_scan rtw89
rtw89_dataplane rtw89_join rtw89_scan sdio_mci usb_ehci usb_xhci uvc_detect
uvc_iso_smoke uvc_open uvc_stream wlan iwl7260 iwl7260_dataplane iwl7260_join
iwl7260_net iwl7260_scan""".split()

KC = None


class GenError(Exception):
    pass


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_kconf():
    global KC
    sys.path.insert(0, str(REPO / "tools" / "kconfig"))
    import kconfiglib  # noqa: E402

    KC = kconfiglib
    if not KCONFIG.is_file():
        raise GenError(f"missing Kconfig tree: {KCONFIG}")
    kconf = kconfiglib.Kconfig(str(KCONFIG), warn_to_stderr=False)
    if kconf.warnings:
        raise GenError("Kconfig warnings:\n  " + "\n  ".join(kconf.warnings))
    return kconf


def all_symbols(kconf):
    """Every defined, non-choice symbol, in stable name order."""
    return sorted(kconf.unique_defined_syms, key=lambda s: s.name)


def sym_value(sym) -> str:
    return sym.str_value


def is_bool(sym) -> bool:
    return sym.orig_type in (KC.BOOL, KC.TRISTATE)


def c_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def check_no_levers(kconf) -> None:
    if "LEVERS" in kconf.syms:
        raise GenError("CONFIG_LEVERS still exists in the Kconfig tree")
    for sym in kconf.unique_defined_syms:
        if sym.name == "LEVERS":
            raise GenError("LEVERS is still defined")


def ensure_values(kconf) -> None:
    """Fail loudly when a required configured value is empty."""
    required = [
        "BACKEND_NAME", "BOARD_NAME", "OPT_FLAGS", "PROFILE_NAME",
        "DEBUG_LEVEL_VALUE", "CASES", "HEAP_SIZE", "FREERTOS_HEAP_SIZE",
    ]
    for name in required:
        sym = kconf.syms.get(name)
        if sym is None:
            raise GenError(f"required symbol {name} is missing")
        if sym.str_value == "":
            raise GenError(f"required symbol {name} has no value")


def read_git_tag() -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(REPO), "describe", "--always", "--dirty"],
            text=True, stderr=subprocess.DEVNULL).strip() or "unknown"
    except Exception:
        return "unknown"


def legacy_macros(kconf) -> list[str]:
    """Macros the old -D flags carried that are not Kconfig symbol names.

    These are aliases of generated CONFIG_* values, not a second default
    channel: every one is derived from a symbol that is always present.
    """
    def val(name: str) -> str:
        sym = kconf.syms.get(name)
        if sym is None:
            raise GenError(f"legacy macro source {name} is missing")
        return sym.str_value

    def flag(name: str) -> bool:
        return val(name) == "y"

    selected_cases = set(val("CASES").split())
    lines = [
        "",
        "/* ---- legacy names consumed by existing sources ---- */",
        "#define FW_BUILD_TAG " + c_string(read_git_tag()),
        "#define RTOS_BACKEND_NAME " + c_string(val("BACKEND_NAME")),
        "#define RTOS_KERNEL_NAME " + c_string(val("BACKEND_NAME")),
    ]
    lines.append("#ifndef FREEWIFICAM_NO_CASE_MACROS")
    for name in CASES:
        if name in selected_cases:
            lines.append(f"#define RTOS_CASE_{name.upper()} 1")
    lines.append("#endif /* FREEWIFICAM_NO_CASE_MACROS */")

    lines += [
        f"#define RTL8XXXU_AP_FREQ {val('RTL8XXXU_AP_FREQ')}",
        f"#define RTL8XXXU_USB_BUS {val('RTL8XXXU_USB_BUS')}",
        f"#define RTW89_USB_BUS {val('RTW89_USB_BUS')}",
        f"#define UVC_USB_BUS {val('UVC_USB_BUS')}",
        f"#define RTL8XXXU_USB_STAGE_SIZE {val('RTL8XXXU_USB_STAGE_SIZE')}",
        f"#define ULOG_MAX_MESSAGE_LENGTH {val('ULOG_MAX_MESSAGE_LENGTH')}",
        f"#define CMSIS2_MAX_THREADS {val('CMSIS2_MAX_THREADS')}",
        f"#define CMSIS2_MAX_TIMERS {val('CMSIS2_MAX_TIMERS')}",
        f"#define TX_TIMER_STACK {val('TX_TIMER_STACK')}",
        f"#define TX_TICKS_PER_SECOND {val('TX_TICKS_PER_SECOND')}",
        f"#define WLAN_SCAN_BATCH {val('WLAN_SCAN_BATCH')}",
        f"#define WLAN_PING_COUNT {val('WLAN_PING_COUNT')}u",
        f"#define WLAN_PING_INTERVAL_MS {val('WLAN_PING_INTERVAL_MS')}",
        f"#define WLAN_TCP_SINK_SECONDS {val('WLAN_TCP_SINK_SECONDS')}",
        f"#define WLAN_UDP_PORT {val('WLAN_UDP_PORT')}",
        f"#define WLAN_UDP_SINK_SECONDS {val('WLAN_UDP_SINK_SECONDS')}",
        f"#define WLAN_PUMP_PRIO_LEVEL {val('WLAN_PUMP_PRIO_LEVEL')}",
    ]
    # Strings that used to come from the fragment's quoting.
    ssid = val("RTL8XXXU_AP_SSID")
    lines.append("#define RTL8XXXU_AP_SSID " + c_string(ssid))
    mac = val("RTL8XXXU_EXPECT_MAC")
    parts = mac.replace("-", ":").split(":")
    if len(parts) != 6:
        raise GenError(f"RTL8XXXU_EXPECT_MAC is not a MAC address: {mac!r}")
    try:
        bytes_ = [int(p, 16) for p in parts]
    except ValueError as exc:
        raise GenError(f"RTL8XXXU_EXPECT_MAC is not hex: {mac!r}") from exc
    lines.append("#define RTL8XXXU_EXPECT_MAC "
                 + "{ " + ", ".join(f"0x{b:02x}" for b in bytes_) + " }")

    # Boolean feature names: define only when on, preserving #ifdef semantics.
    for sym_name, macro in [
        ("RTL8XXXU_HB", "RTL8XXXU_HB"),
        ("NET_WATCH", "NET_WATCH"),
        ("ULOG_ENABLED", "ULOG_ENABLED"),
        ("PRINTF_DISABLE_FLOAT", "PRINTF_DISABLE_SUPPORT_FLOAT"),
        ("PRINTF_DISABLE_EXPONENTIAL", "PRINTF_DISABLE_SUPPORT_EXPONENTIAL"),
        ("WLAN_OBS", "WLAN_OBS"),
    ]:
        if flag(sym_name):
            lines.append(f"#define {macro} 1")
    if flag("WLAN_OBS"):
        lines += ["#define CONFIG_LKPI_CENSUS 1",
                  "#define LKPI_SDIO_OBS_RX_REQ_ADDR 0x001c",
                  "#define LKPI_SDIO_OBS_HISR_ADDR 0x0018"]
    if flag("NET_WATCH"):
        lines.append("#define CONFIG_NET_WATCH 1")
    if flag("USB_MSC_DIAG"):
        lines.append("#define MSC_DIAG 1")
    if flag("USB_MSC_DIAG_FULL"):
        lines.append("#define MSC_DIAG_FULL 1")
    if flag("BOARD_AUTO_RESET"):
        lines.append("#define RTOS_BOARD_AUTO_RESET 1")
    else:
        lines.append("#define RTOS_BOARD_AUTO_RESET 0")

    if "emmc" in selected_cases:
        lines.append("#define CONFIG_FSL_SDMMC_ENABLE_MMC 1")
    if any(c in selected_cases for c in
           ("wlan", "rtw88", "sdio_mci", "rtw88_scan")):
        lines.append("#define CONFIG_FSL_SDMMC_ENABLE_SDIO 1")

    # WLAN knobs whose C code uses the unprefixed name.
    for name in ("WLAN_TCP_SINK", "WLAN_UDP_SINK", "WLAN_SCAN_DIAG",
                 "WLAN_PING_TRACE", "WLAN_PUMP_DRAIN"):
        if flag(name):
            lines.append(f"#define {name} 1")
    return lines


def render_header(kconf) -> str:
    lines = [
        "/* Generated by tools/build/kconfig-gen.py; do not edit. */",
        "#ifndef FREEWIFICAM_CONFIG_H",
        "#define FREEWIFICAM_CONFIG_H",
        "",
    ]
    for sym in all_symbols(kconf):
        if sym.name.endswith("_NAME") and sym.orig_type == KC.STRING:
            pass
        if is_bool(sym):
            if sym.str_value == "y":
                lines.append(f"#define CONFIG_{sym.name} 1")
            else:
                lines.append(f"/* CONFIG_{sym.name} is not set */")
        elif sym.orig_type == KC.STRING:
            lines.append(f"#define CONFIG_{sym.name} {c_string(sym.str_value)}")
        elif sym.orig_type in (KC.INT, KC.HEX):
            lines.append(f"#define CONFIG_{sym.name} {sym.str_value or '0'}")
    lines += legacy_macros(kconf)
    lines += ["", "#endif /* FREEWIFICAM_CONFIG_H */", ""]
    return "\n".join(lines)


def render_config(kconf, profile: str) -> str:
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        path = Path(td) / "freewificam_config"
        kconf.write_config(
            str(path), save_old=False,
            header=(f"# Generated by tools/build/kconfig-gen.py from "
                    f"configs/{profile}.defconfig; edit the defconfig or use "
                    f"make.rtos menuconfig.\n"))
        text = path.read_text()
    # Make cannot branch on "y"/"n": normalize the booleans to 1/0.  The C
    # header keeps the Kconfig spelling (true -> #define CONFIG_X 1, false ->
    # a comment), so both consumers see their native form.
    text = re.sub(r"^CONFIG_([A-Z0-9_]+)=y$", r"CONFIG_\1=1", text, flags=re.M)
    text = re.sub(r"^# CONFIG_([A-Z0-9_]+) is not set$", r"CONFIG_\1=0", text,
                  flags=re.M)
    if re.search(r"CONFIG_LEVERS", text):
        raise GenError("generated config still contains CONFIG_LEVERS")
    return text


def manifest(profile: str, defconfig: Path, config_text: str, header: str,
             kconf) -> dict:
    values = {}
    for sym in all_symbols(kconf):
        values[sym.name] = sym.str_value
    return {
        "schema": 2,
        "profile": profile,
        "defconfig": f"configs/{profile}.defconfig",
        "generator": "tools/build/kconfig-gen.py",
        "kconfiglib": ".".join(str(x) for x in KC.VERSION),
        "inputs_sha256": {
            **{p: sha256(REPO / p) for p in KCONFIG_FILES},
            f"configs/{profile}.defconfig": sha256(defconfig),
        },
        "outputs_sha256": {
            "freewificam_config": hashlib.sha256(config_text.encode()).hexdigest(),
            "freewificam_config.h": hashlib.sha256(header.encode()).hexdigest(),
        },
        "symbols": values,
    }


def write_if_changed(path: Path, text: str) -> bool:
    if path.exists() and path.read_text() == text:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(text)
    os.replace(tmp, path)
    return True


def generate(profile: str, defconfig: Path | None, out: Path, check: bool,
             config: Path | None = None) -> int:
    kconf = load_kconf()
    if defconfig is None and config is None:
        defconfig = CONFIGS / f"{profile}.defconfig"
    if config is not None:
        if not config.is_file():
            raise GenError(f"missing config: {config}")
        kconf.load_config(str(config))
        if kconf.missing_syms:
            raise GenError(
                f"{config}: undefined symbols:\n  " +
                "\n  ".join(f"{name}={val}" for name, val in kconf.missing_syms))
        if defconfig is None:
            defconfig = CONFIGS / f"{profile}.defconfig"
            if not defconfig.is_file():
                raise GenError(f"missing defconfig for profile {profile}: {defconfig}")
    else:
        if defconfig is None or not defconfig.is_file():
            raise GenError(f"missing defconfig: {defconfig}")
        kconf.load_config(str(defconfig))
        if kconf.missing_syms:
            raise GenError(
                f"{defconfig}: undefined symbols:\n  " +
                "\n  ".join(f"{name}={val}" for name, val in kconf.missing_syms))

    check_no_levers(kconf)
    ensure_values(kconf)
    config_text = render_config(kconf, profile)
    header = render_header(kconf)
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        dot = Path(td) / ".config"
        kconf.write_config(
            str(dot), save_old=False,
            header=(f"# Generated by tools/build/kconfig-gen.py from "
                    f"configs/{profile}.defconfig; edit the defconfig or use "
                    f"make.rtos menuconfig.\n"))
        dotconfig_text = dot.read_text()
    files = {
        out / ".config": dotconfig_text,
        out / "freewificam_config": config_text,
        out / "freewificam_config.h": header,
        out / "manifest.json": json.dumps(
            manifest(profile, defconfig, config_text, header, kconf),
            indent=2, sort_keys=True) + "\n",
    }
    if check:
        diffs = []
        for path, text in files.items():
            if not path.exists():
                diffs.append(f"missing {path}")
                continue
            old = path.read_text()
            if old != text:
                diffs.append("\n".join(difflib.unified_diff(
                    old.splitlines(), text.splitlines(),
                    fromfile=str(path), tofile=str(path) + " (generated)",
                    lineterm="")))
        if diffs:
            print("\n".join(diffs))
            return 1
        return 0
    for path, text in files.items():
        write_if_changed(path, text)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--profile", help="defconfig name (without .defconfig)")
    ap.add_argument("--defconfig", help="explicit defconfig path")
    ap.add_argument("--from-config", help="load an existing freewificam_config")
    ap.add_argument("--out", help="output directory (default build/kconfig/<profile>)")
    ap.add_argument("--savedefconfig", help="write a minimal defconfig from --from-config")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--check-all", action="store_true")
    args = ap.parse_args()

    try:
        if args.check_all:
            bad = 0
            for defconfig in sorted(CONFIGS.glob("*.defconfig")):
                profile = defconfig.stem
                rc = generate(profile, defconfig, DEFAULT_OUT / profile, check=True)
                print(f"kconfig-gen: check {profile}: {'ok' if rc == 0 else 'DIFF'}")
                bad += rc
            if bad:
                print(f"kconfig-gen: {bad} profile(s) out of date", file=sys.stderr)
                return 1
            return 0
        config = Path(args.from_config) if args.from_config else None
        defconfig = Path(args.defconfig) if args.defconfig else None
        if not args.profile and not args.defconfig and config is None:
            ap.error("pass --profile, --defconfig or --from-config")
        profile = args.profile
        if profile is None:
            if defconfig is not None:
                profile = defconfig.stem
            elif config is not None:
                raise GenError("--profile is required with --from-config")
        if args.savedefconfig:
            if config is None:
                raise GenError("--savedefconfig requires --from-config")
            kconf = load_kconf()
            kconf.load_config(str(config))
            check_no_levers(kconf)
            ensure_values(kconf)
            kconf.write_min_config(args.savedefconfig)
            return 0
        out = Path(args.out) if args.out else DEFAULT_OUT / profile
        return generate(profile, defconfig, out, args.check, config)
    except GenError as e:
        print(f"kconfig-gen: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
