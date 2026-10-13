#!/usr/bin/env python3
"""Judge for the ThreadX board iwlwifi (Intel AC 7260) suite
(threadx-rk3568-iwlwifi).

Input: one serial transcript. Output: the judgment block; exit 0 = pass,
1 = fail (a missing sentinel counts as incomplete), 2 = configuration error
(the declaration and the transcript disagree about what should have run).

Truth sources, all independent of the transcript's own claims:
  * the case list below - reconciled at build time against the image's
    BOARD_TESTS section and suite.mk (tests/board/common/check-case-list.sh);
  * the anchor table below - each row names the emitting site in this
    repository, so a driver pin bump updates a data row, not the logic;
  * the interaction steps - tests/board/threadx-rk3568-iwlwifi/steps.tsv, the
    same file the host runner sends from.

The ground-truth cross-check (the iwlwifi-scan case comparing its sweep with the
Linux-lane capture of the same card) is judged on the case's own verdict plus
its printed evidence lines: the case fails itself when nothing matched, and this
judge additionally refuses a PASS whose logs show zero candidates or zero
ground-truth matches.
"""

import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))

import board_judge as bj  # noqa: E402

SUITE = "threadx-rk3568-iwlwifi"

#: Case names in execution order (data row; the image table and suite.mk carry
#: the same list and the build reconciles the three).
CASES = [
    "shell-up",
    "iwlwifi-probe",
    "iwlwifi-scan",
]

#: Anchors a passing case must have left behind (regex, matched on the
#: stamp-stripped transcript). Emitting sites:
#:   shell-up      ports/cherrysh/shell_port.c (the REPL prompt)
#:   iwlwifi-probe  the linuxkpi PCI engine's enumeration and claim
#:                 (components/linuxkpi/pci/linuxkpi_pci.c), the driver's own
#:                 chip-identification and firmware-load lines routed through
#:                 the linuxkpi sink (third-party/iwlwifi/pcie/gen1_2/trans.c,
#:                 the "Detected" line, and third-party/iwlwifi/iwl-drv.c, the
#:                 "loaded firmware version ... op_mode" line), the mac80211
#:                 substitute's registration line
#:                 (components/linuxkpi/linuxkpi_80211.c), and the case's own
#:                 "probe done" line
#:                 (tests/board/threadx-rk3568-iwlwifi/tests/iwlwifi_probe.c).
#:                 The "03:00.0" between the sink's "lkpi: " prefix and the
#:                 driver's message is the linuxkpi device name the PCI engine
#:                 sets for the function (linuxkpi_pci.c: the BDF), i.e. the
#:                 same dev_info path a Linux printk would carry; it is pinned
#:                 literally, like the sibling suites pin their device tag, so
#:                 the log-anchor family stays specific instead of promoting
#:                 every lkpi line to the never-filtered set.  On this bench
#:                 both root complexes enumerate: the NVMe function on bus 1
#:                 (01:00.0) and the AC 7260 on bus 3 (03:00.0) - the pinned
#:                 BDF is what keeps the anchor on the wireless function
#:                 (board evidence: the enumeration and the driver's own lines
#:                 both carry 03:00.0).
#:   iwlwifi-scan  the case's scan lines (start / one row per candidate / done)
ANCHORS = {
    "shell-up": [r"user@rk3568:/\$"],
    "iwlwifi-probe": [
        r"pcie: 'iwlwifi' claimed 8086:08b1",
        r"lkpi: 03:00.0: Detected Intel\(R\) Dual Band Wireless (AC|N) 7260",
        r"lkpi: 03:00.0: loaded firmware version .* op_mode iwlmvm",
        r"lkpi: lkpi80211: hw registered",
        r"wlan: probe done attached=1",
    ],
    "iwlwifi-scan": [
        r"wlan: scan start ssid=",
        r"wlan: scan done rows=",
        r"wlan: gt-check matched=[1-9]\d*/",
    ],
}

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
    "[  40.360] BOARD-SUITE threadx-rk3568-iwlwifi "
    "result=pass cases=3 ok=3 exit=0\r\n"
)

