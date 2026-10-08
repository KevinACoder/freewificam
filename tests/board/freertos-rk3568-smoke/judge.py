#!/usr/bin/env python3
"""Judge for the FreeRTOS RK3568 bring-up smoke suite (freertos-rk3568-smoke).

Input: one serial transcript. Output: the judgment block; exit 0 = pass,
1 = fail (an absent end anchor means the run did not finish), 2 = configuration
error (the transcript does not match what this declaration says should run).

The smoke image carries no judgment channel of its own: it prints a fixed set
of anchor lines through the polled console. Those lines carry no log stamp -
this image never switches the log to tick mode - and the shared normalization
keeps a stamp-free line unchanged, which is the same "strip if present" rule
the stamped images rely on.

Judgment is one ordered walk over the anchor table below. Every row is a data
row: it names the emitting site in this repository, so a kernel pin bump or a
message edit updates a row, not the logic. The ladder row is numeric: the
reporter prints one beat per 1000 ticks, and the beats must be exactly 1000
ticks apart, with the independent system counter 1000 +/- 10 % apart and a
scheduling count that only grows.
"""

import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))

import board_judge as bj  # noqa: E402

SUITE = "freertos-rk3568-smoke"

#: Case names in execution order - the anchor names of this suite. The same
#: list is declared in suite.mk (SUITE_CASE_NAMES) and reconciled at build time
#: (tests/board/common/check-case-list.sh walks image, declaration and judge).
CASES = [
    "bring-up",
    "kernel",
    "gic",
    "tick",
    "task-hi",
    "task-lo",
    "ladder",
    "pass",
]

#: Anchors, in the order the image prints them. Emitting sites:
#:   bring-up  tests/board/freertos-rk3568-smoke/main.c (banner + build tag)
#:   kernel    tests/board/freertos-rk3568-smoke/main.c (kernel version line)
#:   gic       arch/aarch64/gicv3.c (controller-ready diagnostic)
#:   tick      os/freertos/board/rk3568-itx/freertos_board_glue.c (tick report)
#:   task-hi   tests/board/freertos-rk3568-smoke/main.c (first activation)
#:   task-lo   tests/board/freertos-rk3568-smoke/main.c (first activation)
#:   ladder    tests/board/freertos-rk3568-smoke/main.c (reporter beats)
#:   pass      tests/board/freertos-rk3568-smoke/main.c (end token)
ANCHORS = {
    "bring-up": [r"freertos-rk3568 bring-up \S+"],
    "kernel": [r"freertos: kernel V11\.3\.1 port=ARM_AARCH64_SRE guest=EL1"],
    "gic": [r"gicv3: ready ctlr=00000012 waker=00000000"],
    "tick": [r"tick: source=cntv irq=27 freq=1000Hz"],
    "task-hi": [r"task\[hi\] running loop=0"],
    "task-lo": [r"task\[lo\] running loop=0"],
    "ladder": [],  # numeric checks below
    "pass": [r"freertos: smoke PASS"],
}

#: The end anchor (the same literal the declaration names in
#: SUITE_END_PATTERN). It must be present and it must come after every other
#: anchor, including the ladder's last beat.
END_ANCHOR = r"freertos: smoke PASS"

LADDER_RE = re.compile(r"freertos: tick=(\d+) switches=(\d+) cntv_ms=(\d+)")
LADDER_BEATS = 3
LADDER_STEP = 1000
CNTV_TOLERANCE_PCT = 10

# --------------------------------------------------------------------------
# selftest fixture: one legal transcript plus the mutations that must go red.
# The image prints through the polled console, so its lines carry no log stamp;
# the shape mirrors a real run - banner, kernel, GIC, tick, both task lines,
# the reporter's three beats and the PASS token, in that order.
# --------------------------------------------------------------------------

BASE_TRANSCRIPT = (
    "freertos-rk3568 bring-up selftest-tag\r\n"
    "freertos: kernel V11.3.1 port=ARM_AARCH64_SRE guest=EL1\r\n"
    "gicv3: ready ctlr=00000012 waker=00000000\r\n"
    "tick: source=cntv irq=27 freq=1000Hz\r\n"
    "task[hi] running loop=0\r\n"
    "task[lo] running loop=0\r\n"
    "freertos: tick=1002 switches=142269658 cntv_ms=69252\r\n"
    "freertos: tick=2002 switches=284383617 cntv_ms=70252\r\n"
    "freertos: tick=3002 switches=426498129 cntv_ms=71253\r\n"
    "freertos: smoke PASS\r\n"
)

