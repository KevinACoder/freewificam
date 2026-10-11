#!/usr/bin/env python3
"""Judge for the ThreadX board SDIO suite (threadx-rk3568-sdio).

Input: one serial transcript. Output: the judgment block; exit 0 = pass,
1 = fail (a missing sentinel counts as incomplete), 2 = configuration error
(the declaration and the transcript disagree about what should have run).

Truth sources, all independent of the transcript's own claims:
  * the case list below - reconciled at build time against the image's
    BOARD_TESTS section and suite.mk (tests/board/common/check-case-list.sh);
  * the anchor table below - each row names the emitting site in this
    repository, so a change to a case updates a data row, not the logic;
  * the interaction steps - tests/board/threadx-rk3568-sdio/steps.tsv, the same
    file the host runner sends from.

Two backstops run on top of the case verdicts, because a case that reports PASS
while its own evidence says otherwise is exactly what a judgment channel exists
to catch: a passing enum must have named a card with at least one I/O function,
and a passing datapath must have shown the two block reads agreeing.
"""

import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))

import board_judge as bj  # noqa: E402

SUITE = "threadx-rk3568-sdio"

#: Case names in execution order (data row; the image table and suite.mk carry
#: the same list and the build reconciles the three).
CASES = [
    "shell-up",
    "sdio-enum",
    "sdio-datapath",
    "sdio-pump",
]

#: Anchors a passing case must have left behind (regex, matched on the
#: stamp-stripped transcript). Emitting sites:
#:   shell-up       ports/cherrysh/shell_port.c (the REPL prompt)
#:   sdio-enum      the case's own lines (tests/board/threadx-rk3568-sdio/
#:                  tests/sdio_enum.c), whose identity values come from the
#:                  card's CIS through the imported stack
#:   sdio-datapath  the case's own lines (tests/.../tests/sdio_datapath.c)
#:   sdio-pump      the case's own lines (tests/.../tests/sdio_pump.c)
ANCHORS = {
    "shell-up": [r"user@rk3568:/\$"],
    "sdio-enum": [
        r"sdio: card [0-9a-f]+:[0-9a-f]+, \d+ IO func\(s\), "
        r"CCCR v\d+\.\d+, bus \d+ kHz \((?:HS|DS)\)",
        r"sdio: func1 enabled ioex=0x[0-9a-f]{2}",
        r"sdio: func1 block size set=\d+ read=\d+",
        r"sdio: func1 fbr write \d+ read=\d+ restore \d+ read=\d+",
        r"sdio: SDIO READY",
    ],
    "sdio-datapath": [
        r"sdio: fbr read twice identical \(\d+ bytes\)",
        r"sdio: fbr cmd52 and cmd53 agree first=[0-9a-f]{8}",
        r"sdio: cis block read twice identical \(\d+ bytes\)",
        r"sdio: fbr 2-byte write read=\d+ restore read=\d+",
        r"sdio: pump quiet pending=0x[0-9a-f]{2}",
    ],
    "sdio-pump": [
        r"sdio: pump \d+ polls quiet",
        r"sdio: pump done, cccr=0x[0-9a-f]{2}",
    ],
}

HERE = os.path.dirname(os.path.abspath(__file__))
STEPS_PATH = os.path.join(HERE, "steps.tsv")

# --------------------------------------------------------------------------
# selftest fixture: one legal transcript plus the mutations that must go red.
# --------------------------------------------------------------------------

# The console paints its prompt, so every prompt occurrence carries escape
# sequences (a plain "user@rk3568:/$" never appears on the wire).
PROMPT = "\x1b[1;32muser\x1b[m@\x1b[1;32mrk3568\x1b[m:\x1b[1;34m/\x1b[m$ \x1b[m"

SENTINEL_LINE = (
    "[  22.360] BOARD-SUITE threadx-rk3568-sdio "
    "result=pass cases=4 ok=4 exit=0\r\n"
)

