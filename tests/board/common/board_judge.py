#!/usr/bin/env python3
"""Shared primitives for the board-suite judges.

A judge consumes one serial transcript and returns 0 (pass), 1 (fail - a
missing sentinel counts as an incomplete run and is a fail) or 2 (a
configuration error: the declaration and the transcript disagree about what
should have run). This module carries what every suite judge needs; a suite's
judge adds its own data tables (case names, anchors, tolerances).

Transcript normalization (three views, all derived once):

  raw_lines   the transcript split on CR/LF, unchanged (stamps included);
  lines       raw_lines with the log stamp removed - protocol lines and
              verbatim anchors are matched here;
  rx_lines    lines with transmitted bytes removed, ANSI escapes stripped and
              CR normalized - interaction steps and prompt-shaped anchors are
              matched here.

Transmitted bytes arrive in the same ring buffer as received bytes, marked with
``==> ``. The mark is injected inline when a prompt fragment (no newline) is
pending, so the cut is "from the mark to the end of that physical line": the
pending prompt fragment is kept, the echoed command is dropped, and a sent
command can never match its own expectation.
"""

from __future__ import annotations

import re
import sys

STAMP_RE = re.compile(r"\[\s*(\d+)\.(\d{3})\]\s*")
# A CSI sequence: ESC '[' parameters/intermediates, then a final byte from
# 0x40-0x7e. The final byte is NOT always a letter - an interleaved writer can
# leave '\x1b[' immediately followed by another '[' (the console redraws while
# a case prints, and the two writers share one byte stream), which is itself a
# legal final byte. Matching only letters leaves that fragment in the text.
CSI_RE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")
# Leftover fragments: a truncated escape at the end of a split.
SGR_RE = CSI_RE
TX_MARK = "==> "

#: Conditions that are never a pass, whatever else the transcript says.
FAULT_PATTERNS = (
    r"fatal:",
    r"tx_fault_park",
    r"tick: SETUP FAILED",
    r"tx: stack error",
)

_GLOB_RE = re.compile(r"^[a-z0-9\-*?]{1,32}$")


def split_lines(text: str) -> list[str]:
    return re.split(r"\r\n|\r|\n", text)


def stamp_ms(line: str) -> int | None:
    """Milliseconds of the line's log stamp, or None when it carries none."""
    match = STAMP_RE.match(line)
    if not match:
        return None
    return int(match.group(1)) * 1000 + int(match.group(2))