TRUNCATED_TRANSCRIPT = (
    "freertos-rk3568 bring-up selftest-tag\r\n"
    "freertos: kernel V11.3.1 port=ARM_AARCH64_SRE guest=EL1\r\n"
    "gicv3: ready ctlr=00000012 waker=00000000\r\n"
    "tick: source=cntv irq=27 freq=1000Hz\r\n"
    "task[hi] running loop=0\r\n"
    "task[lo] running loop=0\r\n"
    "freertos: tick=1002 switches=142269658 cntv_ms=69252\r\n"
    "freertos: tick=2002 switches=284383617 cntv_ms=70252\r\n"
    "noise flood: the capture ends before the third beat and the PASS token\r\n"
    "##########\r\n"
)

WRONG_IMAGE_TRANSCRIPT = (
    "[   5.000] BOARD-SUITE freertos-rk3568-smoke start cases=1 sel=all "
    "list=bring-up\r\n" + BASE_TRANSCRIPT
)

MUTATIONS = [
    ("banner anchor dropped", BASE_TRANSCRIPT.replace(
        "freertos-rk3568 bring-up selftest-tag\r\n", ""), 1),
    ("kernel anchor dropped", BASE_TRANSCRIPT.replace(
        "freertos: kernel V11.3.1 port=ARM_AARCH64_SRE guest=EL1\r\n", ""), 1),
    ("kernel version bumped (stale data row)", BASE_TRANSCRIPT.replace(
        "V11.3.1", "V11.4.0"), 1),
    ("gic anchor dropped", BASE_TRANSCRIPT.replace(
        "gicv3: ready ctlr=00000012 waker=00000000\r\n", ""), 1),
    ("tick anchor dropped", BASE_TRANSCRIPT.replace(
        "tick: source=cntv irq=27 freq=1000Hz\r\n", ""), 1),
    ("task anchor dropped", BASE_TRANSCRIPT.replace(
        "task[hi] running loop=0\r\n", ""), 1),
    ("ladder beat dropped", BASE_TRANSCRIPT.replace(
        "freertos: tick=2002 switches=284383617 cntv_ms=70252\r\n", ""), 1),
    ("ladder tick delta broken", BASE_TRANSCRIPT.replace(
        "freertos: tick=2002", "freertos: tick=2502"), 1),
    ("ladder counter delta out of tolerance", BASE_TRANSCRIPT.replace(
        "cntv_ms=70252", "cntv_ms=71000"), 1),
    ("ladder switches go backwards", BASE_TRANSCRIPT.replace(
        "switches=426498129", "switches=1000"), 1),
    ("end anchor dropped", BASE_TRANSCRIPT.replace(
        "freertos: smoke PASS\r\n", ""), 1),
    ("noise flood truncation", TRUNCATED_TRANSCRIPT, 1),
    ("fault line injected", BASE_TRANSCRIPT + (
        "fatal: parked; power-cycle to recover\r\n"), 1),
    ("wrong image under this declaration", WRONG_IMAGE_TRANSCRIPT, 2),
]


def find_anchor(transcript: bj.Transcript, pattern: str, start: int) -> tuple[int, str] | None:
    """First stamp-stripped line at or after `start` matching `pattern`.

    The line view is the shared normalization (stamp stripped if present); the
    anchors of this image never carry one, so the walk is verbatim.
    """
    regex = re.compile(pattern)
    for index in range(start, len(transcript.lines)):
        line = transcript.lines[index].strip()
        if regex.search(line):
            return index, line
    return None


