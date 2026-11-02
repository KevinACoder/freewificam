#!/usr/bin/env python3
"""Compare the effective make variables of two dump-vars.py snapshots.

A1 of the BUILD-01 migration is not "the image hash is the same" (``-g3`` and
``__LINE__`` make that a weak judge); it is "the compiler sees the same
contract".  For every profile and every dumped variable this tool checks:

  * the effective ``-D`` map: name -> value, last definition wins (so a
    duplicate that the old build emitted twice and the new build emits once
    compares equal when the value agrees),
  * the sequence of non-``-D`` tokens (include paths, warning flags, link
    flags) - the order matters,
  * source lists (``*_SRCS``) byte for byte and in order.

Only one textual difference is expected: ``net-netio-raw-tx256-prof`` used to
carry ``-DCONFIG_GMAC_PROF=1`` both in BASE_CFLAGS and again in CONFIG_LEVERS;
the effective map is unchanged, so the map comparison accepts it.  Every other
difference is a migration defect.

Usage:
    tools/build/compare-flags.py --baseline BASE.json[.gz] --new AFTER.json
    tools/build/compare-flags.py --baseline ... --new ... --quiet
"""

from __future__ import annotations

import argparse
import gzip
import json
import re
import shlex
import sys
from pathlib import Path

D_RE = re.compile(r"^-D([^=]+)(?:=(.*))?$")


def load(path: str) -> dict:
    raw = Path(path).read_bytes()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    return json.loads(raw)


def split_tokens(text: str) -> list[str]:
    """Split a flag string the way the shell/make would see it.

    Quoted definitions like ``-DRTOS_KERNEL_NAME='"FreeRTOS-Kernel V11.3.1"'``
    contain spaces; a plain ``.split()`` would tear them apart.  ``shlex``
    normalises the quoting on both sides, which is what the comparison wants.
    """
    try:
        return shlex.split(text)
    except ValueError:
        return text.split()


def dmap(tokens: list[str]) -> dict[str, str]:
    out: dict[str, str] = {}
    for tok in tokens:
        m = D_RE.match(tok)
        if not m:
            continue
        name, value = m.group(1), m.group(2)
        out[name] = "1" if value is None else value
    return out


def nond(tokens: list[str]) -> list[str]:
    return [tok for tok in tokens if not tok.startswith("-D")]


def first_diff(a, b) -> str:
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return f"token {i}: {x!r} != {y!r}"
    if len(a) != len(b):
        return f"length {len(a)} != {len(b)} (first extra: {(a[len(b):] or b[len(a):])!r})"
    return ""


def compare_var(name: str, before: str, after: str) -> list[str]:
    diffs: list[str] = []
    tb, ta = split_tokens(before), split_tokens(after)
    db, da = dmap(tb), dmap(ta)
    if db != da:
        only_b = sorted(set(db) - set(da))
        only_a = sorted(set(da) - set(db))
        changed = sorted(k for k in set(db) & set(da) if db[k] != da[k])
        if only_b:
            diffs.append(f"  {name}: -D only before: {only_b}")
        if only_a:
            diffs.append(f"  {name}: -D only after: {only_a}")
        for k in changed:
            diffs.append(f"  {name}: -D {k}: {db[k]!r} -> {da[k]!r}")
    nb, na = nond(tb), nond(ta)
    if nb != na:
        diffs.append(f"  {name}: non-D sequence differs: {first_diff(nb, na)}")
    return diffs


def compare_profile(before: dict, after: dict) -> list[str]:
    diffs: list[str] = []
    vb, va = before.get("vars", {}), after.get("vars", {})
    names_b, names_a = set(vb), set(va)
    if names_b != names_a:
        only_b, only_a = sorted(names_b - names_a), sorted(names_a - names_b)
        if only_b:
            diffs.append(f"  variables only before: {only_b}")
        if only_a:
            diffs.append(f"  variables only after: {only_a}")
    for name in sorted(names_b & names_a):
        if name.endswith("_SRCS"):
            if vb[name] != va[name]:
                diffs.append(f"  {name}: source list differs")
                # Find the first differing entry, not the whole list.
                a = vb[name].split()
                b = va[name].split()
                diffs.append("    " + first_diff(a, b))
            continue
        diffs.extend(compare_var(name, vb[name], va[name]))
    return diffs


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--baseline", required=True, help="dump-vars.py JSON (plain or .gz)")
    ap.add_argument("--new", required=True, help="dump-vars.py JSON")
    ap.add_argument("--quiet", action="store_true", help="only failures")
    args = ap.parse_args()

    base = load(args.baseline)
    new = load(args.new)
    pb, pn = base.get("profiles", {}), new.get("profiles", {})
    missing = sorted(set(pb) - set(pn))
    extra = sorted(set(pn) - set(pb))
    bad = 0

    if base.get("build_tag") != new.get("build_tag"):
        print(f"compare: build tag differs: {base.get('build_tag')!r} != {new.get('build_tag')!r}")
        bad += 1
    if missing:
        print(f"compare: profiles missing after: {missing}")
        bad += 1
    if extra:
        print(f"compare: profiles only after: {extra}")
        bad += 1

    for profile in sorted(set(pb) & set(pn)):
        diffs = compare_profile(pb[profile], pn[profile])
        if diffs:
            bad += 1
            print(f"DIFF {profile}")
            print("\n".join(diffs))
        elif not args.quiet:
            print(f"ok   {profile}")

    if bad:
        print(f"compare: {bad} profile(s) differ")
        return 1
    print(f"compare: {len(set(pb) & set(pn))} profiles equivalent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
