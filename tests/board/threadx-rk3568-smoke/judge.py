#!/usr/bin/env python3
"""Judge for the ThreadX RK3568 bring-up smoke suite (threadx-rk3568-smoke).

Input: one serial transcript. Output: the judgment block; exit 0 = pass,
1 = fail (an absent end anchor means the run did not finish), 2 = configuration
error (the transcript does not match what this declaration says should run).

The smoke image carries no judgment channel of its own: it prints a fixed set
of anchor lines, and the judge walks them in the order they appear. Two shapes
of line live side by side and the shared normalization covers both - the
kernel's log lines carry a stamp ("[   s.mmm] ...", stripped before matching)
while the GIC diagnostic goes straight to the polled console without one.

Judgment is one ordered walk over the anchor table below. Every row is a data
row: it names the emitting site in this repository, so a kernel pin bump or a
message edit updates a row, not the logic. The ladder row is numeric: three
beats of `tick: t=<n>`, each 1000 +/- 10 ticks after the previous one, with the
wall-clock stamp moving by the same amount +/- 10 ms. The window is a window
and not an equality because the beats are tx_thread_sleep(1000) wake-ups,
which land one tick late (measured on the board: 1000, 2001, 3002). The ladder
and the second thread interleave in the real image (the lo thread reports
between two beats), so the ladder is checked on its own lines and only the end
anchor has to come after everything.
"""

import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))

import board_judge as bj  # noqa: E402

SUITE = "threadx-rk3568-smoke"

#: Case names in execution order - the anchor names of this suite. The same
#: list is declared in suite.mk (SUITE_CASE_NAMES) and reconciled at build time
#: (tests/board/common/check-case-list.sh walks image, declaration and judge).
CASES = [
    "kernel",
    "unused-mem",
    "gic",
    "tick",
    "stamp",
    "ladder",
    "thread-lo",
    "pass",
]

#: Anchors, in the order the image prints them. Emitting sites:
#:   kernel      os/threadx/board/rk3568-itx/tx_glue.c (kernel entry banner)
#:   unused-mem  os/threadx/board/rk3568-itx/tx_glue.c (mapped-RAM probe)
#:   gic         arch/aarch64/gicv3.c (controller-ready diagnostic)
#:   tick        os/threadx/board/rk3568-itx/tx_glue.c (tick source report)
#:   stamp       os/threadx/board/rk3568-itx/tx_glue.c (stamp switch)
#:   ladder      tests/board/threadx-rk3568-smoke/main.c (one beat per second)
#:   thread-lo   tests/board/threadx-rk3568-smoke/main.c (second thread count)
#:   pass        tests/board/threadx-rk3568-smoke/main.c (end token)
ANCHORS = {
    "kernel": [r"tx: kernel entered version=6\.5\.1\.202602a"],
    "unused-mem": [r"tx: unused_mem=0x"],
    "gic": [r"gicv3: ready ctlr=00000012 waker=00000000"],
    "tick": [r"tick: source=virt irq=27 freq=1000Hz"],
    "stamp": [r"stamp: switched to tick"],
    "ladder": [],  # numeric checks below
    "thread-lo": [r"thr\[lo\]: n=(\d+)"],
    "pass": [r"threadx: smoke PASS"],
}

#: The end anchor (the same literal the declaration names in
#: SUITE_END_PATTERN). It must be present and it must come after every other
#: anchor, including the ladder's last beat.
END_ANCHOR = r"threadx: smoke PASS"

LADDER_RE = re.compile(r"tick: t=(\d+)")
LADDER_BEATS = 3
LADDER_STEP = 1000
LADDER_TOLERANCE_TICKS = 10
STAMP_TOLERANCE_MS = 10

# --------------------------------------------------------------------------
# selftest fixture: one legal transcript plus the mutations that must go red.
# The shape mirrors a real run: stamped kernel lines, the unstamped GIC
# diagnostic in between, the first beat at the whole second the image aligns
# to, the lo thread's count interleaved between two beats, the PASS token right
# after the third beat.
# --------------------------------------------------------------------------

BASE_TRANSCRIPT = (
    "[   0.000] tx: kernel entered version=6.5.1.202602a\r\n"
    "[   0.004] tx: unused_mem=0x0\r\n"
    "gicv3: ready ctlr=00000012 waker=00000000\r\n"
    "[   0.011] tick: source=virt irq=27 freq=1000Hz\r\n"
    "[   0.000] stamp: switched to tick\r\n"
    "[   1.000] tick: t=1000\r\n"
    "[   2.001] tick: t=2001\r\n"
    "[   2.002] thr[lo]: n=1\r\n"
    "[   3.002] tick: t=3002\r\n"
    "[   3.003] threadx: smoke PASS\r\n"
)

TRUNCATED_TRANSCRIPT = (
    "[   0.000] tx: kernel entered version=6.5.1.202602a\r\n"
    "[   0.004] tx: unused_mem=0x0\r\n"
    "gicv3: ready ctlr=00000012 waker=00000000\r\n"
    "[   0.011] tick: source=virt irq=27 freq=1000Hz\r\n"
    "[   0.000] stamp: switched to tick\r\n"
    "[   1.000] tick: t=1000\r\n"
    "[   2.001] tick: t=2001\r\n"
    "noise flood: the capture ends before the third beat and the PASS token\r\n"
    "##########\r\n"
)