def check_ladder(transcript: bj.Transcript) -> tuple[list[str], int | None, list[str]]:
    """The reporter's beats: (errors, index of the last checked beat, report).

    The acceptance window is the first three beats. The image stops after the
    third one (the reporter suspends the workers), so anything beyond three is
    not part of the criterion; a transcript that carries fewer is incomplete.
    """
    errors: list[str] = []
    beats = []
    for index, line in enumerate(transcript.lines):
        match = LADDER_RE.search(line)
        if match:
            beats.append((index, line.strip(), int(match.group(1)),
                          int(match.group(2)), int(match.group(3))))
    if len(beats) < LADDER_BEATS:
        return (["ladder: %d beats (want at least %d)" % (len(beats), LADDER_BEATS)],
                None, [])

    window = beats[:LADDER_BEATS]
    report = ["beat %d: %s" % (position + 1, beat[1])
              for position, beat in enumerate(window)]
    allowance = LADDER_STEP * CNTV_TOLERANCE_PCT // 100
    for position, (_index, _line, tick, switches, cntv_ms) in enumerate(window):
        if switches <= 0:
            errors.append("ladder: beat %d reports no scheduling activity (switches=%d)"
                          % (position + 1, switches))
        if position == 0:
            continue
        _prev_index, _prev_line, prev_tick, prev_switches, prev_cntv = window[position - 1]
        if tick - prev_tick != LADDER_STEP:
            errors.append("ladder: beat %d tick delta %d (want %d)"
                          % (position + 1, tick - prev_tick, LADDER_STEP))
        delta = cntv_ms - prev_cntv
        if abs(delta - LADDER_STEP) > allowance:
            errors.append("ladder: beat %d system-counter delta %d ms (want %d +/- %d)"
                          % (position + 1, delta, LADDER_STEP, allowance))
        if switches < prev_switches:
            errors.append("ladder: beat %d scheduling count fell (%d < %d)"
                          % (position + 1, switches, prev_switches))
    return errors, window[-1][0], report


def judge_text(text: str, steps_path: str | None = None) -> tuple[int, str]:
    transcript = bj.Transcript(text)
    out: list[str] = []
    errors: list[str] = []

    # This declaration registers an image with no judgment channel; a
    # BOARD-SUITE line means a different image was deployed under this name.
    if any("BOARD-SUITE" in line for line in transcript.lines):
        out.append("configuration error: BOARD-SUITE line in a suite declared as anchor-only")
        out.append("verdict: configuration error (wrong image for this declaration)")
        return 2, "\n".join(out)

    faults = transcript.faults() + transcript.anchor_lines(r"assert:")
    if faults:
        errors.append("fault line: %s" % faults[0])

    cases, foreign = transcript.case_lines(SUITE)
    if foreign:
        errors.append("foreign CASE line: %s" % foreign[0])
    unknown = [name for name, _verdict in cases if name not in CASES]
    if unknown:
        errors.append("case name outside the table: %s" % ",".join(unknown))

    cursor = 0
    matched: dict[str, int] = {}
    context_index: int | None = None
    for name in CASES:
        for pattern in ANCHORS[name]:
            found = find_anchor(transcript, pattern, cursor)
            if found is None:
                state = "out of order" if transcript.anchor_lines(pattern) else "absent"
                label = "end anchor" if pattern == END_ANCHOR else "anchor"
                errors.append("case %s: %s %s: %s" % (name, label, state, pattern))
                continue
            index, line = found
            matched[name] = index
            cursor = index + 1
            out.append("anchor %-9s line %4d  %s" % (name, index, line))
            if not errors:
                context_index = index

    ladder_errors, ladder_last, ladder_report = check_ladder(transcript)
    errors.extend(ladder_errors)
    out.extend(ladder_report)

    end_index = matched.get("pass")
    if end_index is not None and ladder_last is not None and end_index < ladder_last:
        errors.append("end anchor (line %d) is not last: the ladder's third beat is at line %d"
                      % (end_index, ladder_last))

    out.append("cases: %d declared, %d anchor rows matched"
               % (len(CASES), len(matched)))
    if errors:
        out.append("errors:")
        out.extend("  - " + error for error in errors)
        context = ""
        if faults:
            context = bj.excerpt(transcript.raw, faults[0])
        elif context_index is not None:
            context = bj.excerpt(transcript.raw, transcript.raw_lines[context_index].strip())
        if context:
            out.append("last good anchor and its context:")
            out.append(context)
        tail = [line for line in transcript.lines[-5:] if line.strip()]
        if tail:
            out.append("tail:")
            out.extend("    " + line for line in tail)
        out.append("verdict: fail")
        return 1, "\n".join(out)

    out.append("verdict: pass")
    return 0, "\n".join(out)


if __name__ == "__main__":
    sys.exit(bj.main(judge_text, sys.argv[1:], None, os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "common", "select-vectors.tsv"),
        BASE_TRANSCRIPT, MUTATIONS))
