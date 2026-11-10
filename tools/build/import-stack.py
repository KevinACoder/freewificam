#!/usr/bin/env python3
"""Import the Linux wireless stack sources into liblinux80211, or verify them.

The stack is carried verbatim: the translation units the upstream Makefiles
build unconditionally, plus every internal header of the two directories.  The
import is selective on purpose - mesh/debugfs/led/pm/wext/of are compile-time
options this line keeps off, so their TUs never enter the tree
(docs/HANDOFF-S0-HOST.md §2.1).

  tools/build/import-stack.py            copy and write stack/IMPORT.tsv
  tools/build/import-stack.py --check    re-hash the tree against upstream
  tools/build/import-stack.py --emit-sources-block
                                         print the component sources.mk block

The block is printed, not written: the component owns one sources.mk
(components/README.md), and a generated file would be the second spelling of
the TU list.  --check compares the block's file set with the Makefiles.
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
DEST_ROOT = REPO / "components" / "liblinux80211" / "stack"
SOURCES_MK = REPO / "components" / "liblinux80211" / "sources.mk"

MAC80211_EXTRA = ("wbrf.c", "rc80211_minstrel_ht.c")


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
                return line.split("SPDX-License-Identifier:")[1].strip()
    return "unknown"


def upstream_mac80211_tus(upstream: Path) -> list[str]:
    text = (upstream / "net/mac80211/Makefile").read_text()
    cut = text.index("mac80211-$(CONFIG_MAC80211_LEDS)")
    names = re.findall(r"([a-z0-9_-]+)\.o", text[:cut])
    names = [n for n in names if n != "mac80211"]
    tus = [f"net/mac80211/{n}.c" for n in names]
    tus += [f"net/mac80211/{n}" for n in MAC80211_EXTRA]
    if len(tus) != 44:
        raise SystemExit(f"mac80211 TU set is {len(tus)}, want 44: {tus}")
    return tus


def upstream_cfg80211_tus(upstream: Path) -> list[str]:
    text = (upstream / "net/wireless/Makefile").read_text()
    block = text[text.index("cfg80211-y += core"):text.index("cfg80211-$(CONFIG_OF)")]
    names = [n for n in re.findall(r"([a-z0-9_-]+)\.o", block) if n != "cfg80211"]
    tus = [f"net/wireless/{n}.c" for n in names]
    if len(tus) != 18:
        raise SystemExit(f"cfg80211 TU set is {len(tus)}, want 18: {tus}")
    return tus


def headers(upstream: Path) -> list[str]:
    out: list[str] = []
    for sub in ("net/mac80211", "net/wireless"):
        out += [str(p.relative_to(upstream)) for p in sorted((upstream / sub).glob("*.h"))]
    return out


def sources_block(tus: list[str]) -> str:
    lines = ["ifeq ($(SRC_TAG),stack)", "obj-$(CONFIG_LIBLINUX80211) += \\"]
    for i, tu in enumerate(tus):
        tail = " \\" if i + 1 < len(tus) else ""
        lines.append(f"\tstack/{tu[len('net/'):]}{tail}")
    lines.append("endif")
    return "\n".join(lines) + "\n"


def manifest_rows(upstream: Path, tus: list[str], hdrs: list[str]) -> list[str]:
    commit = subprocess.run(
        ["git", "-C", str(upstream), "rev-parse", "HEAD"],
        text=True, capture_output=True, check=True,
    ).stdout.strip()
    rows = [
        "# Imported stack files: source commit, source path, destination, sha256, lines, kind, license.",
        "# Written by tools/build/import-stack.py; --check re-derives it.",
    ]
    for kind, files in (("tu", tus), ("header", hdrs)):
        for rel in files:
            src = upstream / rel
            dst = DEST_ROOT / rel[len("net/"):]
            rows.append(
                f"{commit}\t{rel}\t{dst.relative_to(REPO)}\t"
                f"{sha256(src)}\t{len(src.read_bytes().splitlines())}\t{kind}\t{spdx(src)}"
            )
    return rows


def do_import(upstream: Path) -> int:
    tus = upstream_mac80211_tus(upstream) + upstream_cfg80211_tus(upstream)
    hdrs = headers(upstream)
    if not upstream.joinpath("net/mac80211/Makefile").exists():
        raise SystemExit(f"no net/mac80211 under {upstream}; pass --upstream")
    for sub in ("mac80211", "wireless"):
        (DEST_ROOT / sub).mkdir(parents=True, exist_ok=True)
    for rel in tus + hdrs:
        src = upstream / rel
        dst = DEST_ROOT / rel[len("net/"):]
        dst.parent.mkdir(parents=True, exist_ok=True)
        if not dst.exists() or sha256(dst) != sha256(src):
            shutil.copyfile(src, dst)
    (DEST_ROOT / "IMPORT.tsv").write_text("\n".join(manifest_rows(upstream, tus, hdrs)) + "\n")
    print(f"imported {len(tus)} TUs + {len(hdrs)} headers -> {DEST_ROOT}")
    print(f"manifest: {DEST_ROOT / 'IMPORT.tsv'}")
    return 0


def do_check(upstream: Path) -> int:
    tus = upstream_mac80211_tus(upstream) + upstream_cfg80211_tus(upstream)
    hdrs = headers(upstream)
    bad = 0
    for rel in tus + hdrs:
        src = upstream / rel
        dst = DEST_ROOT / rel[len("net/"):]
        if not dst.exists():
            print(f"MISSING {dst.relative_to(REPO)}", file=sys.stderr)
            bad += 1
        elif sha256(dst) != sha256(src):
            print(f"DIFFERS {dst.relative_to(REPO)} (upstream changed?)", file=sys.stderr)
            bad += 1
    text = SOURCES_MK.read_text()
    carried = set(re.findall(r"stack/([a-z0-9_/-]+\.c)", text))
    want = {rel[len("net/"):] for rel in tus}
    for missing in sorted(want - carried):
        print(f"sources.mk lacks stack/{missing}", file=sys.stderr)
        bad += 1
    for extra in sorted(carried - want):
        print(f"sources.mk carries unknown stack/{extra}", file=sys.stderr)
        bad += 1
    if bad:
        print(f"check: FAIL ({bad} problem(s))", file=sys.stderr)
        return 1
    commit = subprocess.run(["git", "-C", str(upstream), "rev-parse", "--short", "HEAD"],
                            text=True, capture_output=True).stdout.strip()
    print(f"check: ok ({len(tus)} TUs + {len(hdrs)} headers byte-identical to {commit})")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--upstream", type=Path,
                    default=REPO.parent / "linux-upstream")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--emit-sources-block", action="store_true")
    args = ap.parse_args()
    upstream = args.upstream.resolve()
    if args.emit_sources_block:
        tus = upstream_mac80211_tus(upstream) + upstream_cfg80211_tus(upstream)
        sys.stdout.write(sources_block(tus))
        return 0
    if args.check:
        return do_check(upstream)
    return do_import(upstream)


if __name__ == "__main__":
    raise SystemExit(main())