WRONG_IMAGE_TRANSCRIPT = (
    "[   5.000] BOARD-SUITE threadx-rk3568-smoke start cases=1 sel=all "
    "list=kernel\r\n" + BASE_TRANSCRIPT
)

MUTATIONS = [
    ("kernel anchor dropped", BASE_TRANSCRIPT.replace(
        "[   0.000] tx: kernel entered version=6.5.1.202602a\r\n", ""), 1),
    ("kernel version bumped (stale data row)", BASE_TRANSCRIPT.replace(
        "version=6.5.1.202602a", "version=6.5.2.202602a"), 1),
    ("unused-mem anchor dropped", BASE_TRANSCRIPT.replace(
        "[   0.004] tx: unused_mem=0x0\r\n", ""), 1),
    ("gic anchor dropped", BASE_TRANSCRIPT.replace(
        "gicv3: ready ctlr=00000012 waker=00000000\r\n", ""), 1),
    ("tick anchor dropped", BASE_TRANSCRIPT.replace(
        "[   0.011] tick: source=virt irq=27 freq=1000Hz\r\n", ""), 1),
    ("stamp anchor dropped", BASE_TRANSCRIPT.replace(
        "[   0.000] stamp: switched to tick\r\n", ""), 1),
    ("ladder beat dropped", BASE_TRANSCRIPT.replace(
        "[   2.001] tick: t=2001\r\n", ""), 1),
    ("ladder tick delta broken", BASE_TRANSCRIPT.replace(
        "t=2001", "t=2500"), 1),
    ("ladder stamp delta out of tolerance", BASE_TRANSCRIPT.replace(
        "[   2.001] tick: t=2001", "[   2.500] tick: t=2001"), 1),
    ("second-thread anchor dropped", BASE_TRANSCRIPT.replace(
        "[   2.002] thr[lo]: n=1\r\n", ""), 1),
    ("second-thread counter zero", BASE_TRANSCRIPT.replace(
        "thr[lo]: n=1", "thr[lo]: n=0"), 1),
    ("end anchor dropped", BASE_TRANSCRIPT.replace(
        "[   3.003] threadx: smoke PASS\r\n", ""), 1),
    ("noise flood truncation", TRUNCATED_TRANSCRIPT, 1),
    ("fault line injected", BASE_TRANSCRIPT + (
        "[   4.001] tx_fault_park kind=1 esa=0x0\r\n"), 1),
    ("wrong image under this declaration", WRONG_IMAGE_TRANSCRIPT, 2),
]


def find_anchor(transcript: bj.Transcript, pattern: str, start: int) -> tuple[int, str] | None:
    """First stamp-stripped line at or after `start` matching `pattern`."""
    regex = re.compile(pattern)
    for index in range(start, len(transcript.lines)):
        line = transcript.lines[index].strip()
        if regex.search(line):
            return index, line
    return None


def check_ladder(transcript: bj.Transcript) -> tuple[list[str], int | None, list[str]]:
    """The ladder beats: (errors, index of the last checked beat, report).

    The acceptance window is the first three beats; the hi thread keeps beating
    every second afterwards (resident heartbeat), so a longer ladder is legal
    and only the first three beats carry the criterion. Beats are read from the
    raw lines because the wall-clock spacing is judged from their stamps.
    """
    errors: list[str] = []
    beats = []
    for index, line in enumerate(transcript.raw_lines):
        match = LADDER_RE.search(line)
        if match:
            beats.append((index, bj.stamp_ms(line), int(match.group(1))))
    if len(beats) < LADDER_BEATS:
        return (["ladder: %d beats (want at least %d)" % (len(beats), LADDER_BEATS)],
                None, [])

    window = beats[:LADDER_BEATS]
    report = ["beat %d: %s" % (position + 1, transcript.raw_lines[beat[0]].strip())
              for position, beat in enumerate(window)]
    for position, (_index, stamp, tick) in enumerate(window):
        if position == 0:
            continue
        _prev_index, prev_stamp, prev_tick = window[position - 1]
        delta = tick - prev_tick
        if abs(delta - LADDER_STEP) > LADDER_TOLERANCE_TICKS:
            errors.append("ladder: beat %d tick delta %d (want %d +/- %d)"
                          % (position + 1, delta, LADDER_STEP, LADDER_TOLERANCE_TICKS))
        if stamp is not None and prev_stamp is not None:
            stamp_delta = stamp - prev_stamp
            if abs(stamp_delta - LADDER_STEP) > STAMP_TOLERANCE_MS:
                errors.append("ladder: beat %d stamp delta %d ms (want %d +/- %d)"
                              % (position + 1, stamp_delta, LADDER_STEP, STAMP_TOLERANCE_MS))
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
            out.append("anchor %-10s line %4d  %s" % (name, index, line))
            if not errors:
                context_index = index

    for line in transcript.anchor_lines(r"thr\[lo\]: n=(\d+)"):
        counter = int(re.search(r"thr\[lo\]: n=(\d+)", line).group(1))
        if counter < 1:
            errors.append("case thread-lo: counter is zero: %s" % line)
            break

    ladder_errors, ladder_last, ladder_report = check_ladder(transcript)
    errors.extend(ladder_errors)
    out.extend(ladder_report)

    end_index = matched.get("pass")
    if end_index is not None and ladder_last is not None and end_index < ladder_last:
        errors.append("end anchor (line %d) is not last: the ladder's third beat is at line %d"
                      % (end_index, ladder_last))

    out.append("cases: %d declared, %d anchor rows matched" % (len(CASES), len(matched)))
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
