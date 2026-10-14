#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Judge for the ThreadX board eMMC suite (threadx-rk3568-emmc).

Input: one serial transcript. Output: the judgment block; exit 0 = pass,
1 = fail (a missing sentinel counts as incomplete), 2 = configuration error
(the declaration and the transcript disagree about what should have run).

Truth sources, all independent of the transcript's own claims:
  * the case list below - reconciled at build time against the image's
    BOARD_TESTS section and suite.mk (tests/board/common/check-case-list.sh);
  * the anchor table below - each row names the emitting site in this
    repository, so a change to a case updates a data row, not the logic;
  * the interaction steps - tests/board/threadx-rk3568-emmc/steps.tsv, the same
    file the host runner sends from.

Backstops, because a case that reports PASS while its own evidence says otherwise
is exactly what a judgment channel exists to catch:
  * a passing emmc-probe must have come up in high speed over eight lines at the
    50 MHz slot's rate, must have block-count commands enabled, and must have
    left no controller error line behind it - the bring-up's clean ladder is the
    claim this round makes about the controller;
  * a passing emmc-block must have issued block-count commands and must have
    recorded an SDMA boundary event: the boundary-crossing transfer is the only
    witness that the continuation path works at all;
  * a passing emmc-fs must have cleared this round's throughput floors in both
    directions and must have moved at least the file's worth of bytes each way
    (a run that wrote and read a gigabyte cannot have moved less).
