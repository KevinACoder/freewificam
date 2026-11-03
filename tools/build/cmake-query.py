#!/usr/bin/env python3
"""Ask the platform Makefile what one profile would compile and link.

This keeps the platform Makefile as the single owner of the source sets, the
per-driver flag worlds and the link.  This script runs the Makefile in dry-run
mode (``make -Bn image``), so every command it reads is the command make itself
would run, fully expanded -- no second copy of the source lists exists.

The output is the build description CMake consumes:

  {
    "profile": "...", "repo": "...", "backend": "freertos",
    "compiler": "...", "elf": "...", "bin": "...", "map": "...",
    "sources": [{"source": abs, "object": abs, "language": "C"|"ASM",
                 "flags": [...]}],
    "link": {"objects": [...], "flags": [...]},
    "objcopy": [...],
    "defines": [...]          # any -D found; must stay empty
  }

The query is read-only: make runs with -n and never writes an object.  The
per-component sources.mk migration will replace the query; until then this is
the one bridge between the two build systems.

Usage:
  tools/build/cmake-query.py --profile <name> --out build/cmake/<p>/build.json
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BUILD_MK = REPO / "tests" / "board" / "rtos" / "build"
OBJ_MARK = "/build/obj/"


class QueryError(Exception):
    pass


def make_command(profile: str, build_tag: str) -> list[str]:
    make = shutil.which("make")
    if make is None:
        raise QueryError("make is not on PATH")
    return [make, "-Bn", "image", f"PROFILE={profile}", f"BUILD_TAG={build_tag}"]


def run_make(profile: str, build_tag: str) -> list[list[str]]:
    env = dict(os.environ)
    proc = subprocess.run(
        make_command(profile, build_tag), cwd=BUILD_MK, env=env,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        tail = "\n".join(proc.stderr.strip().splitlines()[-15:])
        raise QueryError(f"make -Bn image PROFILE={profile} failed:\n{tail}")
    commands = []
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line or "aarch64-none-elf-" not in line:
            continue
        # Skip shell glue lines (the gates target prints several).
        if line.startswith("start=") or "| awk" in line:
            continue
        try:
            argv = shlex.split(line)
        except ValueError:
            continue
        commands.append(argv)
    return commands


def is_compile(argv: list[str]) -> bool:
    return "-c" in argv and any(a.endswith("gcc") for a in argv[:1])


def is_link(argv: list[str]) -> bool:
    return "-c" not in argv and "-o" in argv and any(
        a.endswith("gcc") for a in argv[:1])


def is_objcopy(argv: list[str]) -> bool:
    return bool(argv) and argv[0].endswith("objcopy")


def split_defines(flags: list[str]) -> list[str]:
    return [f for f in flags if f.startswith("-D") and len(f) > 2]


def compile_entry(argv: list[str]) -> dict:
    source = argv[argv.index("-c") + 1]
    obj = argv[argv.index("-o") + 1]
    flags = []
    skip = False
    for i, tok in enumerate(argv[1:], start=1):
        if skip:
            skip = False
            continue
        if tok == "-c":
            skip = True  # the source operand
            continue
        if tok == "-o":
            skip = True  # the object operand
            continue
        if tok in ("-MMD", "-MP"):
            continue
        flags.append(tok)
    lang = "ASM" if source.endswith(".S") else "C"
    if source.endswith(".s"):
        lang = "ASM"
    return {"source": source, "object": obj, "language": lang, "flags": flags}


def link_entry(argv: list[str]) -> dict:
    out = argv[argv.index("-o") + 1]
    objects, flags = [], []
    skip = False
    for i, tok in enumerate(argv[1:], start=1):
        if skip:
            skip = False
            continue
        if tok == "-o":
            skip = True
            continue
        if OBJ_MARK in tok and tok.endswith(".o"):
            objects.append(tok)
            continue
        flags.append(tok)
    return {"output": out, "objects": objects, "flags": flags}


def strip_map(flags: list[str]) -> tuple[list[str], str | None]:
    kept, mapfile = [], None
    for f in flags:
        if f.startswith("-Wl,-Map,"):
            mapfile = f[len("-Wl,-Map,"):]
        else:
            kept.append(f)
    return kept, mapfile


def query(profile: str, build_tag: str) -> dict:
    commands = run_make(profile, build_tag)
    compiles = [compile_entry(a) for a in commands if is_compile(a)]
    links = [link_entry(a) for a in commands if is_link(a)]
    if not compiles:
        raise QueryError("make printed no compile commands")
    if len(links) != 1:
        raise QueryError(f"expected one link command, got {len(links)}")
    link = links[0]
    link_flags, mapfile = strip_map(link["flags"])
    objcopy = [a for a in commands if is_objcopy(a)]
    defines = split_defines(link_flags)
    for entry in compiles:
        defines += split_defines(entry["flags"])
    backend = "unknown"
    if "freertos" in link["output"]:
        backend = "freertos"
    elif "threadx" in link["output"]:
        backend = "threadx"
    return {
        "profile": profile,
        "repo": str(REPO),
        "backend": backend,
        "compiler": commands[0][0],
        "elf": link["output"],
        "bin": link["output"][:-4] + ".bin",
        "map": mapfile,
        "sources": compiles,
        "link": {"objects": link["objects"], "flags": link_flags},
        "objcopy": objcopy[0] if objcopy else [],
        "defines": defines,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--profile", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--build-tag", default="cmake")
    args = ap.parse_args()
    try:
        data = query(args.profile, args.build_tag)
    except QueryError as e:
        print(f"cmake-query: {e}", file=sys.stderr)
        return 1
    if data["defines"]:
        print("cmake-query: the make build carries -D tokens, refusing to copy "
              "them into CMake:\n  " + "\n  ".join(data["defines"]),
              file=sys.stderr)
        return 1
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(data, indent=2) + "\n")
    print(f"cmake-query: {args.profile}: {len(data['sources'])} sources, "
          f"{len(data['link']['objects'])} link objects, "
          f"{len(data['link']['flags'])} link flags -> {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
