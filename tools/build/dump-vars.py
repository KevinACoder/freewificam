#!/usr/bin/env python3
"""Dump the effective make variables of one RTOS build profile.

The build's contract is the compile command and the source list, not the image
hash: ``-g3`` and ``__LINE__`` make a hash a weak judge.  This tool reads the
make database with ``-pn`` (no recipe runs) and writes the variables a
migration must preserve:

  * the flag strings (``BASE_CFLAGS``, ``CFLAGS``, every ``*_CFLAGS``),
  * the link flags (``LDFLAGS``),
  * every source list (``*_SRCS``).

The repository root is rewritten to ``<ROOT>`` so a baseline captured before a
build-system change compares with a dump taken after it.  Values are kept as
single strings: the comparator tokenizes and diffs them, this tool only reads
what make resolved.

Usage:
    tools/build/dump-vars.py --profile rtl8xxxu-8723bu-bringup
    tools/build/dump-vars.py --all --out /tmp/baseline.json
    tools/build/dump-vars.py --all --build-tag baseline --jobs 8

Exit status is non-zero when make itself fails (a missing profile, an error in
the makefile), not when a variable is empty: an empty variable is a fact the
comparator should see.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BUILD = REPO / "tests" / "board" / "rtos" / "build"
CONFIGS = REPO / "tests" / "board" / "rtos" / "configs"

# make -p prints "NAME := value" (simple), "NAME = value" (recursive) and
# "NAME ?= value" (a definition that lost to an earlier one; the resolved
# value is what matters).  Long source lists stay on one line.
VAR_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*) (?::=|\?=|=) (.*)$")

# Variables that are not "*_CFLAGS" but are part of the compile contract.
EXTRA_FLAGS = {"BASE_CFLAGS", "LDFLAGS"}
# Flag strings that describe the host tier only; they are dumped anyway, but
# this keeps the intent visible if a future filter is needed.
HOST_PREFIX = "HOST_"


def parse_make_db(text: str) -> dict[str, str]:
    """Return the last definition of every make variable in the -p output."""
    out: dict[str, str] = {}
    for line in text.splitlines():
        m = VAR_RE.match(line)
        if m:
            out[m.group(1)] = m.group(2)
    return out


def wanted(name: str) -> bool:
    if name in EXTRA_FLAGS:
        return True
    # The link contract is split over the common LDFLAGS and the per-backend
    # BACKEND_LDFLAGS (the heap size rides on --defsym=HEAP_SIZE=...), so a
    # comparator that only reads LDFLAGS cannot see a heap regression.
    if name.endswith("_CFLAGS") or name.endswith("_SRCS") or name.endswith("_LDFLAGS"):
        return True
    return False


def dump_one(profile: str, build_tag: str, make: str) -> dict:
    if not ((CONFIGS / f"{profile}.defconfig").exists()
            or (CONFIGS / f"{profile}.mk").exists()):
        raise SystemExit(f"dump-vars: no profile configs/{profile}.defconfig or .mk")
    cmd = [
        make,
        "-C",
        str(BUILD),
        "-pn",
        f"CONFIG={profile}",
        f"BUILD_TAG={build_tag}",
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, env=dict(os.environ))
    if proc.returncode != 0:
        tail = "\n".join(proc.stderr.strip().splitlines()[-20:])
        raise SystemExit(f"dump-vars: make -pn failed for {profile}:\n{tail}")
    db = parse_make_db(proc.stdout)
    root = str(REPO)
    vars_out: dict[str, str] = {}
    for name in sorted(db):
        if not wanted(name):
            continue
        value = db[name].replace(root, "<ROOT>")
        vars_out[name] = value
    return {
        "schema": 1,
        "profile": profile,
        "build_tag": build_tag,
        "make": make,
        "vars": vars_out,
        "make_stderr_tail": "\n".join(proc.stderr.strip().splitlines()[-5:]),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--profile", action="append", default=[], help="profile name (repeatable)")
    ap.add_argument("--all", action="store_true",
                    help="every configs/*.defconfig (and any legacy *.mk) profile")
    ap.add_argument("--build-tag", default="baseline", help="BUILD_TAG used for the dump")
    ap.add_argument("--make", default="make", help="make executable")
    ap.add_argument("--out", help="write JSON here instead of stdout")
    args = ap.parse_args()

    profiles = list(args.profile)
    if args.all:
        profiles = sorted({p.stem for p in CONFIGS.glob("*.defconfig")}
                          | {p.stem for p in CONFIGS.glob("*.mk")})
    if not profiles:
        ap.error("pass --profile or --all")

    result = {
        "schema": 1,
        "repo": str(REPO),
        "build_tag": args.build_tag,
        "profiles": {},
    }
    for profile in profiles:
        result["profiles"][profile] = dump_one(profile, args.build_tag, args.make)

    text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.out:
        Path(args.out).write_text(text)
        print(f"dump-vars: {len(profiles)} profile(s) -> {args.out}", file=sys.stderr)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
