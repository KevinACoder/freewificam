#!/usr/bin/env python3
"""Judge for the ThreadX board UVC suite (threadx-rk3568-uvc).

Input: one serial transcript. Output: the judgment block; exit 0 = pass,
1 = fail (a missing sentinel counts as incomplete), 2 = configuration error
(the declaration and the transcript disagree about what should have run).

Truth sources, all independent of the transcript's own claims:
  * the case list below - reconciled at build time against the image's
    BOARD_TESTS section and suite.mk (tests/board/common/check-case-list.sh);
  * the anchor table below - each row names the emitting site in this
    repository, so a source move updates a data row, not the logic;
  * the iso-smoke numbers - cross-checked against the open line's payload
    bound (a packet longer than the alternate's bound is impossible on the
    wire), with the flood bound stated next to the constant;
  * the interaction steps - this suite's steps.tsv, the same file the host
    runner sends from.

Four-way reconciliation: the start line's self-report, the CASE lines, the end
sentinel's counts and the judge's own recomputation of the selection must all
agree. A CASE line for a name outside the table is a hijack attempt, not a
pass.
"""

import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))

import board_judge as bj  # noqa: E402

SUITE = "threadx-rk3568-uvc"

#: Case names in execution order (data row; the image table and suite.mk carry
#: the same list and the build reconciles the three).
CASES = [
    "shell-up",
    "usb-host-up",
    "uvc-open",
    "uvc-iso-smoke",
]

#: Anchors a passing case must have left behind (regex, matched on the
#: stamp-stripped transcript). Emitting sites:
#:   usb-host-up  tests/board/threadx-rk3568-uvc/tests/uvc_detect.c (controller
#:                init line) and tests/uvc_app.c (class connect line: the
#:                camera's identity, bus, minor 0 = video0)
#:   uvc-open     tests/uvc_app.c (the negotiated open line)
ANCHORS = {
    "shell-up": [r"user@rk3568:/\$"],
    "usb-host-up": [
        r"usbh: core init bus=0 hcd=ehci base=0xfd800000",
        r"uvc: connect vid=046d pid=0990 bus=0 dev=\d+ minor=0",
    ],
    "uvc-open": [
        r"uvc: open mjpeg 640x480 alt=4 fmt=\d+ frm=\d+ mps=\d+ dmp=\d+ ival=\d+",
    ],
    "uvc-iso-smoke": [],  # numeric checks below
}

#: Smoke tolerances. The acceptance criterion is "completions > 0, packet
#: lengths within the alternate's bound, no error flood"; the bounds below fix
#: what "flood" means so a broken transport (or a silently absent camera) can
#: never pass on one lucky urb.  Two failed urbs out of sixteen are the
#: startup-noise allowance for a freshly opened isochronous stream.
SMOKE_URBS_EXPECTED = 16
SMOKE_URBS_MIN = 14
SMOKE_ERRPKT_MAX = 8
SMOKE_RE = re.compile(
    r"uvc: iso smoke done=(\d+)/(\d+) bytes=(\d+) errpkt=(\d+) errurb=(\d+) max=(\d+)"
)
OPEN_MPS_RE = re.compile(r"uvc: open mjpeg 640x480 alt=4 fmt=\d+ frm=\d+ mps=(\d+)")

HERE = os.path.dirname(os.path.abspath(__file__))
STEPS_PATH = os.path.join(HERE, "steps.tsv")
VECTORS_PATH = os.path.join(HERE, "..", "common", "select-vectors.tsv")

# --------------------------------------------------------------------------
# selftest fixture: one legal transcript plus the mutations that must go red.
# --------------------------------------------------------------------------

# The console paints its prompt, so every prompt occurrence carries escape
# sequences (a plain "user@rk3568:/$" never appears on the wire).
PROMPT = "\x1b[1;32muser\x1b[m@\x1b[1;32mrk3568\x1b[m:\x1b[1;34m/\x1b[m$ \x1b[m"

