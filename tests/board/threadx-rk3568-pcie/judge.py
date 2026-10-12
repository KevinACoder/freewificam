#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Judge for the ThreadX board PCIe/NVMe suite (threadx-rk3568-pcie).

Input: one serial transcript. Output: the judgment block; exit 0 = pass,
1 = fail (a missing sentinel counts as incomplete), 2 = configuration error
(the declaration and the transcript disagree about what should have run).

Truth sources, all independent of the transcript's own claims:
  * the case list below - reconciled at build time against the image's
    BOARD_TESTS section and suite.mk (tests/board/common/check-case-list.sh);
  * the anchor table below - each row names the emitting site in this
    repository, so a change to a case updates a data row, not the logic;
  * the interaction steps - tests/board/threadx-rk3568-pcie/steps.tsv, the same
    file the host runner sends from.

One backstop runs on top of the case verdicts, because a case that reports PASS
while its own evidence says otherwise is exactly what a judgment channel exists
to catch:
  * a passing pcie-nvme must have shown a message interrupt actually delivered
    (the ITS path working) and reported a nonzero delivery count - a polled
    completion alone cannot show that the interrupt arrived;
  * a passing pcie-nvme-fs must have cleared the throughput floor in both the
    write and the read line and must have shown equal, nonzero conservation
    deltas (a run that wrote and read 1 GiB cannot have consumed zero
    completions, and completions and message interrupts cannot disagree);
  * a passing pcie-nvme-fs-verify must have shown a checksum read-back.