BASE_TRANSCRIPT = (
    "threadx-rk3568-sdio bring-up selftest-tag\r\n"
    "[   5.000] BOARD-SUITE threadx-rk3568-sdio select window_ms=5000\r\n"
    "[  10.000] BOARD-SUITE threadx-rk3568-sdio start cases=4 sel=all "
    "list=shell-up,sdio-enum,sdio-datapath,sdio-pump\r\n"
    + PROMPT
    + "[  10.010] CASE threadx-rk3568-sdio/shell-up PASS\r\n"
    "[  10.500] sdio: card 24c:f179, 1 IO func(s), CCCR v0.2, "
    "bus 50000 kHz (HS)\r\n"
    "[  10.510] sdio: identity sdio_rev=3 cccr_rev=2 ocr=0xffffff "
    "width=4 fn0_max_block=8 memory=0 multi_block=1\r\n"
    "[  10.520] sdio: common cis@0x1000\r\n"
    "[  10.530] sdio: cis bytes 20 01 01 02\r\n"
    "[  10.540] sdio: func1 enabled ioex=0x02\r\n"
    "[  10.550] sdio: func1 block size set=512 read=512\r\n"
    "[  10.560] sdio: func1 fbr write 256 read=256 restore 512 read=512\r\n"
    "[  10.570] sdio: SDIO READY\r\n"
    "[  10.580] CASE threadx-rk3568-sdio/sdio-enum PASS\r\n"
    "[  11.000] sdio: fbr read twice identical (8 bytes)\r\n"
    "[  11.100] sdio: fbr cmd52 and cmd53 agree first=00010000\r\n"
    "[  11.200] sdio: func1 cis@0x1000\r\n"
    "[  11.300] sdio: cis block read twice identical (512 bytes)\r\n"
    "[  11.400] sdio: fbr 2-byte write read=256 restore read=512\r\n"
    "[  11.500] sdio: pump quiet pending=0x00\r\n"
    "[  11.600] CASE threadx-rk3568-sdio/sdio-datapath PASS\r\n"
    "[  12.000] sdio: pump 16 polls quiet\r\n"
    "[  12.100] sdio: pump done, cccr=0x32\r\n"
    "[  12.200] CASE threadx-rk3568-sdio/sdio-pump PASS\r\n"
    + SENTINEL_LINE
    + PROMPT + "==> help\r\n"
    + PROMPT + "help\x1b[0J\r\n"
    "total function 9\r\n"
    "  exit   -> /sbin - leave the shell\r\n"
    "\r\n"
    "total variable 2\r\n"
    "  $ZERO r-   1\r\n"
    + PROMPT + "==> sdio status\r\n"
    + PROMPT + "sdio status\x1b[0J\r\n"
    "sdio: ready=1 card=1 cccr=0x32 ioex=0x02\r\n"
)

EMPTY_SELECTION_TRANSCRIPT = (
    "threadx-rk3568-sdio bring-up selftest-tag\r\n"
    "[   5.000] BOARD-SUITE threadx-rk3568-sdio select window_ms=5000\r\n"
    "[  10.000] BOARD-SUITE threadx-rk3568-sdio start cases=0 sel=skip=* list=\r\n"
    "[  10.010] BOARD-SUITE-note select empty\r\n"
    "[  10.020] BOARD-SUITE threadx-rk3568-sdio result=pass cases=0 ok=0 exit=0\r\n"
)