SENTINEL_LINE = (
    "[  14.100] BOARD-SUITE threadx-rk3568-uvc "
    "result=pass cases=4 ok=4 exit=0\r\n"
)

BASE_TRANSCRIPT = (
    "threadx-rk3568-uvc bring-up selftest-tag\r\n"
    "gicv3: ready ctlr=00000012 waker=00000000\r\n"
    "[   0.000] tx: kernel entered version=6.5.1.202602a\r\n"
    "[   5.000] BOARD-SUITE threadx-rk3568-uvc select window_ms=5000\r\n"
    "[  10.000] BOARD-SUITE threadx-rk3568-uvc start cases=4 sel=all "
    "list=shell-up,usb-host-up,uvc-open,uvc-iso-smoke\r\n"
    + PROMPT
    + "[  10.010] CASE threadx-rk3568-uvc/shell-up PASS\r\n"
    "[  10.500] usbh: core init bus=0 hcd=ehci base=0xfd800000\r\n"
    "[  13.000] uvc: connect vid=046d pid=0990 bus=0 dev=3 minor=0 formats=2 alts=12\r\n"
    "[  13.100] CASE threadx-rk3568-uvc/usb-host-up PASS\r\n"
    "[  13.200] uvc: open mjpeg 640x480 alt=4 fmt=1 frm=5 mps=640 dmp=640 ival=1\r\n"
    "[  13.400] CASE threadx-rk3568-uvc/uvc-open PASS\r\n"
    "[  13.500] uvc: iso smoke done=16/16 bytes=102400 errpkt=0 errurb=0 max=640\r\n"
    "[  14.000] CASE threadx-rk3568-uvc/uvc-iso-smoke PASS\r\n"
    + SENTINEL_LINE
    + "==> help\r\n"
    + PROMPT + "help\x1b[0J\r\n"
    "total function 6\r\n"
    "  exit   -> /sbin - leave the shell\r\n"
    "\r\n"
    "total variable 2\r\n"
    "  $ZERO r-   1\r\n"
    + PROMPT + "==> exit\r\n"
    + PROMPT + "exit\x1b[0J\r\n"
    + PROMPT + "\x1b[0J\r\n"
)

EMPTY_SELECTION_TRANSCRIPT = (
    "threadx-rk3568-uvc bring-up selftest-tag\r\n"
    "[   5.000] BOARD-SUITE threadx-rk3568-uvc select window_ms=5000\r\n"
    "[  10.000] BOARD-SUITE threadx-rk3568-uvc start cases=0 sel=skip=* list=\r\n"
    "[  10.010] BOARD-SUITE-note select empty\r\n"
    "[  10.020] BOARD-SUITE threadx-rk3568-uvc result=pass cases=0 ok=0 exit=0\r\n"
)

