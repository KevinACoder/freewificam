#!/usr/bin/env python3
"""@file    judge.py
@brief   Reference judge for the board-suite protocol fixtures.
@author  zhugengyu
@date    08.10.2026
@design  adapter + self-verifying checker - ref
         tests/board/threadx-rk3568-regression/judge.py: the same contract
         (one transcript in, a verdict block out, exit 0 = pass / 1 = fail /
         2 = configuration error, and a --selftest that must go red on every
         tampered fixture) over the shared transcript primitives in
         tests/board/common/board_judge.py. Nothing is re-parsed here: the
         start line, the CASE lines and the end sentinel all come from
         board_judge.

The synthetic line protocol the static transcripts under transcripts/ print:

    BOARD-SUITE fixture-accept start cases=<n> sel=<spec> list=<case,...>
    CASE fixture-accept/<case> PASS|FAIL
    BOARD-SUMMARY fixture-accept cases=<n> ok=<m> exit=<k>
    DUMP <id> seq=<i>/<n> len=<len> <hex...>
    BOARD-SUITE fixture-accept result=pass|fail cases=<n> ok=<m> exit=<k>

The start line, the CASE lines and the sentinel are the shared protocol. The
BOARD-SUMMARY line is the suite's independent summary anchor - the target of
the declaration's SUITE_SUMMARY_PATTERN (fixture-accept/suite.mk), which the
host runner captures; this judge consumes the anchor from the transcript and
echoes the reconciled counts in the same grammar. DUMP frames are the
recording channel only: a sequence gap marks the dump incomplete and never
flips the verdict.

Four-way reconciliation (every witness must agree before a pass):
  1. the end sentinel must be present; absent = incomplete = fail, with the
     transcript tail printed for localization;
  2. the sentinel's result / cases / ok / exit must agree with the CASE lines;
  3. the case list declared on the start line must equal the observed CASE
     names in order; a declared name that never printed is listed as missing;
  4. the summary anchor must agree with the sentinel and with the CASE lines.

Exit 2 is for a transcript that cannot be reconciled with the case table at
all: an observed case name the table does not declare, a malformed selection
directive, or a selection that selects no case. A missing case is a plain
fail; an unknown case means the declaration and the transcript describe
different runs (the image-side hijack case, analogous to the host container's
prefix check).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
COMMON = HERE.parent.parent / "common"
sys.path.insert(0, str(COMMON))

import board_judge as bj  # noqa: E402

SUITE = "fixture-accept"

#: The fixture suite's case table, in execution order. The transcripts and the
#: suite declaration carry the same names; the three are reconciled the way the
#: board suites reconcile image / declaration / judge at build time
#: (tests/board/common/check-case-list.sh), so a case cannot silently vanish
#: from the judgment channel.
CASES = ("alpha", "beta", "gamma", "delta")

#: The independent summary anchor, as declared by fixture-accept/suite.mk
#: (SUITE_SUMMARY_PATTERN). One grammar for the transcript line and for the
#: line this judge prints, so the declaration's capture works either way.
SUMMARY_RE = re.compile(
    r"^BOARD-SUMMARY " + re.escape(SUITE) + r" cases=(\d+) ok=(\d+) exit=(\d+)$"
)

#: A recording frame: DUMP <id> seq=<i>/<n> len=<len> <hex...>.
DUMP_RE = re.compile(r"^DUMP (\S+) seq=(\d+)/(\d+)(?:\s|$)")

#: The vector file of the shared matcher, replayed by --selftest. It lives one
#: directory above tests/board/fixtures, next to board_judge.py.
VECTORS = COMMON / "select-vectors.tsv"


def summary_counts(transcript: bj.Transcript) -> tuple[int, int, int] | None:
    """(cases, ok, exit) of the first summary anchor, or None when absent."""
    for line in transcript.lines:
        match = SUMMARY_RE.match(line.strip())
        if match:
            return (int(match.group(1)), int(match.group(2)), int(match.group(3)))
    return None


def dump_notes(transcript: bj.Transcript) -> list[str]:
    """Recording-channel notes: a sequence gap is reported, never judged."""
    frames: dict[str, list[tuple[int, int]]] = {}
    for line in transcript.lines:
        match = DUMP_RE.match(line.strip())
        if match:
            frames.setdefault(match.group(1), []).append(
                (int(match.group(2)), int(match.group(3)))
            )
    notes = []
    for ident in sorted(frames):
        seqs = frames[ident]
        total = max(last for _index, last in seqs)
        seen = {index for index, _last in seqs}
        missing = [index for index in range(1, total + 1) if index not in seen]
        if missing:
            notes.append(
                "dump-incomplete: DUMP %s frames %d/%d, missing seq %s"
                % (ident, len(seqs), total, ",".join(str(index) for index in missing))
            )
    return notes


def judge_text(text: str) -> tuple[int, str]:
    """Judge one transcript; returns (exit code, verdict block)."""
    transcript = bj.Transcript(text)
    out: list[str] = []
    config: list[str] = []

    sentinel = transcript.sentinel(SUITE)
    if sentinel is None:
        out.append("FAIL: end sentinel absent - incomplete run")
        tail = [line for line in transcript.lines[-5:] if line.strip()]
        if tail:
            out.append("tail:")
            out.extend("    " + line for line in tail)
        out.append("verdict: fail (incomplete)")
        return 1, "\n".join(out)

    start = transcript.start_line(SUITE)
    cases, foreign = transcript.case_lines(SUITE)
    faults = transcript.faults()
    observed = [name for name, _verdict in cases]
    passed = [name for name, verdict in cases if verdict == "PASS"]
    failed = [name for name, verdict in cases if verdict == "FAIL"]

    # -- configuration: the transcript cannot be reconciled with the table ----
    unknown = [name for name in observed if name not in CASES]
    if unknown:
        config.append(
            "unknown case name(s): %s - the case table cannot reconcile them"
            % ",".join(unknown)
        )
    declared = list(CASES)
    selected: list[str] | None = None
    if start is not None:
        declared = list(start["list"])
        selected = bj.select_names(list(CASES), start["sel"])
        if selected is None:
            config.append("malformed selection directive: %s" % start["sel"])
        elif not selected:
            config.append("empty selection: %s" % start["sel"])

    out.append("cases: %d observed, %d passed, %d failed"
               % (len(cases), len(passed), len(failed)))
    out.append("BOARD-SUMMARY %s cases=%d ok=%d exit=%d"
               % (SUITE, len(cases), len(passed), len(failed)))
    if config:
        out.append("errors:")
        out.extend("  - " + item for item in config)
        out.append("verdict: configuration error")
        return 2, "\n".join(out)

    errors: list[str] = []

    # -- 2. the sentinel against the CASE lines -------------------------------
    if sentinel["cases"] != len(cases):
        errors.append("sentinel cases=%d but %d CASE lines"
                      % (sentinel["cases"], len(cases)))
    if sentinel["ok"] != len(passed):
        errors.append("sentinel ok=%d but %d PASS lines" % (sentinel["ok"], len(passed)))
    if sentinel["exit"] != len(failed):
        errors.append("sentinel exit=%d but %d FAIL lines"
                      % (sentinel["exit"], len(failed)))
    if (sentinel["result"] == "pass") != (sentinel["exit"] == 0):
        errors.append("sentinel result=%s contradicts exit=%d"
                      % (sentinel["result"], sentinel["exit"]))
    if (sentinel["result"] == "pass") != (sentinel["ok"] == sentinel["cases"]):
        errors.append("sentinel result=%s contradicts ok=%d cases=%d"
                      % (sentinel["result"], sentinel["ok"], sentinel["cases"]))

    # -- 3. the declared list against the observed CASE lines -----------------
    if start is not None:
        if start["cases"] != len(cases):
            errors.append("start cases=%d but %d CASE lines"
                          % (start["cases"], len(cases)))
        if declared != observed:
            errors.append("declared case list=%s but observed=%s"
                          % (",".join(declared), ",".join(observed)))
    if selected is not None and selected != observed:
        errors.append("selection mismatch: expected=%s observed=%s"
                      % (",".join(selected), ",".join(observed)))
    missing = [name for name in declared if name not in observed]
    if missing:
        errors.append("missing case(s): %s" % ",".join(missing))
    extra = [name for name in observed if name not in declared]
    if extra:
        errors.append("undeclared case(s) ran: %s" % ",".join(extra))

    # -- 4. the summary anchor -----------------------------------------------
    summary = summary_counts(transcript)
    sentinel_counts = (sentinel["cases"], sentinel["ok"], sentinel["exit"])
    observed_counts = (len(cases), len(passed), len(failed))
    if summary is None:
        errors.append("summary anchor absent: no BOARD-SUMMARY line")
    else:
        if summary != sentinel_counts:
            errors.append(
                "summary cases=%d ok=%d exit=%d contradicts the sentinel "
                "cases=%d ok=%d exit=%d" % (summary + sentinel_counts)
            )
        if summary != observed_counts:
            errors.append(
                "summary cases=%d ok=%d exit=%d contradicts the CASE lines "
                "cases=%d ok=%d exit=%d" % (summary + observed_counts)
            )

    # -- faults and foreign lines (fail, not configuration) -------------------
    if foreign:
        errors.append("foreign CASE line: %s" % foreign[0])
    if faults:
        errors.append("fault line: %s" % faults[0])

    # -- the recording channel: reported, never judged ------------------------
    for note in dump_notes(transcript):
        out.append("note: %s" % note)

    if errors:
        out.append("errors:")
        out.extend("  - " + error for error in errors)
        excerpt = bj.excerpt(transcript.raw, "BOARD-SUITE " + SUITE + " result=")
        if excerpt:
            out.append("sentinel context:")
            out.append(excerpt.rstrip())
        out.append("verdict: fail")
        return 1, "\n".join(out)

    out.append("verdict: pass")
    return 0, "\n".join(out)


def load_manifest(path: Path) -> list[tuple[str, int, str]]:
    """(transcript file, expected exit, required text) rows; '#' starts a comment."""
    entries = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        needle = parts[2].strip() if len(parts) > 2 else ""
        entries.append((parts[0].strip(), int(parts[1].strip()), needle))
    return entries


def selftest() -> int:
    """Judge every fixture and every mutation; exit 0 only when all match."""
    ok = True
    transcripts = HERE / "transcripts"

    for name, want, needle in load_manifest(transcripts / "manifest.tsv"):
        path = transcripts / name
        if not path.exists():
            ok = False
            print("selftest: %-28s MISSING" % name)
            continue
        code, report = judge_text(bj.Transcript.load(str(path)).raw)
        if code != want:
            detail = "MISMATCH (want %d)" % want
        elif needle and needle not in report:
            detail = "MISMATCH (block lacks %r)" % needle
        else:
            detail = "ok"
        ok = ok and detail == "ok"
        print("selftest: %-28s exit=%d want=%d %s" % (name, code, want, detail))
        if detail != "ok":
            for line in report.splitlines():
                print("    | %s" % line)

    # The configuration-error path has no standing transcript in the fixture
    # set (every standing transcript is a run, and a run with an irreconcilable
    # case set is a declaration bug), so it is exercised as in-memory mutations
    # of the legal transcript - the same "feed it wrong, it must go red" move
    # the board suites make.
    legal = (transcripts / "legal.txt").read_text(encoding="utf-8")
    mutations = (
        ("mutation unknown case injected",
         legal.replace("CASE fixture-accept/delta PASS", "CASE fixture-accept/epsilon PASS"), 2),
        ("mutation malformed selection",
         legal.replace("sel=all", "sel=only=bad!!"), 2),
        ("mutation empty selection",
         legal.replace("sel=all", "sel=only=zzz*"), 2),
        ("mutation summary deleted",
         legal.replace("BOARD-SUMMARY fixture-accept cases=4 ok=4 exit=0\n", ""), 1),
    )
    for label, text, want in mutations:
        if text == legal:
            ok = False
            print("selftest: %-28s MUTATION DID NOT APPLY" % label)
            continue
        code, _report = judge_text(text)
        detail = "ok" if code == want else "MISMATCH (want %d)" % want
        ok = ok and code == want
        print("selftest: %-28s exit=%d want=%d %s" % (label, code, want, detail))

    if not VECTORS.exists():
        ok = False
        print("selftest: matcher vectors missing: %s" % VECTORS)
    else:
        total, bad = bj.check_vectors(str(VECTORS))
        for entry in bad:
            print("selftest: vector mismatch: %s" % entry)
        if bad:
            ok = False
        print("selftest: matcher vectors checked=%d bad=%d" % (total, len(bad)))

    print("judge-selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main(argv: list[str]) -> int:
    if "--selftest" in argv:
        return selftest()

    paths = [arg for arg in argv if not arg.startswith("--")]
    if len(paths) != 1:
        print("usage: judge.py [--selftest] <transcript>", file=sys.stderr)
        return 2
    code, report = judge_text(bj.Transcript.load(paths[0]).raw)
    print(report)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