MUTATIONS = [
    ("drop a CASE line", BASE_TRANSCRIPT.replace(
        "CASE threadx-rk3568-sdio/sdio-datapath PASS\r\n", ""), 1),
    ("sentinel counts disagree", BASE_TRANSCRIPT.replace(
        "cases=4 ok=4 exit=0", "cases=4 ok=3 exit=0"), 1),
    ("sentinel deleted", BASE_TRANSCRIPT.replace(SENTINEL_LINE, ""), 1),
    ("foreign case injected", BASE_TRANSCRIPT.replace(
        "CASE threadx-rk3568-sdio/sdio-pump PASS",
        "CASE threadx-rk3568-sdio/zzz PASS\r\n"
        "CASE threadx-rk3568-sdio/sdio-pump PASS"), 1),
    ("identity anchor dropped", BASE_TRANSCRIPT.replace(
        "sdio: card 24c:f179, 1 IO func(s), CCCR v0.2, bus 50000 kHz (HS)\r\n",
        ""), 1),
    ("ready anchor dropped", BASE_TRANSCRIPT.replace(
        "sdio: SDIO READY\r\n", ""), 1),
    ("block size evidence dropped", BASE_TRANSCRIPT.replace(
        "sdio: func1 block size set=512 read=512\r\n", ""), 1),
    ("enable anchor dropped", BASE_TRANSCRIPT.replace(
        "sdio: func1 enabled ioex=0x02\r\n", ""), 1),
    ("register read anchor dropped", BASE_TRANSCRIPT.replace(
        "sdio: fbr read twice identical (8 bytes)\r\n", ""), 1),
    ("cross-path agreement dropped", BASE_TRANSCRIPT.replace(
        "sdio: fbr cmd52 and cmd53 agree first=00010000\r\n", ""), 1),
    ("cis repeat anchor dropped", BASE_TRANSCRIPT.replace(
        "sdio: cis block read twice identical (512 bytes)\r\n", ""), 1),
    ("two-byte write anchor dropped", BASE_TRANSCRIPT.replace(
        "sdio: fbr 2-byte write read=256 restore read=512\r\n", ""), 1),
    ("pump quiet anchor dropped", BASE_TRANSCRIPT.replace(
        "sdio: pump 16 polls quiet\r\n", ""), 1),
    ("step response dropped", BASE_TRANSCRIPT.replace(
        "total variable 2\r\n", ""), 1),
    ("start list disagrees", BASE_TRANSCRIPT.replace(
        "list=shell-up,sdio-enum,sdio-datapath,sdio-pump",
        "list=shell-up,sdio-enum"), 1),
    ("empty selection", EMPTY_SELECTION_TRANSCRIPT, 2),
]

#: The enum backstop: a PASS must have named a card with at least one I/O
#: function. A transcript whose identity line reports zero functions is not an
#: enumeration, whatever its CASE line says.
IDENTITY_RE = re.compile(
    r"sdio: card [0-9a-f]+:[0-9a-f]+, (\d+) IO func\(s\)"
)

#: The datapath backstop: the two block reads agreeing is the case's whole
#: claim, so a PASS without that line is refused.
CIS_REPEAT_RE = re.compile(r"sdio: cis block read twice identical")


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
            if not transcript.anchor_lines(pattern) and not transcript.rx_has(pattern):
                errors.append("case %s: anchor missing: %s" % (name, pattern))

    # the enum backstop: the identity line must name a card with at least one
    # I/O function, and the case must have reported the ready verdict
    if verdicts.get("sdio-enum") == "PASS":
        funcs = None
        for line in transcript.lines:
            m = IDENTITY_RE.search(line)
            if m:
                funcs = int(m.group(1))
        if funcs is None:
            errors.append("sdio-enum: no identity line (the case did not report the card)")
        elif funcs == 0:
            errors.append("sdio-enum: the identity line reports zero I/O functions")

    # the datapath backstop: the case's whole claim is the two reads agreeing
    if verdicts.get("sdio-datapath") == "PASS":
        if not any(CIS_REPEAT_RE.search(line) for line in transcript.lines):
            errors.append("sdio-datapath: no CIS repeat-read line")

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
    sys.exit(bj.main(judge_text, sys.argv[1:], STEPS_PATH, None,
                     BASE_TRANSCRIPT, MUTATIONS))
