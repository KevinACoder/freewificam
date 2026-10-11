#!/usr/bin/env python3
"""Judge for the ThreadX board regression suite (threadx-rk3568-regression).

Input: one serial transcript. Output: the judgment block; exit 0 = pass,
1 = fail (a missing sentinel counts as incomplete), 2 = configuration error
(the declaration and the transcript disagree about what should have run).

Truth sources, all independent of the transcript's own claims:
  * the case list below - reconciled at build time against the image's
    BOARD_TESTS section and suite.mk (tests/board/common/check-case-list.sh);
  * the anchor table below - each row names the emitting site in this
    repository, so a kernel pin bump updates a data row, not the logic;
  * the interaction steps - tests/board/common/shell-steps.tsv, the same file
    the host runner sends from.

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

SUITE = "threadx-rk3568-regression"

#: Case names in execution order (data row; the image table and suite.mk carry
#: the same list and the build reconciles the three).
CASES = [
    "shell-up",
    "kernel-up",
    "tick-ladder",
    "thread-sched",
    "cmsis2-prio",
    "tlsf-heap",
    "os-timer",
    "usb-xhci",
    "usb-ehci",
    "lwip-loopback",
    "gmac-up",
]

#: Anchors a passing case must have left behind (regex, matched on the
#: stamp-stripped transcript). Emitting sites:
#:   kernel-up    os/threadx/board/rk3568-itx/tx_glue.c (kernel entry banner)
#:   thread-sched tests/board/threadx-rk3568-regression/tests/thread_sched.c
#:   cmsis2-prio  os/threadx/cmsis2_threadx.c (priority map print)
#:   tlsf-heap    os/threadx/board/rk3568-itx/heap.c (heap ready line)
#:   lwip-loopback tests/board/threadx-rk3568-regression/tests/lwip_smoke.c
#:                 (stack init callback line, loopback echo result line)
#:   gmac-up      tests/board/threadx-rk3568-regression/tests/gmac_dataplane.c
#:                 (PHY identity + link line, MAC address, channel start)
ANCHORS = {
    "shell-up": [r"user@rk3568:/\$"],
    "kernel-up": [
        r"tx: kernel entered version=6\.5\.1\.202602a",
        r"tx: unused_mem=0x",
        r"tick: source=virt irq=27 freq=1000Hz",
        r"stamp: switched to tick",
    ],
    "tick-ladder": [],  # numeric checks below
    "thread-sched": [r"thr\[lo\]: n=(\d+)"],
    "cmsis2-prio": [r"cmsis2: prio 24 -> tx 19"],
    "tlsf-heap": [r"heap: tlsf ready 1048576 bytes at 0x"],
    "os-timer": [r"os2-timer: periodic fires="],
    "usb-xhci": [
        r"usbh: core init bus=3 hcd=xhci",
        r"usbh: class msc claimed lun=0",
        r"usbh: msc read blk0=0x",
        r"usbh: msc write/read roundtrip ok",
    ],
    "usb-ehci": [
        r"usbh: core init bus=1 hcd=ehci",
        r"usbh: class msc claimed lun=0",
        r"usbh: msc read blk0=0x",
        r"usbh: msc write/read roundtrip ok",
    ],
    "lwip-loopback": [
        r"net: READY tcpip=up lwip=\d+\.\d+\.\d+",
        r"net: loopback echo ok port=7777 sent=32 got=32",
    ],
    "gmac-up": [
        r"phy1: id 0x001c:0xc916 at mdio \d+",
        r"phy1: link up, 1000 Mbps full duplex",
        r"gmac1: channel start",
        r"net: up gmac1 ip=192\.168\.0\.200/24 mac=02:e4:a5:35:68:01",
        r"gmac1: dataplane up",
    ],
}

LADDER_RE = re.compile(r"tick: t=(\d+)")
LADDER_BEATS = 3
LADDER_STEP = 1000
# A 1000-tick sleep returns on the tick after the interval elapsed, so the
# measured delta is 1000..1002 ticks (the bring-up smoke measured 1001:
# t=1000/2001/3002), while the wall-clock stamps differ by 1000 +/- 10 ms.
LADDER_SLACK = 2
STAMP_TOLERANCE_MS = 10

HERE = os.path.dirname(os.path.abspath(__file__))
STEPS_PATH = os.path.join(HERE, "steps.tsv")
VECTORS_PATH = os.path.join(HERE, "..", "common", "select-vectors.tsv")

# --------------------------------------------------------------------------
# selftest fixture: one legal transcript plus the mutations that must go red.
# The shape mirrors a real run: stamped log lines, a prompt fragment with the
# transmitted marker injected inline, the six CASE lines, the sentinel.
# --------------------------------------------------------------------------

# The console paints its prompt, so every prompt occurrence carries escape
# sequences (a plain "user@rk3568:/$" never appears on the wire).
PROMPT = "\x1b[1;32muser\x1b[m@\x1b[1;32mrk3568\x1b[m:\x1b[1;34m/\x1b[m$ \x1b[m"

SENTINEL_LINE = (
    "[  12.360] BOARD-SUITE threadx-rk3568-regression "
    "result=pass cases=11 ok=11 exit=0\r\n"
)

BASE_TRANSCRIPT = (
    "threadx-rk3568-regression bring-up selftest-tag\r\n"
    "gicv3: ready ctlr=00000012 waker=00000000\r\n"
    "[   0.000] tx: kernel entered version=6.5.1.202602a\r\n"
    "[   0.000] tx: unused_mem=0x0\r\n"
    "[   0.000] tick: source=virt irq=27 freq=1000Hz\r\n"
    "[   0.000] stamp: switched to tick\r\n"
    "[   0.001] heap: tlsf ready 1048576 bytes at 0xa026730\r\n"
    "[   5.000] BOARD-SUITE threadx-rk3568-regression select window_ms=5000\r\n"
    "[  10.000] BOARD-SUITE threadx-rk3568-regression start cases=11 sel=all "
    "list=shell-up,kernel-up,tick-ladder,thread-sched,cmsis2-prio,tlsf-heap,"
    "os-timer,usb-xhci,usb-ehci,lwip-loopback,gmac-up\r\n"
    + PROMPT
    + "[  10.010] cmsis2: prio 24 -> tx 19\r\n"
    "[  10.020] CASE threadx-rk3568-regression/shell-up PASS\r\n"
    "[  10.030] CASE threadx-rk3568-regression/kernel-up PASS\r\n"
    "[  10.040] tick: t=11000\r\n"
    "[  11.041] tick: t=12000\r\n"
    "[  12.042] tick: t=13000\r\n"
    "[  12.050] CASE threadx-rk3568-regression/tick-ladder PASS\r\n"
    "[  12.060] thr[lo]: n=1\r\n"
    "[  12.100] CASE threadx-rk3568-regression/thread-sched PASS\r\n"
    "[  12.110] cmsis2: prio 24 -> tx 19\r\n"
    "[  12.120] CASE threadx-rk3568-regression/cmsis2-prio PASS\r\n"
    "[  12.130] CASE threadx-rk3568-regression/tlsf-heap PASS\r\n"
    "[  12.140] os2-timer: periodic fires=6 one-shot fires=1\r\n"
    "[  12.150] CASE threadx-rk3568-regression/os-timer PASS\r\n"
    "[  12.160] usbh: core init bus=3 hcd=xhci base=0xfcc00000\r\n"
    "[  12.170] usbh: class msc claimed lun=0\r\n"
    "[  12.180] usbh: msc read blk0=0x00000000 ok\r\n"
    "[  12.190] usbh: msc write/read roundtrip ok at lba=1\r\n"
    "[  12.200] CASE threadx-rk3568-regression/usb-xhci PASS\r\n"
    "[  12.210] usbh: core init bus=1 hcd=ehci base=0xfd880000\r\n"
    "[  12.220] usbh: class msc claimed lun=0\r\n"
    "[  12.230] usbh: msc read blk0=0x00000000 ok\r\n"
    "[  12.240] usbh: msc write/read roundtrip ok at lba=1\r\n"
    "[  12.250] CASE threadx-rk3568-regression/usb-ehci PASS\r\n"
    "[  12.255] net: READY tcpip=up lwip=2.2.1\r\n"
    "[  12.258] net: loopback echo ok port=7777 sent=32 got=32\r\n"
    "[  12.260] CASE threadx-rk3568-regression/lwip-loopback PASS\r\n"
    "[  12.265] gmac1: mac version 0x3051 at 0xfe010000 irq 64\r\n"
    "[  12.270] phy1: id 0x001c:0xc916 at mdio 1\r\n"
    "[  12.275] phy1: strapped RGMII tx delay already clear (mdio 1, d08.0x11 0x0009)\r\n"
    "[  13.280] phy1: link up, 1000 Mbps full duplex\r\n"
    "[  13.285] gmac1: link 1000 Mbps full duplex\r\n"
    "[  13.290] gmac1: channel start\r\n"
    "[  13.292] lwiperf: tcp server start\r\n"
    "[  13.295] net: up gmac1 ip=192.168.0.200/24 mac=02:e4:a5:35:68:01\r\n"
    "[  13.300] gmac1: dataplane up\r\n"
    "[  13.310] CASE threadx-rk3568-regression/gmac-up PASS\r\n"
    + SENTINEL_LINE
    + "==> help\r\n"
    + PROMPT + "help\x1b[0J\r\n"
    "total function 8\r\n"
    "  exit   -> /sbin - leave the shell\r\n"
    "\r\n"
    "total variable 2\r\n"
    "  $ZERO r-   1\r\n"
    + PROMPT + "==> cl\r\n"
    + PROMPT + "c" + PROMPT + "cl==> \t\r\n"
    + PROMPT + "clear\x1b[0J\r\n"
    "\x1b[2J\x1b[H" + PROMPT + "==> \r\n"
    + PROMPT + "==> exit\r\n"
    + PROMPT + "exit\x1b[0J\r\n"
    + PROMPT + "\x1b[0J\r\n"
)

EMPTY_SELECTION_TRANSCRIPT = (
    "threadx-rk3568-regression bring-up selftest-tag\r\n"
    "[   5.000] BOARD-SUITE threadx-rk3568-regression select window_ms=5000\r\n"
    "[  10.000] BOARD-SUITE threadx-rk3568-regression start cases=0 sel=skip=* list=\r\n"
    "[  10.010] BOARD-SUITE-note select empty\r\n"
    "[  10.020] BOARD-SUITE threadx-rk3568-regression result=pass cases=0 ok=0 exit=0\r\n"
)

MUTATIONS = [
    ("drop a CASE line", BASE_TRANSCRIPT.replace(
        "CASE threadx-rk3568-regression/tlsf-heap PASS\r\n", ""), 1),
    ("sentinel counts disagree", BASE_TRANSCRIPT.replace(
        "cases=11 ok=11 exit=0", "cases=11 ok=10 exit=0"), 1),
    ("sentinel deleted", BASE_TRANSCRIPT.replace(SENTINEL_LINE, ""), 1),
    ("foreign case injected", BASE_TRANSCRIPT.replace(
        "CASE threadx-rk3568-regression/tlsf-heap PASS",
        "CASE threadx-rk3568-regression/zzz PASS\r\n"
        "CASE threadx-rk3568-regression/tlsf-heap PASS"), 1),
    ("anchor dropped (heap ready)", BASE_TRANSCRIPT.replace(
        "heap: tlsf ready 1048576 bytes at 0xa026730\r\n", ""), 1),
    ("anchor dropped (net ready)", BASE_TRANSCRIPT.replace(
        "net: READY tcpip=up lwip=2.2.1\r\n", ""), 1),
    ("anchor dropped (link up)", BASE_TRANSCRIPT.replace(
        "phy1: link up, 1000 Mbps full duplex\r\n", ""), 1),
    ("ladder beat dropped", BASE_TRANSCRIPT.replace("tick: t=12000\r\n", ""), 1),
    ("step response dropped", BASE_TRANSCRIPT.replace("total variable 2\r\n", ""), 1),
    ("start list disagrees", BASE_TRANSCRIPT.replace(
        "list=shell-up,kernel-up,tick-ladder,thread-sched,cmsis2-prio,tlsf-heap,"
        "os-timer,usb-xhci,usb-ehci,lwip-loopback,gmac-up",
        "list=shell-up,kernel-up,tick-ladder,thread-sched,cmsis2-prio,tlsf-heap,"
        "os-timer,usb-xhci,usb-ehci,lwip-loopback"), 1),
    ("empty selection", EMPTY_SELECTION_TRANSCRIPT, 2),
]


def check_ladder(transcript: bj.Transcript) -> list[str]:
    """The three ladder beats: 1000..1002 ticks apart (see LADDER_SLACK), with
    wall-clock stamps 1000 +/- 10 ms apart."""
    errors = []
    beats = []
    for line in transcript.raw_lines:
        match = LADDER_RE.search(line)
        if match:
            beats.append((bj.stamp_ms(line), int(match.group(1))))
    if len(beats) != LADDER_BEATS:
        return ["tick ladder: %d beats (want %d)" % (len(beats), LADDER_BEATS)]
    for index, (stamp, tick) in enumerate(beats):
        if index:
            delta = tick - beats[index - 1][1]
            if delta < LADDER_STEP or delta > LADDER_STEP + LADDER_SLACK:
                errors.append("tick ladder: beat %d delta %d (want 1000..1002)" % (index + 1, delta))
            if stamp is not None and beats[index - 1][0] is not None:
                stamp_delta = stamp - beats[index - 1][0]
                if abs(stamp_delta - LADDER_STEP) > STAMP_TOLERANCE_MS:
                    errors.append(
                        "tick ladder: beat %d stamp delta %d ms (want 1000+/-10)"
                        % (index + 1, stamp_delta)
                    )
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
    # configuration error, not a test failure.  (The prerequisite is never
    # filtered out, so a truly empty list cannot happen.)
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

    if re.search(r"code=17", transcript.raw):
        errors.append("controller completion code=17 (parameter error) in the transcript")

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
    if verdicts.get("tick-ladder") == "PASS":
        errors.extend(check_ladder(transcript))
    if verdicts.get("thread-sched") == "PASS":
        for line in transcript.anchor_lines(r"thr\[lo\]: n=(\d+)"):
            match = re.search(r"thr\[lo\]: n=(\d+)", line)
            if match and int(match.group(1)) < 1:
                errors.append("thread-sched: thr[lo] counter is zero")

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
