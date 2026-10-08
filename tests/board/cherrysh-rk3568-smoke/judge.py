#!/usr/bin/env python3
"""Judge for the CherrySH RK3568 shell suite (cherrysh-rk3568-smoke).

Input: one serial transcript. Output: the judgment block; exit 0 = pass,
1 = fail (an absent end anchor means the run did not finish), 2 = configuration
error (this declaration and the transcript or the steps file disagree).

This suite is judged on the interaction channel. The image brings the shell up
and prints its prompt; the host runner sends the steps declared in
tests/board/common/shell-steps.tsv - the same file this judge reads, so the
script and the expectation cannot drift apart - and the judge verifies every
expected response in order on the received view (transmitted bytes are marked
with "==> " and excluded, ANSI escapes are stripped, CR is normalized). One
case name per step row: the first row sends nothing and only asserts that the
prompt is up.

The bring-up anchors printed before the REPL takes over are checked in order
too, and the end anchor (the exit echo, the literal the declaration names in
SUITE_END_PATTERN) must come after every other response.
"""

import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))

import board_judge as bj  # noqa: E402

SUITE = "cherrysh-rk3568-smoke"

#: Case names in execution order - one per row of the shared step table. The
#: same list is declared in suite.mk (SUITE_CASE_NAMES).
CASES = [
    "prompt",
    "help-echo",
    "tab-complete",
    "clear-run",
    "exit-restore",
]

#: Bring-up anchors, printed before the REPL takes over; they must be present,
#: in this order, and ahead of the first prompt. Emitting sites:
#:   banner  tests/board/cherrysh-rk3568-smoke/main.c (banner + build tag)
#:   gic     arch/aarch64/gicv3.c (controller-ready diagnostic)
#:   tick    os/freertos/board/rk3568-itx/freertos_board_glue.c (tick report)
BOOT_ANCHORS = [
    r"cherrysh-rk3568 bring-up \S+",
    r"gicv3: ready ctlr=00000012 waker=00000000",
    r"tick: source=cntv irq=27 freq=1000Hz",
]

#: The end anchor, spelled as the declaration spells it in SUITE_END_PATTERN:
#: the shell echoes the exit command and the prompt comes back afterwards (the
#: board's exit resets the session, the image keeps running).
END_ANCHOR = re.compile(r"\$ exit")

HERE = os.path.dirname(os.path.abspath(__file__))
STEPS_PATH = os.path.join(HERE, "..", "common", "shell-steps.tsv")
VECTORS_PATH = os.path.join(HERE, "..", "common", "select-vectors.tsv")

# --------------------------------------------------------------------------
# selftest fixture: one legal transcript plus the mutations that must go red.
# The shape mirrors a real run: unstamped console lines from the image, then
# the shell session as the hub buffer delivers it - prompt fragments carry SGR
# colour codes and no newline, the transmitted commands arrive inline behind
# "==> ", and the line endings are CRLF.
# --------------------------------------------------------------------------

BASE_TRANSCRIPT = (
    "cherrysh-rk3568 bring-up selftest-tag\r\n"
    "gicv3: ready ctlr=00000012 waker=00000000\r\n"
    "tick: source=cntv irq=27 freq=1000Hz\r\n"
    "\x1b[0G\x1b[1;32muser\x1b[1;32m@\x1b[1;32mrk3568\x1b[m:\x1b[1;34m/\x1b[m$ "
    "\x1b[m\x1b[0J\x1b[16G==> help\r\n"
    "user@rk3568:/$ huser@rk3568:/$ heuser@rk3568:/$ heluser@rk3568:/$ help\r\n"
    "total function 4\r\n"
    "  exit   -> /sbin - leave the shell\r\n"
    "  clear  -> /sbin - clear the terminal screen\r\n"
    "  help   -> /sbin - display command usage\r\n"
    "  shsize -> /sbin - get or set the terminal window size\r\n"
    "\r\n"
    "total variable 2\r\n"
    "  $ZERO r-   1\r\n"
    "  $PATH r-  11\r\n"
    "user@rk3568:/$\r\n"
    "==> cl\t\r\n"
    "user@rk3568:/$ cuser@rk3568:/$ cluser@rk3568:/$ clear\r\n"
    "==> \r\n"
    "\x1b[2J\x1b[Huser@rk3568:/$ \r\n"
    "==> exit\r\n"
    "user@rk3568:/$ euser@rk3568:/$ exuser@rk3568:/$ exiuser@rk3568:/$ exit\r\n"
    "user@rk3568:/$ \r\n"
)

TRUNCATED_TRANSCRIPT = (
    "cherrysh-rk3568 bring-up selftest-tag\r\n"
    "gicv3: ready ctlr=00000012 waker=00000000\r\n"
    "tick: source=cntv irq=27 freq=1000Hz\r\n"
    "\x1b[0G\x1b[1;32muser\x1b[1;32m@\x1b[1;32mrk3568\x1b[m:\x1b[1;34m/\x1b[m$ "
    "\x1b[m\x1b[0J\x1b[16G==> help\r\n"
    "user@rk3568:/$ huser@rk3568:/$ heuser@rk3568:/$ heluser@rk3568:/$ help\r\n"
    "total function 4\r\n"
    "  exit   -> /sbin - leave the shell\r\n"
    "\r\n"
    "total variable 2\r\n"
    "  $ZERO r-   1\r\n"
    "user@rk3568:/$\r\n"
    "noise flood: the capture ends before the completion and the exit\r\n"
    "##########\r\n"
)

