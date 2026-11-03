#!/usr/bin/env python3
"""Write one manifest per static library produced by `make.rtos libs`.

The manifest is deliberately mechanical: it records the archive, its members,
its undefined symbols and its size.  The library discipline checks in M5 build
on this file instead of on hand-maintained prose.
"""
from __future__ import annotations
import argparse
import json
import subprocess
from pathlib import Path

def run(cmd):
    p = subprocess.run(cmd, text=True, capture_output=True)
    return p.stdout if p.returncode == 0 else ""

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--profile", required=True)
    ap.add_argument("--nm", default="aarch64-none-elf-nm")
    ap.add_argument("--ar", default="aarch64-none-elf-ar")
    args = ap.parse_args()
    libdir = Path(args.dir)
    libs = []
    for archive in sorted(libdir.glob("*.a")):
        members = [l for l in run([args.ar, "t", str(archive)]).splitlines() if l]
        undefined = sorted({
            parts[-1]
            for parts in (line.split() for line in run([args.nm, "-u", str(archive)]).splitlines())
            if len(parts) >= 2 and parts[0] == "U"
        })
        libs.append({
            "name": archive.stem,
            "archive": archive.name,
            "size": archive.stat().st_size,
            "members": len(members),
            "undefined": undefined,
        })
    out = libdir / "manifest.json"
    out.write_text(json.dumps({
        "schema": 1,
        "profile": args.profile,
        "libraries": libs,
    }, indent=2, sort_keys=True) + "\n")
    print(f"manifest: {out} ({len(libs)} libraries)")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