class Transcript:
    """One transcript, normalized once."""

    def __init__(self, text: str) -> None:
        self.raw = text
        self.raw_lines = split_lines(text)
        # The protocol view: the log stamp removed AND escape sequences
        # stripped. The console redraws itself from other contexts (a shell
        # prompt repainting while a case prints), and its cursor/colour
        # sequences can land at the start of a protocol line - measured on the
        # board, where an SGR prefix hid a CASE line from the judge. Escape
        # sequences carry no protocol meaning, so removing them here is safe.
        self.lines = []
        for line in self.raw_lines:
            clean = CSI_RE.sub("", STAMP_RE.sub("", line))
            clean = clean.replace("\x1b", "")   # truncated escape fragments
            self.lines.append(clean)
        rx_lines = []
        for line in self.lines:
            cut = line.find(TX_MARK)
            if cut >= 0:
                line = line[:cut]
            rx_lines.append(SGR_RE.sub("", line))
        self.rx_lines = rx_lines
        self.rx_text = "\n".join(rx_lines)

    @classmethod
    def load(cls, path: str) -> "Transcript":
        with open(path, "rb") as handle:
            return cls(handle.read().decode("utf-8", errors="replace"))

    # -- judgment channel ---------------------------------------------------

    def start_line(self, name: str) -> dict | None:
        pattern = re.compile(
            r"(?:^|\s)BOARD-SUITE " + re.escape(name)
            + r" start cases=(\d+) sel=(\S+) list=(\S*)$"
        )
        for line in self.lines:
            match = pattern.search(line.strip())
            if match:
                names = match.group(3)
                return {
                    "cases": int(match.group(1)),
                    "sel": match.group(2),
                    "list": names.split(",") if names else [],
                }
        return None

    def sentinel(self, name: str) -> dict | None:
        pattern = re.compile(
            r"(?:^|\s)BOARD-SUITE " + re.escape(name)
            + r" result=(pass|fail) cases=(\d+) ok=(\d+) exit=(\d+)$"
        )
        for line in self.lines:
            match = pattern.search(line.strip())
            if match:
                return {
                    "result": match.group(1),
                    "cases": int(match.group(2)),
                    "ok": int(match.group(3)),
                    "exit": int(match.group(4)),
                }
        return None

    def case_lines(self, suite: str) -> tuple[list[tuple[str, str]], list[str]]:
        """(case, verdict) pairs for this suite, plus any foreign CASE line."""
        cases = []
        foreign = []
        pattern = re.compile(r"(?:^|\s)CASE (\S+)/(\S+) (PASS|FAIL)$")
        for line in self.lines:
            match = pattern.search(line.strip())
            if not match:
                continue
            if match.group(1) == suite:
                cases.append((match.group(2), match.group(3)))
            else:
                foreign.append(line.strip())
        return cases, foreign

    def faults(self) -> list[str]:
        found = []
        for line in self.lines:
            for pattern in FAULT_PATTERNS:
                if re.search(pattern, line):
                    found.append(line.strip())
                    break
            else:
                if re.search(r"(?:^|\s)CASE \S+/\S+ FAIL$", line.strip()):
                    found.append(line.strip())
        return found

    def anchor_lines(self, pattern: str) -> list[str]:
        """All stamp-stripped lines matching a regex (verbatim anchors)."""
        return [line.strip() for line in self.lines if re.search(pattern, line)]

    def rx_has(self, pattern: str) -> bool:
        return re.search(pattern, self.rx_text) is not None


# -- selection --------------------------------------------------------------


def glob_match(pattern: str, name: str) -> bool:
    """The image's matcher, reimplemented: '*' = any run, '?' = one character."""
    p = 0
    n = 0
    star = None
    mark = 0
    while n < len(name):
        if p < len(pattern) and pattern[p] == "*":
            star = p
            p += 1
            mark = n
            continue
        if p < len(pattern) and (pattern[p] == "?" or pattern[p] == name[n]):
            p += 1
            n += 1
            continue
        if star is not None:
            p = star + 1
            mark += 1
            n = mark
            continue
        return False
    while p < len(pattern) and pattern[p] == "*":
        p += 1
    return p == len(pattern)


def select_names(names: list[str], spec: str) -> list[str] | None:
    """Recompute the selected set from the echoed directive.

    Returns None when the directive is malformed (the judge then treats the
    run as a configuration error, matching the image's parser).
    """
    only = None
    skip = None
    if spec != "all":
        for token in spec.split():
            if token.startswith("only=") and _GLOB_RE.match(token[5:]) and only is None:
                only = token[5:]
            elif token.startswith("skip=") and _GLOB_RE.match(token[5:]) and skip is None:
                skip = token[5:]
            else:
                return None
    selected = []
    for name in names:
        if only is not None and not glob_match(only, name):
            continue
        if skip is not None and glob_match(skip, name):
            continue
        selected.append(name)
    return selected


def load_vectors(path: str) -> list[tuple[str, str, bool]]:
    vectors = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.split()
            if len(parts) != 3:
                continue
            vectors.append((parts[0], parts[1], parts[2] == "1"))
    return vectors


def check_vectors(path: str) -> tuple[int, list[str]]:
    """Replay the shared matcher vectors through the Python twin."""
    bad = []
    vectors = load_vectors(path)
    for name, pattern, expect in vectors:
        if glob_match(pattern, name) != expect:
            bad.append("%s ~ %s expect=%d" % (name, pattern, expect))
    return len(vectors), bad