"""

import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))

import board_judge as bj  # noqa: E402

SUITE = "threadx-rk3568-pcie"

#: Case names in execution order (data row; the image table and suite.mk carry
#: the same list and the build reconciles the three).
CASES = [
    "shell-up",
    "pcie-nvme",
    "pcie-nvme-fs",
    "pcie-nvme-fs-verify",
]

#: Anchors a passing case must have left behind (regex, matched on the
#: stamp-stripped transcript). Emitting sites:
#:   shell-up    ports/cherrysh/shell_port.c (the REPL prompt)
#:   pcie-nvme   the board drivers' own lines (board/rk3568-itx/drivers/pcie/
#:               dwc_pcie.c: the root complex's link-up and function claim;
#:               board/rk3568-itx/drivers/nvme/dwc_nvme.c: the controller's
#:               identity and namespace geometry and the message-interrupt
#:               delivery line) plus the case's own lines
#:               (tests/board/threadx-rk3568-pcie/tests/pcie_nvme.c).
#:   pcie-nvme-fs / pcie-nvme-fs-verify
#:               the case's own lines
#:               (tests/board/threadx-rk3568-pcie/tests/pcie_nvme_fs.c), over
#:               the storage stack (ports/fatfs/).
#:
#: The class field is four hex digits: dwc_pcie.c prints the base class and the
#: subclass as `%02x%02x`.
ANCHORS = {
    "shell-up": [r"user@rk3568:/\$"],
    "pcie-nvme": [
        r"pcie: ctrl \d+ link up, downstream bus \d+",
        r"pcie: dev [0-9a-f]{2}:[0-9a-f]{2}\.[0-9a-f] "
        r"[0-9a-f]{4}:[0-9a-f]{4} class [0-9a-f]{4}",
        r"nvme\d+: [0-9a-f]{4}:[0-9a-f]{4} at 0x[0-9a-f]+, "
        r"ns\d+: \d+ blocks of \d+ bytes",
        r"nvme\d+: message interrupts delivered \(intid \d+, \d+ so far\)",
        r"pcie-nvme: ctrls=\d+ devs=\d+",
        r"pcie-nvme: ns blocks=\d+ sector=\d+",
        r"pcie-nvme: irqs=\d+",
    ],
    "pcie-nvme-fs": [
        r"pcie-nvme-fs: bound nvme0 blocks=\d+ sector=\d+",
        r"pcie-nvme-fs: formatted exFAT sector=4096",
        r"pcie-nvme-fs: mounted type=exFAT sector=4096",
        r"pcie-nvme-fs: write bytes=\d+ ms=\d+ kib=\d+",
        r"pcie-nvme-fs: read bytes=\d+ ms=\d+ kib=\d+",
        r"pcie-nvme-fs: verify ok checksum=[0-9a-f]{8}",
        r"pcie-nvme-fs: conservation irqs=\d+ completions=\d+",
    ],
    "pcie-nvme-fs-verify": [
        r"pcie-nvme-fs-verify: mounted type=exFAT sector=4096",
        r"pcie-nvme-fs-verify: read bytes=\d+ ms=\d+ kib=\d+",
        r"pcie-nvme-fs-verify: verify ok checksum=[0-9a-f]{8}",
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
    "[  22.360] BOARD-SUITE threadx-rk3568-pcie "
    "result=pass cases=4 ok=4 exit=0\r\n"
)

BASE_TRANSCRIPT = (
    "threadx-rk3568-pcie bring-up selftest-tag\r\n"
    "[   5.000] BOARD-SUITE threadx-rk3568-pcie select window_ms=5000\r\n"
    "[  10.000] BOARD-SUITE threadx-rk3568-pcie start cases=4 sel=all "
    "list=shell-up,pcie-nvme,pcie-nvme-fs,pcie-nvme-fs-verify\r\n"
    + PROMPT
    + "[  10.010] CASE threadx-rk3568-pcie/shell-up PASS\r\n"
    "[  10.020] pcie: ctrl 0 link up, downstream bus 1\r\n"
    "[  10.030] pcie: ctrl 1 link up, downstream bus 2\r\n"
    "[  10.040] pcie: dev 01:00.0 144d:a80a class 0108\r\n"
    "[  10.050] its: 01:00.0 got 1 message irq(s) (event 0 -> intid 8192)\r\n"
    "[  10.060] nvme: 1 message vector(s), completion intid 8192\r\n"
    "[  10.070] nvme0: 144d:a80a at 0xf4300000, ns1: 500118192 blocks of "
    "512 bytes (244198 MiB)\r\n"
    "[  10.080] pcie-nvme: ctrls=2 devs=2\r\n"
    "[  10.090] pcie-nvme: ns blocks=500118192 sector=512\r\n"
    "[  10.100] nvme0: message interrupts delivered (intid 8192, 1 so far)\r\n"
    "[  10.110] pcie-nvme: irqs=1\r\n"
    "[  10.120] CASE threadx-rk3568-pcie/pcie-nvme PASS\r\n"
    "[  10.130] pcie-nvme-fs: bound nvme0 blocks=500118192 sector=512\r\n"
    "[  10.140] pcie-nvme-fs: formatted exFAT sector=4096\r\n"
    "[  10.150] pcie-nvme-fs: mounted type=exFAT sector=4096\r\n"
    "[  10.160] pcie-nvme-fs: write bytes=1073741824 ms=131072 kib=8192\r\n"
    "[  10.170] pcie-nvme-fs: read bytes=1073741824 ms=131072 kib=8192\r\n"
    "[  10.180] pcie-nvme-fs: verify ok checksum=1a2b3c4d\r\n"
    "[  10.190] pcie-nvme-fs: conservation irqs=4096 completions=4096\r\n"
    "[  10.200] CASE threadx-rk3568-pcie/pcie-nvme-fs PASS\r\n"
    "[  10.210] pcie-nvme-fs-verify: mounted type=exFAT sector=4096\r\n"
    "[  10.220] pcie-nvme-fs-verify: read bytes=1073741824 ms=131072 kib=8192\r\n"
    "[  10.230] pcie-nvme-fs-verify: verify ok checksum=1a2b3c4d\r\n"
    "[  10.240] CASE threadx-rk3568-pcie/pcie-nvme-fs-verify PASS\r\n"
    + SENTINEL_LINE
    + PROMPT + "==> help\r\n"
    + PROMPT + "help\x1b[0J\r\n"
    "total function 10\r\n"
    "  exit   -> /sbin - leave the shell\r\n"
    "\r\n"
    "total variable 2\r\n"
    "  $ZERO r-   1\r\n"
    + PROMPT + "==> pcie status\r\n"
    + PROMPT + "pcie status\x1b[0J\r\n"
    "pcie: ctrls=2 devs=2 nvme=1 irqs=1\r\n"
    + PROMPT + "==> stor info\r\n"
    + PROMPT + "stor info\x1b[0J\r\n"
    "stor: volume 0, device nvme0, sector 4096\r\n"
    "stor: mounted, exFAT, 476831 MB total, 475807 MB free\r\n"
)

EMPTY_SELECTION_TRANSCRIPT = (
    "threadx-rk3568-pcie bring-up selftest-tag\r\n"
    "[   5.000] BOARD-SUITE threadx-rk3568-pcie select window_ms=5000\r\n"
    "[  10.000] BOARD-SUITE threadx-rk3568-pcie start cases=0 sel=skip=* list=\r\n"
    "[  10.010] BOARD-SUITE-note select empty\r\n"
    "[  10.020] BOARD-SUITE threadx-rk3568-pcie result=pass cases=0 ok=0 exit=0\r\n"
)

MUTATIONS = [
    ("drop a CASE line", BASE_TRANSCRIPT.replace(
        "CASE threadx-rk3568-pcie/pcie-nvme PASS\r\n", ""), 1),
    ("sentinel counts disagree", BASE_TRANSCRIPT.replace(
        "cases=4 ok=4 exit=0", "cases=4 ok=3 exit=0"), 1),
    ("sentinel deleted", BASE_TRANSCRIPT.replace(SENTINEL_LINE, ""), 1),
    ("foreign case injected", BASE_TRANSCRIPT.replace(
        "CASE threadx-rk3568-pcie/pcie-nvme PASS",
        "CASE threadx-rk3568-pcie/zzz PASS\r\n"
        "CASE threadx-rk3568-pcie/pcie-nvme PASS"), 1),
    ("link-up anchor dropped", BASE_TRANSCRIPT.replace(
        "pcie: ctrl 0 link up, downstream bus 1\r\n", "").replace(
        "pcie: ctrl 1 link up, downstream bus 2\r\n", ""), 1),
    ("dev anchor dropped", BASE_TRANSCRIPT.replace(
        "pcie: dev 01:00.0 144d:a80a class 0108\r\n", ""), 1),
    ("nvme geometry anchor dropped", BASE_TRANSCRIPT.replace(
        "nvme0: 144d:a80a at 0xf4300000, ns1: 500118192 blocks of 512 bytes "
        "(244198 MiB)\r\n", ""), 1),
    ("delivery anchor dropped", BASE_TRANSCRIPT.replace(
        "nvme0: message interrupts delivered (intid 8192, 1 so far)\r\n", ""), 1),
    ("ctrls anchor dropped", BASE_TRANSCRIPT.replace(
        "pcie-nvme: ctrls=2 devs=2\r\n", ""), 1),
    ("ns blocks anchor dropped", BASE_TRANSCRIPT.replace(
        "pcie-nvme: ns blocks=500118192 sector=512\r\n", ""), 1),
    ("irqs anchor dropped", BASE_TRANSCRIPT.replace(
        "pcie-nvme: irqs=1\r\n", ""), 1),
    ("zero irq count (backstop)", BASE_TRANSCRIPT.replace(
        "pcie-nvme: irqs=1\r\n", "pcie-nvme: irqs=0\r\n"), 1),
    ("pcie-nvme-fs CASE dropped", BASE_TRANSCRIPT.replace(
        "CASE threadx-rk3568-pcie/pcie-nvme-fs PASS\r\n", ""), 1),
    ("pcie-nvme-fs-verify CASE dropped", BASE_TRANSCRIPT.replace(
        "CASE threadx-rk3568-pcie/pcie-nvme-fs-verify PASS\r\n", ""), 1),
    ("bound anchor dropped", BASE_TRANSCRIPT.replace(
        "pcie-nvme-fs: bound nvme0 blocks=500118192 sector=512\r\n", ""), 1),
    ("formatted anchor dropped", BASE_TRANSCRIPT.replace(
        "pcie-nvme-fs: formatted exFAT sector=4096\r\n", ""), 1),
    ("mounted anchor dropped", BASE_TRANSCRIPT.replace(
        "pcie-nvme-fs: mounted type=exFAT sector=4096\r\n", ""), 1),
    ("write throughput anchor dropped", BASE_TRANSCRIPT.replace(
        "pcie-nvme-fs: write bytes=1073741824 ms=131072 kib=8192\r\n", ""), 1),
    ("read throughput anchor dropped", BASE_TRANSCRIPT.replace(
        "pcie-nvme-fs: read bytes=1073741824 ms=131072 kib=8192\r\n", ""), 1),
    ("verify ok anchor dropped", BASE_TRANSCRIPT.replace(
        "pcie-nvme-fs: verify ok checksum=1a2b3c4d\r\n", ""), 1),
    ("conservation anchor dropped", BASE_TRANSCRIPT.replace(
        "pcie-nvme-fs: conservation irqs=4096 completions=4096\r\n", ""), 1),
    ("write throughput below floor (backstop)", BASE_TRANSCRIPT.replace(
        "pcie-nvme-fs: write bytes=1073741824 ms=131072 kib=8192\r\n",
        "pcie-nvme-fs: write bytes=1073741824 ms=1048576 kib=1024\r\n"), 1),
    ("read throughput below floor (backstop)", BASE_TRANSCRIPT.replace(
        "pcie-nvme-fs: read bytes=1073741824 ms=131072 kib=8192\r\n",
        "pcie-nvme-fs: read bytes=1073741824 ms=1048576 kib=1024\r\n"), 1),
    ("conservation deltas unequal (backstop)", BASE_TRANSCRIPT.replace(
        "pcie-nvme-fs: conservation irqs=4096 completions=4096\r\n",
        "pcie-nvme-fs: conservation irqs=4096 completions=4095\r\n"), 1),
    ("conservation deltas zero (backstop)", BASE_TRANSCRIPT.replace(
        "pcie-nvme-fs: conservation irqs=4096 completions=4096\r\n",
        "pcie-nvme-fs: conservation irqs=0 completions=0\r\n"), 1),
    ("verify mounted anchor dropped", BASE_TRANSCRIPT.replace(
        "pcie-nvme-fs-verify: mounted type=exFAT sector=4096\r\n", ""), 1),
    ("verify read anchor dropped", BASE_TRANSCRIPT.replace(
        "pcie-nvme-fs-verify: read bytes=1073741824 ms=131072 kib=8192\r\n", ""), 1),
    ("verify ok anchor dropped (verify case)", BASE_TRANSCRIPT.replace(
        "pcie-nvme-fs-verify: verify ok checksum=1a2b3c4d\r\n", ""), 1),
    ("step response dropped", BASE_TRANSCRIPT.replace(
        "total variable 2\r\n", ""), 1),
    ("stor step response dropped", BASE_TRANSCRIPT.replace(
        "stor: volume 0, device nvme0, sector 4096\r\n", "").replace(
        "stor: mounted, exFAT, 476831 MB total, 475807 MB free\r\n", ""), 1),
    ("start list disagrees", BASE_TRANSCRIPT.replace(
        "list=shell-up,pcie-nvme,pcie-nvme-fs,pcie-nvme-fs-verify",
        "list=shell-up,pcie-nvme"), 1),
    ("empty selection", EMPTY_SELECTION_TRANSCRIPT, 2),
]

#: The delivery backstop: a PASS must have shown a message interrupt actually
#: delivered. The device posts the completion whether or not the interrupt was
#: delivered, so without this line the completion alone proves nothing about the
#: ITS path.
DELIVERY_RE = re.compile(
    r"nvme\d+: message interrupts delivered \(intid \d+, \d+ so far\)"
)

#: The count backstop: the case's own delivery count must be nonzero.
IRQ_COUNT_RE = re.compile(r"pcie-nvme: irqs=(\d+)")

#: The burn-in backstop: the throughput floor a pass must clear in each phase,
#: in KiB/s (the case asserts the same floor; this refuses a PASS that reports
#: less).
THROUGHPUT_FLOOR_KIB = 4096

#: The write/read throughput lines, capturing the KiB/s field.
WRITE_KIB_RE = re.compile(r"pcie-nvme-fs: write bytes=\d+ ms=\d+ kib=(\d+)")
READ_KIB_RE = re.compile(r"pcie-nvme-fs: read bytes=\d+ ms=\d+ kib=(\d+)")

#: The conservation line, capturing both deltas.
CONSERVATION_RE = re.compile(
    r"pcie-nvme-fs: conservation irqs=(\d+) completions=(\d+)"
)

#: The verify case's checksum read-back line.
VERIFY_OK_RE = re.compile(r"pcie-nvme-fs-verify: verify ok checksum=[0-9a-f]{8}")


def _last_match(transcript, pattern):
    """The last line matching `pattern`, or None (the case prints one such line
    per run, so the last one is the run's word)."""
    found = None
    for line in transcript.lines:
        match = re.search(pattern, line)
        if match:
            found = match
    return found


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

    # the delivery backstop: a PASS must have shown a message interrupt
    # delivered and reported a nonzero count - a polled completion cannot show
    # that the ITS path worked.
    if verdicts.get("pcie-nvme") == "PASS":
        if not any(DELIVERY_RE.search(line) for line in transcript.lines):
            errors.append("pcie-nvme: no message-interrupt delivery line")
        irq = None
        for line in transcript.lines:
            m = IRQ_COUNT_RE.search(line)
            if m:
                irq = int(m.group(1))
        if irq is None:
            errors.append("pcie-nvme: no delivery-count line")
        elif irq < 1:
            errors.append("pcie-nvme: the reported delivery count is zero")

    # the burn-in backstop: a passing pcie-nvme-fs must clear the throughput
    # floor in both phases and show equal, nonzero conservation deltas. A run
    # that wrote and read 1 GiB cannot have consumed zero completions, and the
    # completions and the message interrupts cannot disagree.
    if verdicts.get("pcie-nvme-fs") == "PASS":
        for phase, pattern in (("write", WRITE_KIB_RE), ("read", READ_KIB_RE)):
            match = _last_match(transcript, pattern)
            if match is None:
                errors.append("pcie-nvme-fs: no %s throughput line" % phase)
            elif int(match.group(1)) < THROUGHPUT_FLOOR_KIB:
                errors.append("pcie-nvme-fs: the %s throughput %s KiB/s is below %d"
                              % (phase, match.group(1), THROUGHPUT_FLOOR_KIB))
        match = _last_match(transcript, CONSERVATION_RE)
        if match is None:
            errors.append("pcie-nvme-fs: no conservation line")
        else:
            irqs = int(match.group(1))
            comps = int(match.group(2))
            if irqs == 0 or comps == 0 or irqs != comps:
                errors.append("pcie-nvme-fs: conservation irqs=%d completions=%d "
                              "(want equal and nonzero)" % (irqs, comps))

    # the persistence backstop: a passing pcie-nvme-fs-verify must have shown a
    # checksum read-back (the same checksum the format run printed).
    if verdicts.get("pcie-nvme-fs-verify") == "PASS":
        if not transcript.anchor_lines(VERIFY_OK_RE.pattern):
            errors.append("pcie-nvme-fs-verify: no verify-ok line")

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