BASE_TRANSCRIPT = (
    "threadx-rk3568-iwlwifi bring-up selftest-tag\r\n"
    "[   0.000] iwlwifi: chain wired (backend + registry + firmware + bridge)\r\n"
    "[   5.000] BOARD-SUITE threadx-rk3568-iwlwifi select window_ms=5000\r\n"
    "[  10.000] BOARD-SUITE threadx-rk3568-iwlwifi start cases=3 sel=all "
    "list=shell-up,iwlwifi-probe,iwlwifi-scan\r\n"
    + PROMPT
    + "[  10.010] CASE threadx-rk3568-iwlwifi/shell-up PASS\r\n"
    "[  10.020] pcie: bus up, 1 device(s)\r\n"
    "[  10.030] pcie: 'iwlwifi' claimed 8086:08b1\r\n"
    "[  10.040] lkpi: 03:00.0: Detected Intel(R) Dual Band Wireless AC 7260\r\n"
    "[  11.000] lkpi: 03:00.0: loaded firmware version 17.0.0.0 op_mode "
    "iwlmvm\r\n"
    "[  11.100] lkpi: lkpi80211: hw registered\r\n"
    "[  11.200] iwlwifi: pci bus up, 1 device(s)\r\n"
    "[  11.300] wlan: probe done attached=1 rx=0 mgt_tx=0 scan=0/0\r\n"
    "[  11.310] CASE threadx-rk3568-iwlwifi/iwlwifi-probe PASS\r\n"
    "[  12.020] wlan: scan start ssid=QiQiJia mgt_tx=1 rx=0\r\n"
    "[  15.000] wlan: bss 14:a3:2f:18:07:2c ssid=QiQiJia freq=2412 rssi=-53\r\n"
    "[  15.010] wlan: bss 14:a3:2f:18:07:31 ssid=QiQiJia-2 freq=2412 rssi=-57\r\n"
    "[  15.020] wlan: gt-match idx=0 bssid=14:a3:2f:18:07:2c freq=2412 seen=1\r\n"
    "[  15.030] wlan: scan done rows=2 chans=0 mgt_tx=1 rx=90 scan=1/1 "
    "state=1 entered=1 close=4\r\n"
    "[  15.040] wlan: gt-check matched=1/3 rows=2\r\n"
    "[  15.050] CASE threadx-rk3568-iwlwifi/iwlwifi-scan PASS\r\n"
    + SENTINEL_LINE
    + PROMPT + "==> help\r\n"
    + PROMPT + "help\x1b[0J\r\n"
    "total function 8\r\n"
    "  exit   -> /sbin - leave the shell\r\n"
    "\r\n"
    "total variable 2\r\n"
    "  $ZERO r-   1\r\n"
    + PROMPT + "==> wlan status\r\n"
    + PROMPT + "wlan status\x1b[0J\r\n"
    "wlan: attached=1 state=2 mgt_tx=3 data_tx=0 rx=0 tx_done=0 scan=0/0 "
    "filter=0/0\r\n"
    + PROMPT + "==> exit\r\n"
    + PROMPT + "exit\x1b[0J\r\n"
    + PROMPT + "\x1b[0J\r\n"
)

EMPTY_SELECTION_TRANSCRIPT = (
    "threadx-rk3568-iwlwifi bring-up selftest-tag\r\n"
    "[   5.000] BOARD-SUITE threadx-rk3568-iwlwifi select window_ms=5000\r\n"
    "[  10.000] BOARD-SUITE threadx-rk3568-iwlwifi start cases=0 sel=skip=* list=\r\n"
    "[  10.010] BOARD-SUITE-note select empty\r\n"
    "[  10.020] BOARD-SUITE threadx-rk3568-iwlwifi result=pass cases=0 ok=0 exit=0\r\n"
)

