#!/usr/bin/env python3
"""Public-language gate: reject internal stage labels in first-party files.

`freewificam` is the public side of the project. Its first-party sources,
profiles, tools and docs use descriptive stage names; the internal stage
numbers from the private ledger are not part of the public vocabulary. The
8189FTV case/profile/tool paths must be clean. A small baseline records the
remaining historical occurrences in unrelated lines; the gate fails on any
new occurrence or any increase. The baseline is debt, not permission.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SCAN = ("README.md", "docs", "components", "tests/board/rtos", "tools")
SKIP_PARTS = {"third-party", "wireless-drivers", "linux-upstream", "build",
              "__pycache__", ".git"}
SKIP_DIRS = (
    "components/liblinux80211/stack",
    "components/liblinux80211/fw",
    "components/liblinux80211/include/linux",
    "components/liblinux80211/include/net",
    "components/liblinux80211/include/uapi",
)
BINARY_SUFFIXES = {".bin", ".ucode", ".gz", ".xz", ".zip", ".png", ".jpg",
                   ".jpeg", ".pdf", ".o", ".elf", ".a", ".so", ".pyc"}
SKIP_FILES = {"tools/build/check-public-language.py",
              "tools/build/public-language-baseline.json"}
# Case-insensitive stage labels. The lookarounds keep identifiers such as
# `rtl8189fs_throughput` from being read as a label.
PATTERN = re.compile(
    r"(?i)(?<![0-9A-Za-z_:])(?:C[0-6]|C6T|A[1-4]|W[0-6]B?)(?![0-9A-Za-z_:])")
DEDICATED = (
    "tests/board/rtos/app/cases/rtl8189fs_",
    "tests/board/rtos/configs/rk3568-rtl8189fs-",
    "tests/board/rtos/configs/stm32f429-rtl8189fs-",
)
BASELINE = REPO / "tools/build/public-language-baseline.json"


def public_files():
    for rel in SCAN:
        base = REPO / rel
        if base.is_file():
            yield base
        elif base.is_dir():
            for path in base.rglob("*"):
                if not path.is_file():
                    continue
                if set(path.parts) & SKIP_PARTS:
                    continue
                rel = path.relative_to(REPO).as_posix()
                if rel in SKIP_FILES:
                    continue
                if path.suffix.lower() in BINARY_SUFFIXES:
                    continue
                if any(rel.startswith(d + "/") for d in SKIP_DIRS):
                    continue
                try:
                    if b"\0" in path.read_bytes():
                        continue
                except OSError:
                    continue
                yield path


def scan(path: Path) -> tuple[int, list[str]]:
    try:
        text = path.read_text(errors="ignore")
    except OSError:
        return 0, []
    hits = PATTERN.findall(text)
    return len(hits), hits


def load_baseline() -> dict[str, int]:
    if not BASELINE.is_file():
        return {}
    data = json.loads(BASELINE.read_text())
    return {str(k): int(v) for k, v in data.get("files", {}).items()}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--update-baseline", action="store_true",
                    help="rewrite the baseline from the current tree (only "
                         "after a cleanup; never to hide a new hit)")
    args = ap.parse_args()

    counts: dict[str, int] = {}
    dedicated: list[tuple[str, int, list[str]]] = []
    for path in public_files():
        rel = path.relative_to(REPO).as_posix()
        n, hits = scan(path)
        if n == 0:
            continue
        counts[rel] = n
        if any(rel.startswith(prefix) for prefix in DEDICATED):
            dedicated.append((rel, n, hits))

    if args.update_baseline:
        BASELINE.write_text(json.dumps(
            {"note": "Debt ledger for historical non-8189FTV occurrences; "
                     "new hits are failures and are not added here.",
             "files": dict(sorted(counts.items()))}, indent=2) + "\n")
        print(f"public-language: baseline updated: {len(counts)} files, "
              f"{sum(counts.values())} occurrences")
        return 0

    baseline = load_baseline()
    new: list[str] = []
    grew: list[str] = []
    for rel, n in sorted(counts.items()):
        if rel not in baseline:
            new.append(f"{rel}:{n}")
        elif n > baseline[rel]:
            grew.append(f"{rel}:{n}>{baseline[rel]}")
    for rel, n, hits in dedicated:
        new.append(f"{rel}:{n} (8189FTV path: {', '.join(hits)})")

    debt = sum(baseline.values())
    print(f"public-language: 8189FTV paths clean"
          if not dedicated else "public-language: 8189FTV path has labels")
    print(f"public-language: historical debt {debt} occurrence(s) in "
          f"{len(baseline)} file(s)")
    if new or grew:
        print("public-language: FAIL: new or increased labels", file=sys.stderr)
        for row in new + grew:
            print("  " + row, file=sys.stderr)
        return 1
    print("public-language: PASS: no new labels")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
