#!/usr/bin/env python3
"""Generate a profile's build fragment from its defconfig.

One Kconfig tree is the source of truth.  This tool loads
``configs/<profile>.defconfig`` into ``Kconfig`` and writes the four files a
make (and, later, a CMake) build consumes:

  build/kconfig/<profile>/.config              the full config (not consumed in
                                               the M0/M1 step, kept for
                                               menuconfig and CMake)
  build/kconfig/<profile>/include/generated/autoconf.h
                                               the C header for the step after
                                               the -D equivalence is proven
  build/kconfig/<profile>/generated/config.mk  the make fragment the build
                                               includes: CONFIG_... variables
                                               and CFG_DEFS
  build/kconfig/<profile>/manifest.json        symbols, inputs, versions

The render has three channels; docs/BUILD-DESIGN.md is the authority:

  A  make variable only (the Makefile/fragment turns it into a -D)
  B  make variable and a CFG_DEFS -D (a symbol the old profile set twice)
  C  CFG_DEFS -D only (a -D that never had a make variable)

Only an explicitly set symbol is rendered (``Symbol.user_value is not None``),
except the always-emitted structural set.  That is what reproduces the old
Makefile's empty-when-unset behaviour: a profile that did not set
CONFIG_WLAN_OBS must leave WLAN_OBS empty, not fall back to a fragment
default (the old Makefile's unconditional ``:=`` made the fragment's ``?=``
dead).

Usage:
    tools/build/kconfig-gen.py --profile rtl8xxxu-8723bu-bringup
    tools/build/kconfig-gen.py --profile <p> --check
    tools/build/kconfig-gen.py --check-all
    tools/build/kconfig-gen.py --profile <p> --out /tmp/gen

Exit status: 0 ok; 1 an input/config error or a --check mismatch; 2 usage.
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
import re
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CONFIGS = REPO / "tests" / "board" / "rtos" / "configs"
KCONFIG = REPO / "Kconfig"
DEFAULT_OUT = REPO / "build" / "kconfig"

# The Kconfig files this tree sources.  Kept explicit: hashing every Kconfig
# found under the tree would also hash the GPL driver trees, whose Kconfig
# files must never become inputs to this build.
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

# The 51 names the legacy profile files and CONFIG_LEVERS carried.  The five
# choices are Kconfig choices; the other 46 are symbols.
MODELED = [
    "BACKEND", "BOARD", "CASES", "FREERTOS_HEAP_SIZE", "GMAC_PROF",
    "HEAP_SIZE", "LEVERS", "NET_WATCH", "OPT", "PROFILE",
    "RTL8XXXU_AP_FREQ", "RTL8XXXU_AP_SSID", "RTL8XXXU_CHIP",
    "RTL8XXXU_EXPECT_MAC", "RTL8XXXU_HB", "RTL8XXXU_USB_BUS",
    "RTW89_USB_BUS", "USB_MSC_DIAG", "USB_MSC_DIAG_FULL", "UVC_USB_BUS",
    "WLAN_OBS", "WLAN_PING_COUNT", "WLAN_PING_INTERVAL_MS",
    "WLAN_PING_TRACE", "WLAN_PUMP_DRAIN", "WLAN_PUMP_PRIO", "WLAN_RX_BA",
    "WLAN_SCAN_BATCH", "WLAN_SCAN_DIAG", "WLAN_TCP_SINK",
    "WLAN_TCP_SINK_SECONDS", "WLAN_UDP_PORT", "WLAN_UDP_SINK",
    "WLAN_UDP_SINK_SECONDS", "SHELL",
    "GMAC_RX_BATCH", "GMAC_RX_BH", "GMAC_RX_HWCSUM", "GMAC_RX_ZC",
    "GMAC_STALL_PROBE", "GMAC_SWEEP_WIDE", "GMAC_TCP_PROBE",
    "GMAC_TX_CSUM", "GMAC_TX_RING", "GMAC_TX_SWEEP_GUARD",
    "GMAC_TX_TAIL_IDLE", "LKPI_USB_OBS", "NETIO_CPU_KHZ",
    "NETIO_RAW_UDP", "NETIO_RXPATH", "NETIO_TCP_PROBE",
    "NETIO_TX_STALL_PROBE",
]

# Resource and toolchain values.  Unlike MODELED these are not legacy profile
# keys: the generator always writes the CONFIG_* make variable (defaults come
# from Kconfig) so a fragment can read it, and a profile overrides a value by
# putting the symbol in its defconfig.  The values are part of the image.
RESOURCES = [
    "BOARD_AUTO_RESET", "SHELL_STACK_KB", "RUNNER_STACK_KB",
    "RUNNER_BIG_STACK_KB", "CMSIS2_MAX_THREADS", "CMSIS2_MAX_TIMERS",
    "TX_TIMER_STACK", "TX_TICKS_PER_SECOND",
    "LKPI_SKB_POOL_NUM", "LKPI_SKB_DATA_POOL_NUM", "LKPI_SKB_HEAP_HDR_MAX",
    "LKPI_USB_CHERRYUSB_SLOTS", "USB_STAGE_SIZE", "USBHOST_MSC_TIMEOUT",
    "USB_EHCI_ISO_NUM", "LWIP_TCPIP_CORE_LOCKING_INPUT",
    "LWIP_TCP_ACK_DELAY_SEGMENTS", "LWIPERF_TCP_MAX_IDLE_SEC",
    "NETIO_CPU_KHZ_LATE", "ULOG_ENABLED", "ULOG_MAX_MESSAGE_LENGTH",
    "PRINTF_DISABLE_FLOAT", "PRINTF_DISABLE_EXPONENTIAL",
]

# Resources that a C file reads directly (the macro it consumes is the
# CONFIG_ name itself, so the value must also appear in CFG_DEFS).  Only an
# explicit defconfig line adds the token; the legacy residual LEVERS keep
# their own -DCONFIG_... token until a profile is migrated.
RESOURCE_CFG = {"NETIO_CPU_KHZ_LATE"}

# Resources whose C macro is the bare lwIP name, not a CONFIG_* name.  When a
# profile sets one explicitly, emit the raw -D token; the token comes after
# PROFILE_DEFS, so it wins over the perf default.
RESOURCE_RAW = {"LWIP_TCPIP_CORE_LOCKING_INPUT", "LWIP_TCP_ACK_DELAY_SEGMENTS"}

CHOICES = {
    "BACKEND": {
        "BACKEND_FREERTOS": "freertos",
        "BACKEND_THREADX": "threadx",
    },
    "BOARD": {
        "BOARD_RK3568_ITX": "rk3568-itx",
        "BOARD_FIREFLY_V1": "firefly-v1",
        "BOARD_FIREFLY_V2": "firefly-v2",
    },
    "OPT": {
        "OPT_O0": "-O0",
        "OPT_O2": "-O2",
        "OPT_O3": "-O3",
        "OPT_OS": "-Os",
        "OPT_OG": "-Og",
    },
    "DEBUG_LEVEL": {
        "DEBUG_G0": "0",
        "DEBUG_G1": "1",
        "DEBUG_G2": "2",
        "DEBUG_G3": "3",
    },
    "PROFILE": {
        "PROFILE_BASE": "base",
        "PROFILE_PERF": "perf",
    },
    "RTL8XXXU_CHIP": {
        "RTL8XXXU_CHIP_8188EU": "8188eu",
        "RTL8XXXU_CHIP_8723BU": "8723bu",
    },
}

# Always emitted, even when the defconfig has no explicit line: these are the
# structural values the old profiles always carried (or the Makefile default
# that was always live).
ALWAYS_MAKE = {
    "BACKEND", "BOARD", "CASES", "OPT", "PROFILE", "HEAP_SIZE", "LEVERS",
    "GMAC_PROF",
}

# Channel B: make variable plus CFG_DEFS token.
CHANNEL_B = {"GMAC_PROF", "WLAN_RX_BA", "NET_WATCH", "SHELL"}

# Channel C: CFG_DEFS token only (the 17 lever-only names).
CFG_ONLY = {
    "GMAC_RX_BATCH", "GMAC_RX_BH", "GMAC_RX_HWCSUM", "GMAC_RX_ZC",
    "GMAC_STALL_PROBE", "GMAC_SWEEP_WIDE", "GMAC_TCP_PROBE",
    "GMAC_TX_CSUM", "GMAC_TX_RING", "GMAC_TX_SWEEP_GUARD",
    "GMAC_TX_TAIL_IDLE", "LKPI_USB_OBS", "NETIO_CPU_KHZ",
    "NETIO_RAW_UDP", "NETIO_RXPATH", "NETIO_TCP_PROBE",
    "NETIO_TX_STALL_PROBE",
}

MAKE_ONLY = set(MODELED) - set(CHOICES) - ALWAYS_MAKE - CFG_ONLY

# A modeled symbol that a residual LEVERS token still names is a converter
# bug: the token would be emitted twice and the last-wins order would move.
LEVER_TOKEN = re.compile(r"^-DCONFIG_([A-Z0-9_]+)=")

KC = None  # set by load_kconf()


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


def find_symbol(kconf, name):
    sym = kconf.syms.get(name)
    if sym is None:
        raise GenError(f"symbol {name} is not defined in the Kconfig tree")
    return sym


def is_bool(sym) -> bool:
    return sym.orig_type in (KC.BOOL, KC.TRISTATE)


def explicit(sym) -> bool:
    return sym.user_value is not None


def bool_value(sym) -> str:
    return "1" if sym.str_value == "y" else "0"


def choice_value(kconf, name: str) -> str:
    for choice in kconf.choices:
        if choice.name == name:
            if choice.selection is None:
                raise GenError(f"choice {name} has no selection")
            return CHOICES[name][choice.selection.name]
    raise GenError(f"choice {name} is not defined in the Kconfig tree")


def choice_explicit(kconf, name: str) -> bool:
    for choice in kconf.choices:
        if choice.name == name:
            return choice.selection is not None and choice.selection.user_value is not None
    raise GenError(f"choice {name} is not defined in the Kconfig tree")


def render_values(kconf) -> tuple[dict[str, str], list[str], dict[str, str | None]]:
    """Return (make variables, CFG_DEFS tokens, rendered values for manifest)."""
    make: dict[str, str] = {}
    cfg: list[str] = []
    values: dict[str, str | None] = {name: None for name in MODELED + RESOURCES + ["DEBUG_LEVEL"]}

    def add_make(name: str, value: str):
        make[f"CONFIG_{name}"] = value
        values[name] = value

    def add_cfg(name: str, value: str):
        cfg.append(f"-DCONFIG_{name}={value}")
        values[name] = value

    # Choices: the 4 structural ones are always emitted; the chip choice only
    # when the defconfig selected it, so the fragment's ?= default stays live
    # for the profiles that never set it.
    for name in ("BACKEND", "BOARD", "OPT", "DEBUG_LEVEL", "PROFILE"):
        add_make(name, choice_value(kconf, name))
    if choice_explicit(kconf, "RTL8XXXU_CHIP"):
        add_make("RTL8XXXU_CHIP", choice_value(kconf, "RTL8XXXU_CHIP"))

    for name in MODELED:
        if name in CHOICES:
            continue
        sym = find_symbol(kconf, name)
        if name == "HEAP_SIZE":
            add_make(name, f"{sym.str_value}M")
            continue
        if name == "FREERTOS_HEAP_SIZE":
            n = int(sym.str_value)
            if n > 0:
                add_make(name, f"{n}u")
            continue
        if name in ("CASES", "LEVERS"):
            add_make(name, sym.str_value)
            continue
        if name == "GMAC_PROF":
            add_make(name, bool_value(sym))
            add_cfg(name, bool_value(sym))
            continue
        if name in CHANNEL_B:
            if explicit(sym):
                add_make(name, bool_value(sym))
                add_cfg(name, bool_value(sym))
            continue
        if name in CFG_ONLY:
            if not explicit(sym):
                continue
            add_cfg(name, bool_value(sym) if is_bool(sym) else sym.str_value)
            continue
        # Channel A: make variable only.
        if not explicit(sym):
            continue
        if is_bool(sym):
            add_make(name, bool_value(sym))
        else:
            add_make(name, sym.str_value)

    # Resources are always rendered: their defaults are part of the image
    # contract, not a legacy-profile presence test.
    for name in RESOURCES:
        sym = find_symbol(kconf, name)
        if is_bool(sym):
            add_make(name, bool_value(sym))
        else:
            add_make(name, sym.str_value)
        if name in RESOURCE_CFG and explicit(sym):
            add_cfg(name, sym.str_value)
        if name in RESOURCE_RAW and explicit(sym):
            value = bool_value(sym) if is_bool(sym) else sym.str_value
            cfg.append(f"-D{name}={value}")

    # Guard: the residual LEVERS must not still name a modeled symbol.
    residual = make.get("CONFIG_LEVERS", "")
    for token in residual.split():
        m = LEVER_TOKEN.match(token)
        if m and m.group(1) in MODELED:
            raise GenError(
                "residual CONFIG_LEVERS still names the modeled symbol "
                f"{m.group(1)}: {token}")
        if "$" in token or "#" in token:
            raise GenError(f"CONFIG_LEVERS token has a make-hostile character: {token!r}")

    if "$" in make.get("CONFIG_CASES", "") or "#" in make.get("CONFIG_CASES", ""):
        raise GenError("CONFIG_CASES has a make-hostile character")

    return make, cfg, values


def config_mk_text(profile: str, make: dict[str, str], cfg: list[str]) -> str:
    order = ["BACKEND", "BOARD", "CASES", "OPT", "DEBUG_LEVEL", "PROFILE",
             "HEAP_SIZE", "FREERTOS_HEAP_SIZE", "LEVERS", "GMAC_PROF"]
    lines = [
        f"# Generated by tools/build/kconfig-gen.py from configs/{profile}.defconfig.",
        "# Do not edit; edit the defconfig and rerun the generation.",
        "",
    ]
    seen = set()
    for name in order:
        key = f"CONFIG_{name}"
        if key in make:
            lines.append(f"{key} := {make[key]}")
            seen.add(key)
    for name in MODELED + RESOURCES:
        key = f"CONFIG_{name}"
        if key in make and key not in seen:
            lines.append(f"{key} := {make[key]}")
            seen.add(key)
    lines.append("")
    lines.append("CFG_DEFS := " + " ".join(cfg))
    lines.append("")
    return "\n".join(lines)


def manifest(profile: str, kconf, values: dict[str, str | None], cfg: list[str],
             defconfig: Path) -> dict:
    return {
        "schema": 1,
        "profile": profile,
        "defconfig": f"configs/{profile}.defconfig",
        "generator": "tools/build/kconfig-gen.py",
        "kconfiglib": ".".join(str(x) for x in KC.VERSION),
        "cfg_defs": cfg,
        "symbols": values,
        "inputs_sha256": {
            **{p: sha256(REPO / p) for p in KCONFIG_FILES},
            f"configs/{profile}.defconfig": sha256(defconfig),
        },
    }


def write_if_changed(path: Path, text: str) -> bool:
    if path.exists() and path.read_text() == text:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(text)
    os.replace(tmp, path)
    return True


def csh_bool(kconf, name: str, default: bool = False) -> int:
    sym = kconf.syms.get(name)
    if sym is None:
        return 1 if default else 0
    return 1 if sym.str_value == "y" else 0


def csh_int(kconf, name: str, default: int) -> int:
    sym = kconf.syms.get(name)
    if sym is None or sym.str_value == "":
        return default
    try:
        return int(sym.str_value)
    except ValueError:
        return default


def csh_config_text(kconf) -> str:
    """Render the macro header the vendored CherrySH core includes.

    CherrySH has no Kconfig of its own; this is the one generated object that
    turns the symbols in components/shell/Kconfig into the core's compile-time
    configuration.  Values the shell does not expose as options keep the
    reference port's defaults (one blocking repl thread, static line buffer).
    """
    autorun_sym = kconf.syms.get("SHELL_AUTORUN")
    autorun = autorun_sym.str_value if autorun_sym is not None else ""
    autorun = autorun.replace("\\", "\\\\").replace('"', '\\"')
    max_arg = csh_int(kconf, "CSH_MAX_ARG", 8)
    shell = csh_bool(kconf, "SHELL")
    batch = csh_bool(kconf, "SHELL_BATCH")
    lines = [
        "/* Generated by tools/build/kconfig-gen.py; do not edit. */",
        "#ifndef CSH_CONFIG_H",
        "#define CSH_CONFIG_H",
        "",
        "#define CONFIG_SHELL %d" % shell,
        "#define CONFIG_SHELL_BATCH %d" % batch,
        '#define CONFIG_SHELL_AUTORUN "%s"' % autorun,
        "",
        "/* argument check */",
        "#define CONFIG_CSH_DEBUG 0",
        "",
        "/* default row / column */",
        "#define CONFIG_CSH_DFTROW 25",
        "#define CONFIG_CSH_DFTCOL 80",
        "",
        "/* history support <+550byte> */",
        "#define CONFIG_CSH_HISTORY %d" % csh_bool(kconf, "CSH_HISTORY", True),
        "",
        "/* completion support <+1100byte> */",
        "#define CONFIG_CSH_COMPLETION %d" % csh_bool(kconf, "CSH_COMPLETION", True),
        "",
        "/* max completion item list count (use stack 4 x count byte) */",
        "#define CONFIG_CSH_MAX_COMPLETION 40",
        "",
        "/* prompt edit support <+1000byte> */",
        "#define CONFIG_CSH_PROMPTEDIT %d" % csh_bool(kconf, "CSH_PROMPTEDIT", False),
        "",
        "/* prompt segment count */",
        "#define CONFIG_CSH_PROMPTSEG 7",
        "",
        "/* xterm support */",
        "#define CONFIG_CSH_XTERM 0",
        "",
        "/* newline */",
        '#define CONFIG_CSH_NEWLINE "\\r\\n"',
        "",
        "/* tab space count */",
        "#define CONFIG_CSH_SPACE 4",
        "",
        "/* independent ctrl/alt map */",
        "#define CONFIG_CSH_CTRLMAP 0",
        "#define CONFIG_CSH_ALTMAP 0",
        "",
        "/* refresh prompt */",
        "#define CONFIG_CSH_REFRESH_PROMPT 0",
        "",
        "/* no waiting for sget (0: the backend blocks until data arrives) */",
        "#define CONFIG_CSH_NOBLOCK 0",
        "",
        "/* help information */",
        '#define CONFIG_CSH_HELP ""',
        "",
        "/* path length 0:const path, <=255:variable path */",
        "#define CONFIG_CSH_MAXLEN_PATH 128",
        "",
        "/* path segment count */",
        "#define CONFIG_CSH_MAXSEG_PATH 16",
        "",
        "/* user count */",
        "#define CONFIG_CSH_MAX_USER 1",
        "",
        "/* max argument count */",
        "#define CONFIG_CSH_MAX_ARG %d" % max_arg,
        "",
        "/* linebuffer static or on stack */",
        "#define CONFIG_CSH_LNBUFF_STATIC 1",
        "",
        "/* linebuffer size (valid only if lnbuff on stack) */",
        "#define CONFIG_CSH_LNBUFF_SIZE 256",
        "",
        "/* multi-thread mode */",
        "#define CONFIG_CSH_MULTI_THREAD 0",
        "",
        "/* independent signal handler (for multi instances) */",
        "#define CONFIG_CSH_SIGNAL_HANDLER 0",
        "",
        "/* Ctrl+c/d/q/s/z/\\ F1-F12 UE <+120byte> */",
        "#define CONFIG_CSH_USER_CALLBACK 1",
        "",
        "/* enable macro export symbol table */",
        "#define CONFIG_CSH_SYMTAB %d" % csh_bool(kconf, "CSH_SYMTAB", True),
        "",
        "/* print buffer size */",
        "#define CONFIG_CSH_PRINT_BUFFER_SIZE 512",
        "",
        "#endif /* CSH_CONFIG_H */",
        "",
    ]
    return "\n".join(lines)


def render_dotconfig(kconf, profile: str) -> tuple[str, str]:
    with tempfile.TemporaryDirectory() as td:
        dotconfig = Path(td) / ".config"
        autoconf = Path(td) / "autoconf.h"
        kconf.write_config(
            str(dotconfig), save_old=False,
            header=f"# Generated by tools/build/kconfig-gen.py from configs/{profile}.defconfig.\n")
        kconf.write_autoconf(str(autoconf))
        return dotconfig.read_text(), autoconf.read_text()


def generate(profile: str, defconfig: Path, out: Path, check: bool) -> int:
    if not defconfig.is_file():
        raise GenError(f"missing defconfig: {defconfig}")
    kconf = load_kconf()
    kconf.load_config(str(defconfig))
    if kconf.missing_syms:
        raise GenError(
            f"{defconfig}: undefined symbols:\n  " +
            "\n  ".join(f"{name}={val}" for name, val in kconf.missing_syms))
    make, cfg, values = render_values(kconf)
    dotconfig, autoconf = render_dotconfig(kconf, profile)
    files = {
        out / "generated" / "config.mk": config_mk_text(profile, make, cfg),
        out / "manifest.json": json.dumps(manifest(profile, kconf, values, cfg, defconfig),
                                          indent=2, sort_keys=True) + "\n",
        out / ".config": dotconfig,
        out / "include" / "generated" / "autoconf.h": autoconf,
        out / "include" / "generated" / "csh_config.h": csh_config_text(kconf),
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
    ap.add_argument("--out", help="output directory (default build/kconfig/<profile>)")
    ap.add_argument("--check", action="store_true",
                    help="compare against the existing output, do not write")
    ap.add_argument("--check-all", action="store_true",
                    help="check every configs/*.defconfig")
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
        if not args.profile and not args.defconfig:
            ap.error("pass --profile or --defconfig")
        profile = args.profile
        if args.defconfig:
            defconfig = Path(args.defconfig)
            if not profile:
                raise GenError("--profile is required with --defconfig")
        else:
            defconfig = CONFIGS / f"{profile}.defconfig"
        out = Path(args.out) if args.out else DEFAULT_OUT / profile
        return generate(profile, defconfig, out, args.check)
    except GenError as e:
        print(f"kconfig-gen: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
