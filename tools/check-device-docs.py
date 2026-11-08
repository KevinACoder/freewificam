#!/usr/bin/env python3
"""Check that every source citation in the device handbooks resolves.

A device handbook is only worth reading if each claim says where it came
from.  The convention those documents follow is:

  * a citation is one inline code span of the form ``path/to/file.c:LINE``
    (a range ``file.c:12-34`` is allowed), written as a whole span;
  * a driver path is relative to ``wireless-drivers/iwlwifi/``; a path that
    starts with a repository directory such as ``docs/``, ``firmware/`` or
    ``tests/board/`` is relative to the repository root;
  * a relative Markdown link points at a file that exists.

This script resolves each citation against the tree and each link against
the file system.  ``--dump`` prints the cited line so the wording can be
reviewed against the source; that is the check a human still has to do.

    tools/check-device-docs.py              # check, exit 1 on findings
    tools/check-device-docs.py --dump       # check, then print every citation
    tools/check-device-docs.py --selftest   # the checker's own tests

Exit: 0 clean, 1 findings, 2 usage.
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DOC_ROOT = REPO / "docs" / "wireless_devices"
DRIVER_ROOT = REPO / "wireless-drivers" / "iwlwifi"

# The extensions this repository actually cites.  A span that looks like a
# path but ends in something else (a firmware name without a line number, a
# register expression) is not a citation and is left alone.
EXTENSIONS = (
    "c", "h", "S", "s", "py", "sh", "mk", "md", "txt", "json", "yaml",
    "yml", "conf", "cfg", "dts", "dtsi", "ucode", "bin", "ko", "pl",
)

CITATION = re.compile(
    r"^(?P<path>[A-Za-z0-9_./+-]+\.(?:" + "|".join(EXTENSIONS) + r"))"
    r":(?P<start>[0-9]+)(?:-(?P<end>[0-9]+))?$"
)
SPAN = re.compile(r"`([^`\n]+)`")
LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")


class Finding:
    def __init__(self, doc: Path, line: int, kind: str, detail: str):
        self.doc = doc
        self.line = line
        self.kind = kind
        self.detail = detail

    def __str__(self) -> str:
        rel = self.doc.relative_to(REPO) if self.doc.is_relative_to(REPO) else self.doc
        return f"{rel}:{self.line}: {self.kind}: {self.detail}"


def _resolve(path_text: str) -> tuple[Path | None, str]:
    """Resolve a cited path against the repository root, then the driver."""
    candidates = [REPO / path_text, DRIVER_ROOT / path_text]
    hits = [c for c in candidates if c.is_file()]
    if not hits:
        return None, "not found at the repository root or under wireless-drivers/iwlwifi"
    if len(hits) > 1:
        return hits[0], "ambiguous: the same path exists in both trees"
    return hits[0], ""


def check_file(md: Path) -> tuple[list[Finding], list[tuple[Path, int, int, str]]]:
    """Return the findings of one document and the citations it carries."""
    findings: list[Finding] = []
    citations: list[tuple[Path, int, int, str]] = []
    text = md.read_text(encoding="utf-8")
    for lineno, raw in enumerate(text.splitlines(), start=1):
        for span in SPAN.findall(raw):
            m = CITATION.match(span.strip())
            if not m:
                continue
            target, why = _resolve(m.group("path"))
            if target is None:
                findings.append(Finding(md, lineno, "citation", f"`{span}`: {why}"))
                continue
            if why:
                findings.append(Finding(md, lineno, "citation", f"`{span}`: {why}"))
                continue
            start = int(m.group("start"))
            end = int(m.group("end") or start)
            total = len(target.read_text(encoding="utf-8", errors="replace").splitlines())
            if start < 1 or end < start or end > total:
                findings.append(Finding(
                    md, lineno, "citation",
                    f"`{span}`: line range {start}-{end} outside 1-{total}"))
                continue
            citations.append((md, lineno, start, str(target)))
        for link in LINK.findall(raw):
            if link.startswith(("http://", "https://", "mailto:", "#")):
                continue
            link_path = link.split("#", 1)[0]
            if not link_path:
                continue
            target = (md.parent / link_path).resolve()
            if not target.exists():
                findings.append(Finding(md, lineno, "link", f"`{link}`: no such file"))
    return findings, citations


def check(doc_root: Path) -> tuple[list[Finding], list[tuple[Path, int, int, str]], int]:
    findings: list[Finding] = []
    citations: list[tuple[Path, int, int, str]] = []
    files = sorted(doc_root.rglob("*.md")) if doc_root.is_dir() else []
    for md in files:
        f, c = check_file(md)
        findings.extend(f)
        citations.extend(c)
    return findings, citations, len(files)


def dump(citations: list[tuple[Path, int, int, str]]) -> None:
    for md, lineno, start, target in citations:
        path = Path(target)
        rel = path.relative_to(REPO) if path.is_relative_to(REPO) else path
        text = path.read_text(encoding="utf-8", errors="replace").splitlines()[start - 1]
        doc = md.relative_to(REPO) if md.is_relative_to(REPO) else md
        print(f"{doc}:{lineno} -> {rel}:{start}: {text.strip()}")


def selftest() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "wireless-drivers" / "iwlwifi").mkdir(parents=True)
        (root / "docs" / "wireless_devices" / "x").mkdir(parents=True)
        (root / "docs" / "boards").mkdir(parents=True)
        (root / "wireless-drivers" / "iwlwifi" / "drv.c").write_text(
            "\n".join(f"/* line {i} */" for i in range(1, 11)) + "\n", encoding="utf-8")
        (root / "docs" / "boards" / "board.md").write_text("# board\n", encoding="utf-8")
        doc = root / "docs" / "wireless_devices" / "x" / "a.md"
        doc.write_text(
            "Good: `drv.c:3`, `drv.c:2-4`, and `docs/boards/board.md:1`.\n"
            "Missing file: `gone.c:1`.\n"
            "Out of range: `drv.c:99`.\n"
            "Broken link: [b](missing.md).\n"
            "Link that exists: [c](../../boards/board.md).\n"
            "Not a citation: `firmware.ucode`, `0x1000`.\n",
            encoding="utf-8")

        global REPO, DRIVER_ROOT
        saved = (REPO, DRIVER_ROOT)
        REPO, DRIVER_ROOT = root, root / "wireless-drivers" / "iwlwifi"
        try:
            findings, citations, files = check(root / "docs" / "wireless_devices")
        finally:
            REPO, DRIVER_ROOT = saved

        kinds = sorted(f.kind for f in findings)
        assert files == 1, files
        assert len(citations) == 3, citations
        assert kinds == ["citation", "citation", "link"], kinds
        joined = " ".join(str(f) for f in findings)
        assert "gone.c:1" in joined and "line range 99-99" in joined, joined
        assert "missing.md" in joined, joined
    print("selftest: ok (3 citations resolved, 2 bad citations and 1 dead link caught)")
    return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, default=DOC_ROOT,
                    help="documentation root (default: docs/wireless_devices)")
    ap.add_argument("--dump", action="store_true",
                    help="print every citation with the cited source line")
    ap.add_argument("--selftest", action="store_true", help="run the checker's tests")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()

    root = args.root if args.root.is_absolute() else REPO / args.root
    if not root.is_dir():
        print(f"check-device-docs: no such directory: {root}", file=sys.stderr)
        return 2

    findings, citations, files = check(root)
    if args.dump:
        dump(citations)
        print()
    for f in findings:
        print(f"check-device-docs: FAIL {f}", file=sys.stderr)
    if findings:
        return 1
    print(f"check-device-docs: {len(citations)} citations in {files} files, all resolved")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