MUTATIONS = [
    ("drop a CASE line", BASE_TRANSCRIPT.replace(
        "CASE threadx-rk3568-uvc/uvc-open PASS\r\n", ""), 1),
    ("sentinel counts disagree", BASE_TRANSCRIPT.replace(
        "cases=4 ok=4 exit=0", "cases=4 ok=3 exit=0"), 1),
    ("sentinel deleted", BASE_TRANSCRIPT.replace(SENTINEL_LINE, ""), 1),
    ("foreign case injected", BASE_TRANSCRIPT.replace(
        "CASE threadx-rk3568-uvc/uvc-open PASS",
        "CASE threadx-rk3568-uvc/zzz PASS\r\n"
        "CASE threadx-rk3568-uvc/uvc-open PASS"), 1),
    ("anchor dropped (connect)", BASE_TRANSCRIPT.replace(
        "uvc: connect vid=046d pid=0990 bus=0 dev=3 minor=0 formats=2 alts=12\r\n",
        ""), 1),
    ("anchor dropped (open)", BASE_TRANSCRIPT.replace(
        "uvc: open mjpeg 640x480 alt=4 fmt=1 frm=5 mps=640 dmp=640 ival=1\r\n",
        ""), 1),
    ("smoke incomplete", BASE_TRANSCRIPT.replace(
        "uvc: iso smoke done=16/16 bytes=102400 errpkt=0 errurb=0 max=640",
        "uvc: iso smoke done=8/16 bytes=51200 errpkt=0 errurb=8 max=640"), 1),
    ("smoke error flood", BASE_TRANSCRIPT.replace(
        "uvc: iso smoke done=16/16 bytes=102400 errpkt=0 errurb=0 max=640",
        "uvc: iso smoke done=16/16 bytes=102400 errpkt=64 errurb=0 max=640"), 1),
    ("smoke packet over the bound", BASE_TRANSCRIPT.replace(
        "uvc: iso smoke done=16/16 bytes=102400 errpkt=0 errurb=0 max=640",
        "uvc: iso smoke done=16/16 bytes=115200 errpkt=0 errurb=0 max=1024"), 1),
    ("start list disagrees", BASE_TRANSCRIPT.replace(
        "list=shell-up,usb-host-up,uvc-open,uvc-iso-smoke",
        "list=shell-up,usb-host-up,uvc-open"), 1),
    ("empty selection", EMPTY_SELECTION_TRANSCRIPT, 2),
]


def check_smoke(transcript: bj.Transcript) -> list[str]:
    """The smoke numbers against the open line's payload bound."""
    errors = []
    lines = transcript.anchor_lines(SMOKE_RE.pattern)
    if not lines:
        return ["iso smoke: anchor line absent"]
    match = SMOKE_RE.search(lines[-1])
    done = int(match.group(1))
    total = int(match.group(2))
    errpkt = int(match.group(4))
    errurb = int(match.group(5))
    maxlen = int(match.group(6))

    if done < 1:
        errors.append("iso smoke: no urb completed")
    if total != SMOKE_URBS_EXPECTED:
        errors.append("iso smoke: total=%d (want %d)" % (total, SMOKE_URBS_EXPECTED))
    if done < SMOKE_URBS_MIN:
        errors.append("iso smoke: only %d/%d urbs completed (want >= %d)"
                      % (done, total, SMOKE_URBS_MIN))
    if done + errurb != total:
        errors.append("iso smoke: done=%d errurb=%d does not add up to %d"
                      % (done, errurb, total))
    if errpkt > SMOKE_ERRPKT_MAX:
        errors.append("iso smoke: %d packet errors (flood bound %d)"
                      % (errpkt, SMOKE_ERRPKT_MAX))

    open_match = None
    for line in transcript.anchor_lines(OPEN_MPS_RE.pattern):
        open_match = OPEN_MPS_RE.search(line)
    if open_match is None:
        errors.append("iso smoke: open line absent - cannot bound the packet length")
    else:
        mps = int(open_match.group(1))
        if maxlen > mps:
            errors.append("iso smoke: packet %d bytes exceeds the bound %d" % (maxlen, mps))
    return errors