MUTATIONS = [
    ("drop a CASE line", BASE_TRANSCRIPT.replace(
        "CASE threadx-rk3568-iwlwifi/iwlwifi-scan PASS\r\n", ""), 1),
    ("sentinel counts disagree", BASE_TRANSCRIPT.replace(
        "cases=3 ok=3 exit=0", "cases=3 ok=2 exit=0"), 1),
    ("sentinel deleted", BASE_TRANSCRIPT.replace(SENTINEL_LINE, ""), 1),
    ("foreign case injected", BASE_TRANSCRIPT.replace(
        "CASE threadx-rk3568-iwlwifi/iwlwifi-scan PASS",
        "CASE threadx-rk3568-iwlwifi/zzz PASS\r\n"
        "CASE threadx-rk3568-iwlwifi/iwlwifi-scan PASS"), 1),
    ("claim anchor dropped", BASE_TRANSCRIPT.replace(
        "pcie: 'iwlwifi' claimed 8086:08b1\r\n", ""), 1),
    ("chip-identification anchor dropped", BASE_TRANSCRIPT.replace(
        "lkpi: 03:00.0: Detected Intel(R) Dual Band Wireless AC 7260\r\n", ""), 1),
    ("firmware anchor dropped", BASE_TRANSCRIPT.replace(
        "lkpi: 03:00.0: loaded firmware version 17.0.0.0 op_mode iwlmvm\r\n",
        ""), 1),
    ("hw registered anchor dropped", BASE_TRANSCRIPT.replace(
        "lkpi: lkpi80211: hw registered\r\n", ""), 1),
    ("probe done anchor dropped", BASE_TRANSCRIPT.replace(
        "wlan: probe done attached=1 rx=0 mgt_tx=0 scan=0/0\r\n", ""), 1),
    ("scan done totals dropped", BASE_TRANSCRIPT.replace(
        "wlan: scan done rows=2 chans=0 mgt_tx=1 rx=90 scan=1/1 state=1 "
        "entered=1 close=4\r\n", ""), 1),
    ("gt match dropped", BASE_TRANSCRIPT.replace(
        "wlan: gt-check matched=1/3 rows=2\r\n", ""), 1),
    ("zero-row sweep (backstop)", BASE_TRANSCRIPT.replace(
        "wlan: scan done rows=2 chans=0 mgt_tx=1 rx=90 scan=1/1 state=1 "
        "entered=1 close=4\r\n",
        "wlan: scan done rows=0 chans=0 mgt_tx=1 rx=90 scan=1/1 state=1 "
        "entered=1 close=4\r\n"), 1),
    ("zero-match sweep (backstop)", BASE_TRANSCRIPT.replace(
        "wlan: gt-check matched=1/3 rows=2\r\n",
        "wlan: gt-check matched=0/3 rows=2\r\n"), 1),
    ("step response dropped", BASE_TRANSCRIPT.replace("total variable 2\r\n", ""), 1),
    ("start list disagrees", BASE_TRANSCRIPT.replace(
        "list=shell-up,iwlwifi-probe,iwlwifi-scan",
        "list=shell-up,iwlwifi-probe"), 1),
    ("empty selection", EMPTY_SELECTION_TRANSCRIPT, 2),
]

#: A scan that reports zero rows or zero matches invalidates the case even if
#: its CASE line says PASS (the case is supposed to fail itself; this row is
#: the backstop against a transcript that papered over it).
SCAN_ROW_RE = re.compile(r"wlan: scan done rows=(\d+)")
SCAN_MATCH_RE = re.compile(r"wlan: gt-check matched=(\d+)/")


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

    # the ground-truth backstop: a PASS whose own evidence shows an empty sweep
    # or an unmatched table is not a pass
    if verdicts.get("iwlwifi-scan") == "PASS":
        row_match = None
        match_match = None
        for line in transcript.lines:
            m = SCAN_ROW_RE.search(line)
            if m:
                row_match = int(m.group(1))
            m = SCAN_MATCH_RE.search(line)
            if m:
                match_match = int(m.group(1))
        if row_match is None:
            errors.append("iwlwifi-scan: no rows= total line (the case did not report its sweep)")
        elif row_match == 0:
            errors.append("iwlwifi-scan: the sweep reported zero candidates")
        if match_match is None:
            errors.append("iwlwifi-scan: no gt-check line (the case did not compare with the ground truth)")
        elif match_match == 0:
            errors.append("iwlwifi-scan: zero ground-truth matches")

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