# -- interaction steps ------------------------------------------------------


def decode_send(text: str) -> str:
    """Decode the step file's \\r / \\t / \\n escapes (the runner sends bytes)."""
    out = []
    i = 0
    while i < len(text):
        if text[i] == "\\" and i + 1 < len(text) and text[i + 1] in "rtn":
            out.append({"r": "\r", "t": "\t", "n": "\n"}[text[i + 1]])
            i += 2
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def load_steps(path: str) -> list[tuple[str, str]]:
    """Two-column TSV: send text (escapes decoded later), expected regex."""
    steps = []
    with open(path, encoding="utf-8") as handle:
        for line in handle.read().splitlines():
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            if "\t" not in line:
                continue
            send, expect = line.split("\t", 1)
            steps.append((send, expect.strip()))
    return steps


def verify_steps(rx_text: str, steps: list[tuple[str, str]]) -> list[dict]:
    """Check every expectation in order; each search starts where the previous
    match ended. A missing expectation keeps the cursor (localizes the break)."""
    results = []
    pos = 0
    for index, (send, expect) in enumerate(steps):
        entry = {"index": index + 1, "send": send, "expect": expect, "ok": False}
        try:
            match = re.compile(expect).search(rx_text, pos)
        except re.error as exc:
            entry["detail"] = "bad regex: %s" % exc
            results.append(entry)
            continue
        if match is None:
            entry["detail"] = "not found after offset %d" % pos
        else:
            entry["ok"] = True
            entry["span"] = match.span()
            pos = match.end()
        results.append(entry)
    return results


# -- reporting --------------------------------------------------------------


def excerpt(text: str, needle: str, before: int = 3, after: int = 3) -> str:
    """The first line containing needle, with context (fail localization)."""
    lines = split_lines(text)
    for index, line in enumerate(lines):
        if needle in line:
            lo = max(0, index - before)
            hi = min(len(lines), index + after + 1)
            return "\n".join("    %s" % lines[i] for i in range(lo, hi))
    return ""


def run_selftest(base_text: str, mutations: list[tuple[str, str, int]], judge_fn,
                 steps_path: str | None) -> tuple[int, str]:
    """Feed the judge a legal transcript and every mutation; all must land on
    their expected exit code (the base on 0). A judge that cannot go red is
    itself broken, so this must pass before a real judgment is trusted."""
    report = []
    ok = True

    code, _ = judge_fn(base_text, steps_path)
    if code != 0:
        ok = False
        report.append("selftest: base transcript judged %d (want 0)" % code)
    else:
        report.append("selftest: base transcript judged pass")

    for label, mutated, want in mutations:
        code, _ = judge_fn(mutated, steps_path)
        good = code == want
        ok = ok and good
        report.append(
            "selftest: %-42s exit=%d want=%d %s"
            % (label, code, want, "ok" if good else "MISMATCH")
        )
    return (0 if ok else 1), "\n".join(report)


def main(judge_fn, argv: list[str], steps_path: str | None, vectors_path: str | None,
         base_text: str, mutations: list[tuple[str, str, int]]) -> int:
    """Standard judge entry point: --selftest, or one transcript path."""
    if "--selftest" in argv:
        code, report = run_selftest(base_text, mutations, judge_fn, steps_path)
        print(report)
        if vectors_path:
            total, bad = check_vectors(vectors_path)
            for entry in bad:
                print("selftest: vector mismatch: %s" % entry)
            print("selftest: matcher vectors checked=%d bad=%d" % (total, len(bad)))
            if bad:
                code = 1
        print("judge-selftest: %s" % ("PASS" if code == 0 else "FAIL"))
        return code

    args = [arg for arg in argv if not arg.startswith("--")]
    if len(args) != 1:
        print("usage: judge.py [--selftest] <transcript>", file=sys.stderr)
        return 2
    code, report = judge_fn(Transcript.load(args[0]).raw, steps_path)
    print(report)
    return code