def judge_text(text: str, steps_path: str | None = None) -> tuple[int, str]:
    transcript = bj.Transcript(text)
    out: list[str] = []
    errors: list[str] = []

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

    if start is None:
        errors.append("start line absent")
    if foreign:
        errors.append("foreign CASE line: %s" % foreign[0])
    if faults:
        errors.append("fault line: %s" % faults[0])

    expected = bj.select_names(CASES, start["sel"]) if start else None
    if start and expected is None:
        errors.append("malformed selection directive: %s" % start["sel"])
    # A selection that keeps only the console prerequisite selected nothing the
    # round asked for: the directive and the suite disagree, so this is a
    # configuration error, not a test failure.
    if expected is not None and not [n for n in expected if n != bj.CONSOLE_PREREQ]:
        out.append("selection: %s" % start["sel"])
        out.append("verdict: configuration error (empty selection)")
        return 2, "\n".join(out)

    observed = [name for name, _verdict in cases]
    verdicts = dict(cases)
    passed = [name for name, verdict in cases if verdict == "PASS"]
    failed = [name for name, verdict in cases if verdict == "FAIL"]

    if sentinel["cases"] != len(cases):
        errors.append(
            "sentinel cases=%d but %d CASE lines" % (sentinel["cases"], len(cases))
        )
    if sentinel["ok"] != len(passed):
        errors.append("sentinel ok=%d but %d PASS lines" % (sentinel["ok"], len(passed)))
    if sentinel["exit"] != len(failed):
        errors.append("sentinel exit=%d but %d FAIL lines" % (sentinel["exit"], len(failed)))
    if (sentinel["result"] == "pass") != (sentinel["exit"] == 0):
        errors.append("sentinel result=%s contradicts exit=%d" % (sentinel["result"], sentinel["exit"]))
    if (sentinel["result"] == "pass") != (sentinel["ok"] == sentinel["cases"]):
        errors.append("sentinel result=%s contradicts ok=%d cases=%d"
                      % (sentinel["result"], sentinel["ok"], sentinel["cases"]))

    unknown = [name for name in observed if name not in CASES]
    if unknown:
        errors.append("unknown case name(s): %s" % ",".join(unknown))

    if start is not None:
        if start["cases"] != len(cases):
            errors.append("start cases=%d but %d CASE lines" % (start["cases"], len(cases)))
        if start["list"] != observed:
            errors.append("start list=%s but observed=%s"
                          % (",".join(start["list"]), ",".join(observed)))

    if expected is not None and expected != observed:
        errors.append("selection mismatch: expected=%s observed=%s"
                      % (",".join(expected), ",".join(observed)))

    for name in passed:
        if name not in CASES:
            continue
        for pattern in ANCHORS.get(name, ()):
            # Verbatim anchors live in the log lines; prompt-shaped anchors live
            # in the received stream, where the console colours them, so the
            # stripped view counts too.
            if not transcript.anchor_lines(pattern) and not transcript.rx_has(pattern):
                errors.append("case %s: anchor missing: %s" % (name, pattern))
    if verdicts.get("uvc-iso-smoke") == "PASS":
        errors.extend(check_smoke(transcript))

    shell_selected = expected is None or "shell-up" in expected
    if steps_path and os.path.exists(steps_path):
        steps = bj.load_steps(steps_path)
        results = bj.verify_steps(transcript.rx_text, steps)
        for entry in results:
            out.append("step %d %-7s %s" % (entry["index"],
                                            "ok" if entry["ok"] else "missing",
                                            entry["expect"]))
        missing = [entry for entry in results if not entry["ok"]]
        if missing and shell_selected:
            first = missing[0]
            errors.append("interaction step %d missing: %s (from offset %s)"
                          % (first["index"], first["expect"], first.get("detail", "?")))
        elif missing:
            out.append("note: shell not selected; missing steps are informational")
    elif shell_selected:
        errors.append("steps file missing: %s" % steps_path)

    out.append("cases: %d observed, %d passed, %d failed" % (len(cases), len(passed), len(failed)))
    if errors:
        out.append("errors:")
        out.extend("  - " + error for error in errors)
        excerpt = bj.excerpt(transcript.raw, errors[0].split(":")[-1].strip()[:20])
        if excerpt:
            out.append("first failure context:")
            out.append(excerpt)
        out.append("verdict: fail")
        return 1, "\n".join(out)

    out.append("verdict: pass")
    return 0, "\n".join(out)


if __name__ == "__main__":
    sys.exit(bj.main(judge_text, sys.argv[1:], STEPS_PATH, VECTORS_PATH,
                     BASE_TRANSCRIPT, MUTATIONS))