"""

import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))

import board_judge as bj  # noqa: E402

SUITE = "threadx-rk3568-emmc"

#: Case names in execution order (data row; the image table and suite.mk carry
#: the same list and the build reconciles the three).
CASES = [
    "shell-up",
    "emmc-probe",
    "emmc-block",
    "emmc-fs",
    "emmc-fs-verify",
    "emmc-bw",
]

#: Anchors a passing case must have left behind (regex, matched on the
#: stamp-stripped transcript). Emitting sites:
#:   shell-up    ports/cherrysh/shell_port.c (the REPL prompt)
#:   emmc-probe  the case's own lines
#:               (tests/board/threadx-rk3568-emmc/tests/emmc_probe.c) over the
#:               port's card service (ports/fsl_sdmmc/sdmmc_mmc_card.c) and the
#:               board driver's record (board/rk3568-itx/drivers/dwc_mshc.c)
#:   emmc-block  the case's own lines (.../tests/emmc_block.c) over the
#:               block-device seam (ports/fatfs/blkdev.c) and the board
#:               driver's census
#:   emmc-fs / emmc-fs-verify / emmc-bw
#:               the cases' own lines over the storage stack (ports/fatfs/)
ANCHORS = {
    "shell-up": [r"user@rk3568:/\$"],
    "emmc-probe": [
        r"emmc: \S+ manufacturer=\d+ serial=\d+ blocks=\d+ block=512",
        r"emmc: mode HS bus 8-bit clock=\d+MHz",
        r"emmc: cru cclk_emmc sel=\d+ dll=bypass",
        r"emmc: cmd23 on",
        r"emmc-probe: transfers=\d+ errors=\d+",
    ],
    "emmc-block": [
        r"emmc-block: write bytes=\d+ ms=\d+ kib=\d+",
        r"emmc-block: read bytes=\d+ ms=\d+ kib=\d+",
        r"emmc-block: verify ok checksum=[0-9a-f]{8}",
        r"emmc-block: cmd23 issued=\d+",
        r"emmc-block: boundary-crossed lba=\d+ len=\d+ events=\d+",
    ],
    "emmc-fs": [
        r"emmc-fs: bound emmc0 blocks=\d+ sector=\d+",
        r"emmc-fs: formatted exFAT sector=4096",
        r"emmc-fs: mounted type=exFAT sector=4096",
        r"emmc-fs: write bytes=\d+ ms=\d+ kib=\d+",
        r"emmc-fs: read bytes=\d+ ms=\d+ kib=\d+",
        r"emmc-fs: verify ok checksum=[0-9a-f]{8}",
        r"emmc-fs: conservation tx=\d+ rx=\d+ boundaries=\d+",
    ],
    "emmc-fs-verify": [
        r"emmc-fs-verify: mounted type=exFAT sector=4096",
        r"emmc-fs-verify: read bytes=\d+ ms=\d+ kib=\d+",
        r"emmc-fs-verify: verify ok checksum=[0-9a-f]{8}",
    ],
    "emmc-bw": [
        r"emmc-bw: mode HS bus 8-bit clock=\d+MHz",
        r"emmc-bw: write bytes=\d+ ms=\d+ kib=\d+",
        r"emmc-bw: read bytes=\d+ ms=\d+ kib=\d+",
        r"emmc-bw: reference linux-52mhz-read=\d+kib",
    ],
}

#: The clock the probe must have come up at: the CRU's 50 MHz slot, in MHz, as
#: the case reports it. The bus mode's timing is named by the card's own timing
#: switch, so the line saying "HS" is the claim, and this is the number.
PROBE_CLOCK_MHZ = 50

#: The CRU selector the probe must have used (DWC_MSHC_CRU_CCLK_SEL_50M).
PROBE_CCLK_SEL = 4

#: The controller error lines that must not appear before the probe's verdict: a
#: bring-up that logged a command or data failure and then reported PASS is
#: exactly the report this backstop refuses.
CONTROLLER_ERROR_RE = re.compile(
    r"dwc_mshc: .*(failed|timeout|error|refused|stuck|not stabilised|not word-aligned)"
)

#: The mode line's clock field and the cmd23 line, captured for the backstops.
PROBE_MODE_RE = re.compile(r"emmc: mode HS bus 8-bit clock=(\d+)MHz")
PROBE_CMD23_RE = re.compile(r"emmc: cmd23 (on|off)")

#: The probe's own counters (transfers, errors).
PROBE_COUNTERS_RE = re.compile(r"emmc-probe: transfers=(\d+) errors=(\d+)")

#: The block case's counters.
BLOCK_CMD23_RE = re.compile(r"emmc-block: cmd23 issued=(\d+)")
BLOCK_BOUNDARY_RE = re.compile(r"emmc-block: boundary-crossed lba=\d+ len=\d+ events=(\d+)")

#: The file case's throughput lines and conservation line.
FS_WRITE_KIB_RE = re.compile(r"emmc-fs: write bytes=\d+ ms=\d+ kib=(\d+)")
FS_READ_KIB_RE = re.compile(r"emmc-fs: read bytes=\d+ ms=\d+ kib=(\d+)")
FS_CONSERVATION_RE = re.compile(r"emmc-fs: conservation tx=(\d+) rx=(\d+) boundaries=\d+")

#: This round's floors, in KiB/s (the case asserts the same numbers; this
#: refuses a PASS that reports less). They are a third of the bus's raw ceiling
#: (50 MHz over eight lines): a working path cannot miss them, and a path that
#: silently degraded is caught.
FS_MIN_READ_KIB = 16384
FS_MIN_WRITE_KIB = 8192

#: The file the conservation line is about.
FS_BYTES = 1 << 30

HERE = os.path.dirname(os.path.abspath(__file__))
STEPS_PATH = os.path.join(HERE, "steps.tsv")

# --------------------------------------------------------------------------
# selftest fixture: one legal transcript plus the mutations that must go red.
# --------------------------------------------------------------------------

# The console paints its prompt, so every prompt occurrence carries escape
# sequences (a plain "user@rk3568:/$" never appears on the wire).
PROMPT = "\x1b[1;32muser\x1b[m@\x1b[1;32mrk3568\x1b[m:\x1b[1;34m/\x1b[m$ \x1b[m"

SENTINEL_LINE = (
    "[  62.360] BOARD-SUITE threadx-rk3568-emmc "
    "result=pass cases=6 ok=6 exit=0\r\n"
)

BASE_TRANSCRIPT = (
    "threadx-rk3568-emmc bring-up selftest-tag\r\n"
    "[   5.000] BOARD-SUITE threadx-rk3568-emmc select window_ms=5000\r\n"
    "[  10.000] BOARD-SUITE threadx-rk3568-emmc start cases=6 sel=all "
    "list=shell-up,emmc-probe,emmc-block,emmc-fs,emmc-fs-verify,emmc-bw\r\n"
    + PROMPT
    + "[  10.010] CASE threadx-rk3568-emmc/shell-up PASS\r\n"
    "[  10.020] emmc: ARJ11X manufacturer=112 serial=1234567890 blocks=61071360 block=512\r\n"
    "[  10.030] emmc: mode HS bus 8-bit clock=50MHz\r\n"
    "[  10.040] emmc: cru cclk_emmc sel=4 dll=bypass\r\n"
    "[  10.050] emmc: cmd23 on\r\n"
    "[  10.060] emmc-probe: transfers=41 errors=0\r\n"
    "[  10.070] CASE threadx-rk3568-emmc/emmc-probe PASS\r\n"
    "[  10.080] emmc-block: write bytes=8388608 ms=350 kib=23967\r\n"
    "[  10.090] emmc-block: read bytes=8388608 ms=200 kib=41943\r\n"
    "[  10.100] emmc-block: verify ok checksum=1a2b3c4d\r\n"
    "[  10.110] emmc-block: cmd23 issued=258\r\n"
    "[  10.120] emmc-block: boundary-crossed lba=60965888 len=8192 events=1\r\n"
    "[  10.130] CASE threadx-rk3568-emmc/emmc-block PASS\r\n"
    "[  10.140] emmc-fs: bound emmc0 blocks=61071360 sector=512\r\n"
    "[  10.150] emmc-fs: formatted exFAT sector=4096\r\n"
    "[  10.160] emmc-fs: mounted type=exFAT sector=4096\r\n"
    "[  10.170] emmc-fs: write bytes=1073741824 ms=51200 kib=20971\r\n"
    "[  10.180] emmc-fs: read bytes=1073741824 ms=25600 kib=41943\r\n"
    "[  10.190] emmc-fs: verify ok checksum=5f3a91c2\r\n"
    "[  10.200] emmc-fs: conservation tx=1078198272 rx=1078198272 boundaries=37\r\n"
    "[  10.210] CASE threadx-rk3568-emmc/emmc-fs PASS\r\n"
    "[  10.220] emmc-fs-verify: mounted type=exFAT sector=4096\r\n"
    "[  10.230] emmc-fs-verify: read bytes=1073741824 ms=25600 kib=41943\r\n"
    "[  10.240] emmc-fs-verify: verify ok checksum=5f3a91c2\r\n"
    "[  10.250] CASE threadx-rk3568-emmc/emmc-fs-verify PASS\r\n"
    "[  10.260] emmc-bw: mode HS bus 8-bit clock=50MHz\r\n"
    "[  10.270] emmc-bw: write bytes=268435456 ms=12800 kib=20971\r\n"
    "[  10.280] emmc-bw: read bytes=268435456 ms=6400 kib=41943\r\n"
    "[  10.290] emmc-bw: reference linux-52mhz-read=44749kib\r\n"
    "[  10.300] CASE threadx-rk3568-emmc/emmc-bw PASS\r\n"
    + SENTINEL_LINE
    + PROMPT + "==> help\r\n"
    + PROMPT + "help\x1b[0J\r\n"
    "total function 10\r\n"
    "  exit   -> /sbin - leave the shell\r\n"
    "\r\n"
    "total variable 2\r\n"
    "  $ZERO r-   1\r\n"
    + PROMPT + "==> stor info\r\n"
    + PROMPT + "stor info\x1b[0J\r\n"
    "stor: volume 0, device emmc0, sector 4096\r\n"
    "stor: mounted, exFAT, 56985 MB total, 56072 MB free\r\n"
)

EMPTY_SELECTION_TRANSCRIPT = (
    "threadx-rk3568-emmc bring-up selftest-tag\r\n"
    "[   5.000] BOARD-SUITE threadx-rk3568-emmc select window_ms=5000\r\n"
    "[  10.000] BOARD-SUITE threadx-rk3568-emmc start cases=0 sel=skip=* list=\r\n"
    "[  10.010] BOARD-SUITE-note select empty\r\n"
    "[  10.020] BOARD-SUITE threadx-rk3568-emmc result=pass cases=0 ok=0 exit=0\r\n"
)

MUTATIONS = [
    ("drop a CASE line", BASE_TRANSCRIPT.replace(
        "CASE threadx-rk3568-emmc/emmc-probe PASS\r\n", ""), 1),
    ("sentinel counts disagree", BASE_TRANSCRIPT.replace(
        "cases=6 ok=6 exit=0", "cases=6 ok=5 exit=0"), 1),
    ("sentinel deleted", BASE_TRANSCRIPT.replace(SENTINEL_LINE, ""), 1),
    ("foreign case injected", BASE_TRANSCRIPT.replace(
        "CASE threadx-rk3568-emmc/emmc-probe PASS",
        "CASE threadx-rk3568-emmc/zzz PASS\r\n"
        "CASE threadx-rk3568-emmc/emmc-probe PASS"), 1),
    ("identity anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc: ARJ11X manufacturer=112 serial=1234567890 blocks=61071360 block=512\r\n", ""), 1),
    ("mode anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc: mode HS bus 8-bit clock=50MHz\r\n", ""), 1),
    ("cru anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc: cru cclk_emmc sel=4 dll=bypass\r\n", ""), 1),
    ("cmd23 anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc: cmd23 on\r\n", ""), 1),
    ("probe counters anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-probe: transfers=41 errors=0\r\n", ""), 1),
    ("probe at the wrong clock (backstop)", BASE_TRANSCRIPT.replace(
        "emmc: mode HS bus 8-bit clock=50MHz\r\n",
        "emmc: mode HS bus 8-bit clock=25MHz\r\n"), 1),
    ("probe on the wrong CRU slot (backstop)", BASE_TRANSCRIPT.replace(
        "emmc: cru cclk_emmc sel=4 dll=bypass\r\n",
        "emmc: cru cclk_emmc sel=3 dll=bypass\r\n"), 1),
    ("probe with cmd23 off (backstop)", BASE_TRANSCRIPT.replace(
        "emmc: cmd23 on\r\n", "emmc: cmd23 off\r\n"), 1),
    ("probe with a controller error line (backstop)", BASE_TRANSCRIPT.replace(
        "emmc-probe: transfers=41 errors=0\r\n",
        "dwc_mshc: CMD1 failed (eint=0x1 pstate=0x0)\r\n"
        "emmc-probe: transfers=41 errors=0\r\n"), 1),
    ("probe reports errors (backstop)", BASE_TRANSCRIPT.replace(
        "emmc-probe: transfers=41 errors=0\r\n",
        "emmc-probe: transfers=41 errors=3\r\n"), 1),
    ("block write anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-block: write bytes=8388608 ms=350 kib=23967\r\n", ""), 1),
    ("block read anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-block: read bytes=8388608 ms=200 kib=41943\r\n", ""), 1),
    ("block verify anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-block: verify ok checksum=1a2b3c4d\r\n", ""), 1),
    ("block cmd23 anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-block: cmd23 issued=258\r\n", ""), 1),
    ("block boundary anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-block: boundary-crossed lba=60965888 len=8192 events=1\r\n", ""), 1),
    ("block with no cmd23 (backstop)", BASE_TRANSCRIPT.replace(
        "emmc-block: cmd23 issued=258\r\n", "emmc-block: cmd23 issued=0\r\n"), 1),
    ("block with no boundary event (backstop)", BASE_TRANSCRIPT.replace(
        "emmc-block: boundary-crossed lba=60965888 len=8192 events=1\r\n",
        "emmc-block: boundary-crossed lba=60965888 len=8192 events=0\r\n"), 1),
    ("fs bound anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-fs: bound emmc0 blocks=61071360 sector=512\r\n", ""), 1),
    ("fs formatted anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-fs: formatted exFAT sector=4096\r\n", ""), 1),
    ("fs mounted anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-fs: mounted type=exFAT sector=4096\r\n", ""), 1),
    ("fs write throughput anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-fs: write bytes=1073741824 ms=51200 kib=20971\r\n", ""), 1),
    ("fs read throughput anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-fs: read bytes=1073741824 ms=25600 kib=41943\r\n", ""), 1),
    ("fs verify ok anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-fs: verify ok checksum=5f3a91c2\r\n", ""), 1),
    ("fs conservation anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-fs: conservation tx=1078198272 rx=1078198272 boundaries=37\r\n", ""), 1),
    ("fs write below floor (backstop)", BASE_TRANSCRIPT.replace(
        "emmc-fs: write bytes=1073741824 ms=51200 kib=20971\r\n",
        "emmc-fs: write bytes=1073741824 ms=1048576 kib=1024\r\n"), 1),
    ("fs read below floor (backstop)", BASE_TRANSCRIPT.replace(
        "emmc-fs: read bytes=1073741824 ms=25600 kib=41943\r\n",
        "emmc-fs: read bytes=1073741824 ms=1048576 kib=1024\r\n"), 1),
    ("fs conservation moved too little (backstop)", BASE_TRANSCRIPT.replace(
        "emmc-fs: conservation tx=1078198272 rx=1078198272 boundaries=37\r\n",
        "emmc-fs: conservation tx=1048576 rx=1048576 boundaries=1\r\n"), 1),
    ("verify mounted anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-fs-verify: mounted type=exFAT sector=4096\r\n", ""), 1),
    ("verify read anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-fs-verify: read bytes=1073741824 ms=25600 kib=41943\r\n", ""), 1),
    ("verify ok anchor dropped (verify case)", BASE_TRANSCRIPT.replace(
        "emmc-fs-verify: verify ok checksum=5f3a91c2\r\n", ""), 1),
    ("bw mode anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-bw: mode HS bus 8-bit clock=50MHz\r\n", ""), 1),
    ("bw write anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-bw: write bytes=268435456 ms=12800 kib=20971\r\n", ""), 1),
    ("bw read anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-bw: read bytes=268435456 ms=6400 kib=41943\r\n", ""), 1),
    ("bw reference anchor dropped", BASE_TRANSCRIPT.replace(
        "emmc-bw: reference linux-52mhz-read=44749kib\r\n", ""), 1),
    ("step response dropped", BASE_TRANSCRIPT.replace(
        "total variable 2\r\n", ""), 1),
    ("stor step response dropped", BASE_TRANSCRIPT.replace(
        "stor: volume 0, device emmc0, sector 4096\r\n", ""), 1),
    ("start list disagrees", BASE_TRANSCRIPT.replace(
        "list=shell-up,emmc-probe,emmc-block,emmc-fs,emmc-fs-verify,emmc-bw",
        "list=shell-up,emmc-probe"), 1),
    ("empty selection", EMPTY_SELECTION_TRANSCRIPT, 2),
]


def _last_match(transcript, pattern):
    """The last line matching `pattern`, or None (the case prints one such line
    per run, so the last one is the run's word)."""
    found = None
    for line in transcript.lines:
        match = re.search(pattern, line)
        if match:
            found = match
    return found


def _probe_prefix(transcript):
    """The transcript's lines up to (and including) the probe's verdict.

    The controller-error backstop is scoped to the bring-up: the probe is the
    case that claims a clean ladder, and a later case's own failure is already
    its verdict's business.
    """
    prefix = []
    for line in transcript.lines:
        prefix.append(line)
        if re.search(r"(?:^|\s)CASE " + re.escape(SUITE) + r"/emmc-probe (PASS|FAIL)$",
                     line.strip()):
            break
    return prefix


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

    # the bring-up backstop: the mode, the clock chain, the write path's shape,
    # and no controller error line before the probe's own verdict.
    if verdicts.get("emmc-probe") == "PASS":
        mode = _last_match(transcript, PROBE_MODE_RE)
        if mode is None:
            errors.append("emmc-probe: no bus-mode line")
        elif int(mode.group(1)) != PROBE_CLOCK_MHZ:
            errors.append("emmc-probe: the reported bus clock is %s MHz, not %d"
                          % (mode.group(1), PROBE_CLOCK_MHZ))

        sel = _last_match(transcript, r"emmc: cru cclk_emmc sel=(\d+) dll=bypass")
        if sel is None:
            errors.append("emmc-probe: no clock-chain line")
        elif int(sel.group(1)) != PROBE_CCLK_SEL:
            errors.append("emmc-probe: the CCLK_EMMC selector is %s, not %d"
                          % (sel.group(1), PROBE_CCLK_SEL))

        cmd23 = _last_match(transcript, PROBE_CMD23_RE)
        if cmd23 is None:
            errors.append("emmc-probe: no block-count line")
        elif cmd23.group(1) != "on":
            errors.append("emmc-probe: block-count commands are off")

        counters = _last_match(transcript, PROBE_COUNTERS_RE)
        if counters is None:
            errors.append("emmc-probe: no controller counters line")
        elif int(counters.group(2)) != 0:
            errors.append("emmc-probe: the controller recorded %s error(s)"
                          % counters.group(2))

        for line in _probe_prefix(transcript):
            if CONTROLLER_ERROR_RE.search(line):
                errors.append("emmc-probe: controller error before the verdict: %s"
                              % line.strip())
                break

    # the block case's backstops: the write path was pre-announced with a block
    # count, and the boundary-crossing transfer really crossed.
    if verdicts.get("emmc-block") == "PASS":
        cmd23 = _last_match(transcript, BLOCK_CMD23_RE)
        if cmd23 is None:
            errors.append("emmc-block: no block-count count line")
        elif int(cmd23.group(1)) < 1:
            errors.append("emmc-block: no block-count command was issued")
        boundary = _last_match(transcript, BLOCK_BOUNDARY_RE)
        if boundary is None:
            errors.append("emmc-block: no boundary-crossing line")
        elif int(boundary.group(1)) < 1:
            errors.append("emmc-block: the boundary transfer recorded no event")

    # the file case's backstops: the floors and the conservation of moved bytes.
    if verdicts.get("emmc-fs") == "PASS":
        for phase, pattern, floor in (("write", FS_WRITE_KIB_RE, FS_MIN_WRITE_KIB),
                                      ("read", FS_READ_KIB_RE, FS_MIN_READ_KIB)):
            match = _last_match(transcript, pattern)
            if match is None:
                errors.append("emmc-fs: no %s throughput line" % phase)
            elif int(match.group(1)) < floor:
                errors.append("emmc-fs: the %s throughput %s KiB/s is below %d"
                              % (phase, match.group(1), floor))
        match = _last_match(transcript, FS_CONSERVATION_RE)
        if match is None:
            errors.append("emmc-fs: no conservation line")
        else:
            tx = int(match.group(1))
            rx = int(match.group(2))
            if tx < FS_BYTES or rx < FS_BYTES:
                errors.append("emmc-fs: conservation tx=%d rx=%d (want at least %d each)"
                              % (tx, rx, FS_BYTES))

    # the persistence backstop: a passing verify case must have shown a checksum
    # read-back.
    if verdicts.get("emmc-fs-verify") == "PASS":
        if not transcript.anchor_lines(r"emmc-fs-verify: verify ok checksum=[0-9a-f]{8}"):
            errors.append("emmc-fs-verify: no verify-ok line")

    # the measurement case must have reported a nonzero rate in both directions.
    if verdicts.get("emmc-bw") == "PASS":
        for phase, pattern in (("write", FS_WRITE_KIB_RE), ("read", FS_READ_KIB_RE)):
            pattern = pattern.pattern.replace("emmc-fs:", "emmc-bw:")
            match = _last_match(transcript, pattern)
            if match is None:
                errors.append("emmc-bw: no %s throughput line" % phase)
            elif int(match.group(1)) == 0:
                errors.append("emmc-bw: the %s throughput is zero" % phase)

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
