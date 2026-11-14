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

Two vendored cores have their own option headers instead of including the
Kconfig header: CherrySH (csh_config.h) and lwIP (lwipopts_kconfig.h).  They are
generated into include/generated/ from the same symbols, so those cores still
consume Kconfig values and never a -D token.

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
    "components/liblinux80211/Kconfig",
    "third-party/Kconfig",
]

# Case list copied from the runner table / all defconfigs.  The hidden
# CASE_<NAME> symbols are derived by this generator from CONFIG_CASES; they are
# not edited by a user.
CASES = """tick cmsis2 net pcie emmc usb_xhci usb_ehci kpi_live kpi_mock
         mac80211_c2 iwl7260_c3 iwl7260_c4 iwl7260_c5 iwl7260_c6 iwl7260_stab
         iwl7260_ap iwl7260_sta_wpa2 iwl7260_ap5g iwl7260_sta5g
         iwl7260_mon rtl8189fs_sdio rtl8189fs_sta rtl8189fs_sta_wpa2
         rtl8189fs_c6t rtl8189fs_ap rtw89_c3 rtw89_c4 rtw89_c5
         rtw89_c6 rtw89_ap""".split()

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

    This line's set is the previous tree's, trimmed to the names the imported
    sources read.  The driver/wireless aliases (RTL8XXXU_*, RTW*, WLAN_*,
    TX_*) left with their worlds; when a world comes back it brings its rows
    here and its symbols into the Kconfig tree in the same commit.
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
        "#define RTOS_KERNEL_NAME " + c_string("FreeRTOS-Kernel V11.3.1"),
    ]
    lines.append("#ifndef FREEWIFICAM_NO_CASE_MACROS")
    for name in CASES:
        if name in selected_cases:
            lines.append(f"#define RTOS_CASE_{name.upper()} 1")
    lines.append("#endif /* FREEWIFICAM_NO_CASE_MACROS */")

    lines += [
        f"#define ULOG_MAX_MESSAGE_LENGTH {val('ULOG_MAX_MESSAGE_LENGTH')}",
        f"#define CMSIS2_MAX_THREADS {val('CMSIS2_MAX_THREADS')}",
        f"#define CMSIS2_MAX_TIMERS {val('CMSIS2_MAX_TIMERS')}",
    ]

    # Boolean feature names: define only when on, preserving #ifdef semantics.
    for sym_name, macro in [
        ("ULOG_ENABLED", "ULOG_ENABLED"),
        ("PRINTF_DISABLE_FLOAT", "PRINTF_DISABLE_SUPPORT_FLOAT"),
        ("PRINTF_DISABLE_EXPONENTIAL", "PRINTF_DISABLE_SUPPORT_EXPONENTIAL"),
    ]:
        if flag(sym_name):
            lines.append(f"#define {macro} 1")
    if flag("USB_MSC_DIAG"):
        lines.append("#define MSC_DIAG 1")
    if flag("USB_MSC_DIAG_FULL"):
        lines.append("#define MSC_DIAG_FULL 1")
    if flag("BOARD_AUTO_RESET"):
        lines.append("#define RTOS_BOARD_AUTO_RESET 1")
    else:
        lines.append("#define RTOS_BOARD_AUTO_RESET 0")

    # Case-derived card-type selection: the fsl wrapper has no other source
    # for it (the previous tree derived it from its CASE_DEFS make variable).
    if "emmc" in selected_cases:
        lines.append("#define CONFIG_FSL_SDMMC_ENABLE_MMC 1")
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
    """Render the macro header the vendored CherrySH core includes."""
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


def lwip_config_text(kconf) -> str:
    """Render the lwIP option header included by the port's lwipopts.h."""
    def val(name: str, default: str) -> str:
        sym = kconf.syms.get(name)
        return sym.str_value if sym is not None and sym.str_value else default

    lines = [
        "/* Generated by tools/build/kconfig-gen.py; do not edit. */",
        "#ifndef FREEWIFICAM_LWIPOPTS_KCONFIG_H",
        "#define FREEWIFICAM_LWIPOPTS_KCONFIG_H",
        "",
        "#define LWIP_TCPIP_CORE_LOCKING_INPUT %s" % val("LWIP_TCPIP_CORE_LOCKING_INPUT", "0"),
        "#define LWIP_TCP_ACK_DELAY_SEGMENTS %s" % val("LWIP_TCP_ACK_DELAY_SEGMENTS", "2"),
        "#define LWIPERF_TCP_MAX_IDLE_SEC %s" % val("LWIPERF_TCP_MAX_IDLE_SEC", "60"),
        "",
        "#endif /* FREEWIFICAM_LWIPOPTS_KCONFIG_H */",
        "",
    ]
    return "\n".join(lines)


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
        out / "include" / "generated" / "csh_config.h": csh_config_text(kconf),
        out / "include" / "generated" / "lwipopts_kconfig.h": lwip_config_text(kconf),
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