WRONG_IMAGE_TRANSCRIPT = (
    "[   5.000] BOARD-SUITE cherrysh-rk3568-smoke start cases=1 sel=all "
    "list=prompt\r\n" + BASE_TRANSCRIPT
)

MUTATIONS = [
    ("banner anchor dropped", BASE_TRANSCRIPT.replace(
        "cherrysh-rk3568 bring-up selftest-tag\r\n", ""), 1),
    ("gic anchor dropped", BASE_TRANSCRIPT.replace(
        "gicv3: ready ctlr=00000012 waker=00000000\r\n", ""), 1),
    ("help table truncated (step 2 response)", BASE_TRANSCRIPT.replace(
        "total variable 2\r\n", ""), 1),
    ("completion response dropped (step 3 response)", BASE_TRANSCRIPT.replace(
        "user@rk3568:/$ cuser@rk3568:/$ cluser@rk3568:/$ clear\r\n",
        "user@rk3568:/$ cuser@rk3568:/$ cl\r\n"), 1),
    ("exit echo dropped (end anchor)", BASE_TRANSCRIPT.replace(
        "user@rk3568:/$ euser@rk3568:/$ exuser@rk3568:/$ exiuser@rk3568:/$ exit\r\n", ""), 1),
    ("restored prompt dropped (step 5 response)", BASE_TRANSCRIPT.replace(
        "user@rk3568:/$ exit\r\nuser@rk3568:/$ \r\n",
        "user@rk3568:/$ exit\r\n"), 1),
    ("noise flood truncation", TRUNCATED_TRANSCRIPT, 1),
    ("fault line injected", BASE_TRANSCRIPT + (
        "fatal: parked; power-cycle to recover\r\n"), 1),
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

    if not steps_path or not os.path.exists(steps_path):
        out.append("configuration error: interaction steps file missing: %s" % steps_path)
        out.append("verdict: configuration error (declaration and steps disagree)")
        return 2, "\n".join(out)
    steps = bj.load_steps(steps_path)
    if len(steps) != len(CASES):
        out.append("configuration error: %d step rows but %d case names" % (len(steps), len(CASES)))
        out.append("verdict: configuration error (declaration and steps disagree)")
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
    boot_index: int | None = None
    for pattern in BOOT_ANCHORS:
        found = find_anchor(transcript, pattern, cursor)
        if found is None:
            state = "out of order" if transcript.anchor_lines(pattern) else "absent"
            errors.append("bring-up anchor %s: %s" % (state, pattern))
            continue
        index, line = found
        cursor = index + 1
        boot_index = index
        out.append("anchor %-8s line %4d  %s" % ("boot", index, line))

    results = bj.verify_steps(transcript.rx_text, steps)
    for entry in results:
        out.append("step %d %-7s %-14s %s" % (
            entry["index"], "ok" if entry["ok"] else "missing",
            CASES[entry["index"] - 1], entry["expect"]))
    missing = [entry for entry in results if not entry["ok"]]
    if missing:
        first = missing[0]
        errors.append("interaction step %d (%s) missing: %s (from offset %s)"
                      % (first["index"], CASES[first["index"] - 1], first["expect"],
                         first.get("detail", "?")))

    # The prompt the first step asserts must come after the bring-up anchors.
    if boot_index is not None and results[0]["ok"]:
        prompt_line = transcript.rx_text.count("\n", 0, results[0]["span"][0])
        if boot_index > prompt_line:
            errors.append("bring-up anchors (line %d) do not precede the first prompt (line %d)"
                          % (boot_index, prompt_line))

    # The end anchor must be present and it must come after every earlier
    # response (the exit echo closes the session; a prompt follows it, which is
    # what the exit-restore case is about).
    matches = list(END_ANCHOR.finditer(transcript.rx_text))
    if not matches:
        errors.append("end anchor absent: %s" % END_ANCHOR.pattern)
    else:
        end_match = matches[-1]
        earlier = [entry["span"][1] for entry in results[:len(CASES) - 1] if entry["ok"]]
        if earlier and end_match.start() < max(earlier):
            errors.append("end anchor is not last: it starts at offset %d, before an earlier response ends"
                          % end_match.start())

    out.append("steps: %d declared, %d answered" % (len(CASES), len(results) - len(missing)))
    if errors:
        out.append("errors:")
        out.extend("  - " + error for error in errors)
        context = ""
        if faults:
            context = bj.excerpt(transcript.raw, faults[0])
        elif missing and missing[0]["index"] > 1:
            answered = results[missing[0]["index"] - 2]
            if answered["ok"]:
                matched = transcript.rx_text[answered["span"][0]:answered["span"][1]]
                context = bj.excerpt(transcript.raw, matched.strip().splitlines()[0])
        if context:
            out.append("last good response and its context:")
            out.append(context)
        tail = [line for line in transcript.rx_lines[-5:] if line.strip()]
        if tail:
            out.append("tail of the received view:")
            out.extend("    " + line for line in tail)
        out.append("verdict: fail")
        return 1, "\n".join(out)

    out.append("verdict: pass")
    return 0, "\n".join(out)


if __name__ == "__main__":
    sys.exit(bj.main(judge_text, sys.argv[1:], STEPS_PATH, VECTORS_PATH,
                     BASE_TRANSCRIPT, MUTATIONS))
