#!/usr/bin/env python3
"""Compare the Make and CMake RTOS builds for one or more profiles.

Acceptance is parity of the build decisions, not of the image bytes:

  * source set          the files each build compiles
  * config symbol set   the CONFIG_* names the generated pair defines
  * defined symbols     nm -g --defined-only of both ELFs
  * undefined symbols   nm -u of both ELFs (the closure gate)
  * no -D               no compile or link command carries a -D token

Image size and sha256 are reported for information only.  The two builds use
different object paths and debug info, so byte equality is not expected and
is not a criterion.

By default the script builds both sides itself for the three representative
profiles:

  tools/build/compare-parity.py
  tools/build/compare-parity.py --profiles rtos-shell --skip-make-build

Usage:
  tools/build/compare-parity.py [--profiles P ...] [--skip-make-build]
                                [--skip-cmake-build] [--json OUT]
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MAKE_BUILD = REPO / "tests" / "board" / "rtos" / "build" / "build"
CMAKE_ROOT = REPO / "build" / "cmake"
QUERY = REPO / "tools" / "build" / "cmake-query.py"
KCONFIG_GEN = REPO / "tools" / "build" / "kconfig-gen.py"
EVIDENCE = REPO / "build" / "parity"

DEFAULT_PROFILES = [
    "net-netio-raw-tx256",
    "rtl8xxxu-8723bu-netio-ar-ht20-ba",
    "rtos-shell",
]

CONFIG_MAKE_RE = re.compile(r"^(CONFIG_[A-Za-z0-9_]+)\s*[:?+]?=")
CONFIG_HDR_RE = re.compile(
    r"^(?:#define\s+(CONFIG_[A-Za-z0-9_]+)|/\*\s+(CONFIG_[A-Za-z0-9_]+) is not set\s*\*/)")


class Fail(Exception):
    pass


def run(cmd: list[str], cwd: Path, log: Path) -> str:
    env = dict(os.environ)
    proc = subprocess.run(cmd, cwd=cwd, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text(proc.stdout)
    if proc.returncode != 0:
        tail = "\n".join(proc.stdout.strip().splitlines()[-20:])
        raise Fail(f"{' '.join(cmd)} failed (rc={proc.returncode}):\n{tail}")
    return proc.stdout


def tool(name: str, compiler: str) -> str:
    candidate = Path(compiler).with_name(f"aarch64-none-elf-{name}")
    if candidate.is_file():
        return str(candidate)
    found = shutil.which(f"aarch64-none-elf-{name}")
    if found:
        return found
    raise Fail(f"cannot find aarch64-none-elf-{name}")


def parse_config_make(path: Path) -> set[str]:
    names = set()
    for line in path.read_text().splitlines():
        m = CONFIG_MAKE_RE.match(line)
        if m:
            names.add(m.group(1))
    return names


def parse_config_header(path: Path) -> set[str]:
    names = set()
    for line in path.read_text().splitlines():
        m = CONFIG_HDR_RE.match(line)
        if m:
            names.add(m.group(1) or m.group(2))
    return names


def nm_names(nm: str, elf: Path, defined: bool, log: Path) -> set[str]:
    args = [nm, "-g", "--defined-only", str(elf)] if defined else [nm, "-u", str(elf)]
    proc = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT)
    log.write_text(proc.stdout)
    if proc.returncode != 0:
        raise Fail(f"nm failed on {elf}:\n{proc.stdout}")
    names = set()
    for line in proc.stdout.splitlines():
        fields = line.split()
        if not fields:
            continue
        if defined:
            if len(fields) >= 3:
                names.add(fields[2])
        else:
            names.add(fields[-1].lstrip("U "))
    return names


def compile_commands_sources(binary_dir: Path) -> tuple[list[str], list[tuple[str, list[str]]]]:
    cc = binary_dir / "compile_commands.json"
    if not cc.is_file():
        raise Fail(f"missing {cc}; configure the CMake preset first")
    entries = json.loads(cc.read_text())
    sources, commands = [], []
    for entry in entries:
        argv = entry.get("arguments")
        if argv is None:
            argv = shlex.split(entry["command"])
        if "-c" not in argv:
            continue
        src = argv[argv.index("-c") + 1]
        if not Path(src).is_absolute():
            src = str(Path(entry["directory"]) / src)
        sources.append(str(Path(src).resolve()))
        commands.append((entry["file"], argv))
    return sources, commands


def defines_in(argv: list[str]) -> list[str]:
    return [tok for tok in argv if tok.startswith("-D") and len(tok) > 2]


def make_query(profile: str, log: Path) -> dict:
    out = EVIDENCE / profile / "make-build.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    run([sys.executable, str(QUERY), "--profile", profile,
         "--build-tag", "parity", "--out", str(out)], REPO, log)
    return json.loads(out.read_text())


def build_make(profile: str) -> None:
    # Regenerate first: FW_BUILD_TAG is baked into the header at generation
    # time, and comparing a stale-tag make image with a fresh CMake one would
    # report an image difference that is not a build divergence.
    run(["./make.rtos", "config", f"PROFILE={profile}"], REPO,
        EVIDENCE / profile / "make-config.log")
    run(["./make.rtos", "image", f"PROFILE={profile}"], REPO,
        EVIDENCE / profile / "make-build.log")


def build_cmake(profile: str) -> None:
    run(["cmake", "--preset", profile], REPO, EVIDENCE / profile / "cmake-configure.log")
    run(["cmake", "--build", str(CMAKE_ROOT / profile)], REPO,
        EVIDENCE / profile / "cmake-build.log")


def fresh_config(profile: str) -> tuple[set[str], set[str], Path]:
    out = EVIDENCE / profile / "kconfig-fresh"
    if out.exists():
        shutil.rmtree(out)
    run([sys.executable, str(KCONFIG_GEN), "--profile", profile, "--out", str(out)],
        REPO, EVIDENCE / profile / "kconfig-fresh.log")
    return (parse_config_make(out / "freewificam_config"),
            parse_config_header(out / "freewificam_config.h"), out)


def cert(profile: str, opts: argparse.Namespace) -> dict:
    pdir = EVIDENCE / profile
    pdir.mkdir(parents=True, exist_ok=True)
    if not opts.skip_make_build:
        build_make(profile)
    query = make_query(profile, pdir / "query.log")
    if not opts.skip_cmake_build:
        build_cmake(profile)
    make_bin = Path(query["bin"])
    make_elf = Path(query["elf"])
    if not make_elf.is_file():
        raise Fail(f"make image missing: {make_elf}")
    cmake_dir = CMAKE_ROOT / profile
    cmake_query = cmake_dir / "build-description.json"
    if not cmake_query.is_file():
        raise Fail(f"missing {cmake_query}; configure the CMake preset first")
    cq = json.loads(cmake_query.read_text())
    cmake_elf = cmake_dir / f"rtos-{cq['backend']}.elf"
    cmake_bin = cmake_dir / f"rtos-{cq['backend']}.bin"
    if not cmake_elf.is_file():
        raise Fail(f"CMake image missing: {cmake_elf}")

    make_sources = [str(Path(s["source"]).resolve()) for s in query["sources"]]
    cmake_sources, cmake_commands = compile_commands_sources(cmake_dir)

    nm = tool("nm", query["compiler"])
    make_defined = nm_names(nm, make_elf, True, pdir / "make-nm.log")
    cmake_defined = nm_names(nm, cmake_elf, True, pdir / "cmake-nm.log")
    make_undef = nm_names(nm, make_elf, False, pdir / "make-nm-u.log")
    cmake_undef = nm_names(nm, cmake_elf, False, pdir / "cmake-nm-u.log")

    fresh_make, fresh_hdr, fresh_dir = fresh_config(profile)
    canonical = REPO / "build" / "kconfig" / profile
    make_cfg = parse_config_make(canonical / "freewificam_config")
    cmake_cfg = parse_config_header(canonical / "freewificam_config.h")
    make_cfg_hdr = parse_config_header(canonical / "freewificam_config.h")

    build_defs = str(REPO / "tests" / "board" / "rtos" / "platform" / "build_defs.h")
    make_flags = [f for s in query["sources"] for f in s["flags"]]
    cmake_argv = [a for _, argv in cmake_commands for a in argv]
    make_has_defs = build_defs in make_flags
    cmake_has_defs = build_defs in cmake_argv
    kcfg = str(canonical)
    make_has_kcfg = any(f == f"-I{kcfg}" or f.startswith(f"-I{kcfg}/") for f in make_flags)
    cmake_has_kcfg = any(f == f"-I{kcfg}" or f.startswith(f"-I{kcfg}/") for f in cmake_argv)

    make_defines = query["defines"]
    cmake_defines = [d for argv in [argv for _, argv in cmake_commands]
                     for d in defines_in(argv)]
    link_defines = []
    try:
        link_cmd = run(["ninja", "-C", str(cmake_dir), "-t", "commands",
                        f"rtos-{cq['backend']}.elf"], REPO, pdir / "ninja-link.log")
        elf_name = f"rtos-{cq['backend']}.elf"
        # Ninja prints the link, the objcopy and the gates as one shell line
        # joined with &&; the link is the segment with -o <elf> and no -c.
        link_segments = []
        for line in link_cmd.splitlines():
            for segment in line.split("&&"):
                if elf_name in segment and " -o " in segment and " -c " not in segment:
                    link_segments.append(segment.strip())
        if not link_segments:
            raise Fail("no link command found in the Ninja command list")
        for segment in link_segments:
            try:
                argv = shlex.split(segment)
            except ValueError:
                continue
            link_defines += defines_in(argv)
    except Fail as e:
        raise Fail(f"cannot read the CMake link command: {e}")

    source_diff = sorted(set(make_sources) ^ set(cmake_sources))
    config_diff = sorted((fresh_make ^ make_cfg) | (fresh_hdr ^ make_cfg_hdr)
                         | (fresh_make ^ cmake_cfg))
    defined_missing = sorted(make_defined - cmake_defined)
    defined_extra = sorted(cmake_defined - make_defined)
    undefined_diff = sorted(make_undef ^ cmake_undef)

    checks = {
        "source sets equal": not source_diff,
        "config symbol sets equal": not config_diff,
        "defined symbols equal": not defined_missing and not defined_extra,
        "undefined symbols equal": not undefined_diff,
        "make force-includes build_defs.h": make_has_defs,
        "cmake force-includes build_defs.h": cmake_has_defs,
        "make uses generated config dir": make_has_kcfg,
        "cmake uses generated config dir": cmake_has_kcfg,
        "make has no -D": not make_defines,
        "cmake compile has no -D": not cmake_defines,
        "cmake link has no -D": not link_defines,
    }
    sha = lambda p: subprocess.run(["sha256sum", str(p)], text=True,
                                   stdout=subprocess.PIPE).stdout.split()[0]
    result = {
        "profile": profile,
        "checks": checks,
        "pass": all(checks.values()),
        "make": {
            "sources": len(make_sources),
            "sync_count": len(make_sources) - len(set(make_sources)),
            "defined": len(make_defined),
            "undefined": len(make_undef),
            "elf": str(make_elf),
            "bin": str(make_bin),
            "bin_size": make_bin.stat().st_size if make_bin.is_file() else None,
            "bin_sha256": sha(make_bin) if make_bin.is_file() else None,
        },
        "cmake": {
            "sources": len(cmake_sources),
            "sync_count": len(cmake_sources) - len(set(cmake_sources)),
            "defined": len(cmake_defined),
            "undefined": len(cmake_undef),
            "elf": str(cmake_elf),
            "bin": str(cmake_bin),
            "bin_size": cmake_bin.stat().st_size if cmake_bin.is_file() else None,
            "bin_sha256": sha(cmake_bin) if cmake_bin.is_file() else None,
        },
        "source_diff": source_diff,
        "config_diff": config_diff,
        "defined_missing_in_cmake": defined_missing,
        "defined_extra_in_cmake": defined_extra,
        "undefined_diff": undefined_diff,
        "make_defines": make_defines,
        "cmake_compile_defines": cmake_defines,
        "cmake_link_defines": link_defines,
        "fresh_config_dir": str(fresh_dir),
    }
    (pdir / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def report(results: list[dict]) -> None:
    print(f"{'profile':44s} {'sources':>7s} {'defined':>7s} {'undef':>5s} "
          f"{'bin bytes':>9s}  verdict")
    for r in results:
        m, c = r["make"], r["cmake"]
        print(f"{r['profile']:44s} {str(m['sources']) + '/' + str(c['sources']):>7s} "
              f"{str(m['defined']) + '/' + str(c['defined']):>7s} "
              f"{str(m['undefined']) + '/' + str(c['undefined']):>5s} "
              f"{str(m['bin_size']) + '/' + str(c['bin_size']):>9s}  "
              f"{'PASS' if r['pass'] else 'FAIL'}")
        if not r["pass"]:
            for name, ok in r["checks"].items():
                if not ok:
                    print(f"    failed: {name}")
            for key in ("source_diff", "config_diff", "defined_missing_in_cmake",
                        "defined_extra_in_cmake", "undefined_diff", "make_defines",
                        "cmake_compile_defines", "cmake_link_defines"):
                if r[key]:
                    items = r[key]
                    shown = ", ".join(str(x) for x in items[:8])
                    more = "" if len(items) <= 8 else f" (+{len(items) - 8})"
                    print(f"    {key}: {shown}{more}")
        if r["make"]["bin_sha256"] != r["cmake"]["bin_sha256"]:
            print("    note: images differ byte-for-byte (informational only; "
                  "build tags/debug paths differ)")
    failed = [r["profile"] for r in results if not r["pass"]]
    print(f"\nparity: {len(results) - len(failed)}/{len(results)} passed"
          + (f"; failed: {', '.join(failed)}" if failed else ""))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--profiles", nargs="+", default=DEFAULT_PROFILES)
    ap.add_argument("--skip-make-build", action="store_true",
                    help="reuse the existing make ELF (single-profile runs only; "
                         "the make build directory holds one image at a time)")
    ap.add_argument("--skip-cmake-build", action="store_true",
                    help="reuse the existing CMake build directory")
    ap.add_argument("--json", help="write the machine-readable result here")
    args = ap.parse_args()
    results = []
    for profile in args.profiles:
        try:
            results.append(cert(profile, args))
        except Fail as e:
            print(f"{profile}: ERROR: {e}", file=sys.stderr)
            return 2
    report(results)
    if args.json:
        Path(args.json).write_text(json.dumps(results, indent=2) + "\n")
    return 0 if all(r["pass"] for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
