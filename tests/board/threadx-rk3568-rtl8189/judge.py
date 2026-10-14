#!/usr/bin/env python3
"""Judge for the ThreadX board RTL8189FTV suite (threadx-rk3568-rtl8189).

Input: one serial transcript. Output: the judgment block; exit 0 = pass,
1 = fail (a missing sentinel counts as incomplete), 2 = configuration error
(the declaration and the transcript disagree about what should have run).

Truth sources, all independent of the transcript's own claims:
  * the case list below - reconciled at build time against the image's
    BOARD_TESTS section and suite.mk (tests/board/common/check-case-list.sh);
  * the anchor table below - each row names the emitting site in this
    repository, so a change to a case updates a data row, not the logic;
  * the interaction steps - tests/board/threadx-rk3568-rtl8189/steps.tsv, the
    same file the host runner sends from.

Backstops run on top of the case verdicts, because a case that reports PASS while
its own evidence says otherwise is exactly what a judgment channel exists to
catch: a passing probe must have named a hardware address that is not all zero
(the driver reads it from the chip's eFuse, so an all-zero address means it never
reached the chip), a passing scan must have reported at least one row (an empty
table is not a scan, whatever the CASE line says), a passing join must carry the
supplicant's own handshake anchor, and a passing dataplane must show a bound
(non-any) address plus at least four ping echo replies.
"""

import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))

import board_judge as bj  # noqa: E402

SUITE = "threadx-rk3568-rtl8189"

#: Case names in execution order (data row; the image table and suite.mk carry
#: the same list and the build reconciles the three).
CASES = [
    "shell-up",
    "wlan8189-probe",
    "wlan8189-scan",
    "wlan8189-join",
    "wlan8189-dataplane",
]

#: Anchors a passing case must have left behind (regex, matched on the
#: stamp-stripped transcript). Emitting sites:
#:   shell-up        ports/cherrysh/shell_port.c (the REPL prompt)
#:   wlan8189-probe  the case's own lines (tests/board/threadx-rk3568-rtl8189/
#:                   tests/wlan8189_probe.c): the card the SDIO seam brought up,
#:                   the wireless device the driver's probe registered together
#:                   with the hardware address it read from the chip, and the
#:                   interface it handed over
#:   wlan8189-scan   the case's own lines (tests/.../tests/wlan8189_scan.c): the
#:                   scan it asked the driver for, the rows the driver reported
#:                   back through the cfg80211 substitute, and the comparison
#:                   against the access points this board's Linux lane recorded
#:   wlan8189-join   the case's own lines (tests/.../tests/wlan8189_join.c): the
#:                   wait line the runner's credential step is held for, and the
#:                   join-ok line printed once the supplicant reported both the
#:                   association and the completed 4-way
#:   wlan8189-       the case's own lines (tests/.../tests/wlan8189_dataplane.c):
#:   dataplane       the interface bring-up, the DHCP lease, the ping window, the
#:                   netif adapter's diagnostic counters, the iperf server, and
#:                   the census instrumentation (the three measurement phases,
#:                   their `perf census:` snapshots, and the rate/BA readout)
ANCHORS = {
    "shell-up": [r"user@rk3568:/\$"],
    "wlan8189-probe": [
        r"wlan8189: sdio up card=[0-9a-f]+:[0-9a-f]+ funcs=\d+",
        r"wlan8189: driver attached mac=[0-9a-f:]{17}",
        r"wlan8189: netdev up rc=0",
        r"wlan8189: probe ok",
    ],
    "wlan8189-scan": [
        r"wlan8189: scan start ssid=\S+",
        r"wlan8189: scan done rows=\d+ chans=\d+",
        r"wlan8189: gt-check matched=\d+/\d+",
        r"wlan8189: scan ok",
    ],
    "wlan8189-join": [
        r"wlan8189: join waiting for the supplicant",
        r"wlan8189: join-ok bssid=",
    ],
    "wlan8189-dataplane": [
        r"wlan8189: dataplane up, dhcp started",
        r"wlan8189: dhcp bound ip=\d+\.\d+\.\d+\.\d+/",
        r"wlan8189: ping window done",
        r"wlan8189: diag rx=\d+ eapol=\d+ tx=\d+ fail=\d+",
        r"wlan8189: iperf server ready port=5001",
        r"wlan8189: phase 1 ready",
        r"wlan8189: phase 3 done",
        r"perf census: A ",
        r"wlan8189: rate ",
    ],
}

#: The census and phase families: every `perf census:` line is a one-boot
#: ledger line and every phase line marks a measurement window, so the log
#: plane must never fold or filter them away (they feed the round's RUN as
#: well as the judge). Collected by tools/gates/log-anchors.py by name.
CENSUS_LINE_RE = re.compile(r"perf census: ")
PHASE_LINE_RE = re.compile(r"wlan8189: phase \d")
PS_LINE_RE = re.compile(r"wlan8189: ps ")

HERE = os.path.dirname(os.path.abspath(__file__))
STEPS_PATH = os.path.join(HERE, "steps.tsv")

# --------------------------------------------------------------------------
# selftest fixture: one legal transcript plus the mutations that must go red.
# --------------------------------------------------------------------------

# The console paints its prompt, so every prompt occurrence carries escape
# sequences (a plain "user@rk3568:/$" never appears on the wire).
PROMPT = "\x1b[1;32muser\x1b[m@\x1b[1;32mrk3568\x1b[m:\x1b[1;34m/\x1b[m$ \x1b[m"

SENTINEL_LINE = (
    "[  41.100] BOARD-SUITE threadx-rk3568-rtl8189 "
    "result=pass cases=5 ok=5 exit=0\r\n"
)

BASE_TRANSCRIPT = (
    "threadx-rk3568-rtl8189 bring-up selftest-tag\r\n"
    "[   5.000] BOARD-SUITE threadx-rk3568-rtl8189 select window_ms=5000\r\n"
    "[  10.000] BOARD-SUITE threadx-rk3568-rtl8189 start cases=5 sel=all "
    "list=shell-up,wlan8189-probe,wlan8189-scan,wlan8189-join,"
    "wlan8189-dataplane\r\n"
    + PROMPT
    + "[  10.010] CASE threadx-rk3568-rtl8189/shell-up PASS\r\n"
    "[  12.400] wlan8189: sdio up card=24c:f179 funcs=1\r\n"
    "[  16.200] wlan8189: driver attached mac=8c:bd:37:95:95:48\r\n"
    "[  16.300] wlan8189: netdev up rc=0\r\n"
    "[  16.310] wlan8189: probe ok\r\n"
    "[  16.320] CASE threadx-rk3568-rtl8189/wlan8189-probe PASS\r\n"
    "[  16.900] wlan8189: scan start ssid=QiQiJia\r\n"
    "[  38.400] wlan8189: bss 14:a3:2f:18:07:2c freq=2412000 rssi=-41 ssid=QiQiJia\r\n"
    "[  38.500] wlan8189: scan done rows=17 chans=13\r\n"
    "[  38.510] wlan8189: gt-check matched=1/3\r\n"
    "[  38.520] wlan8189: scan ok\r\n"
    "[  38.530] CASE threadx-rk3568-rtl8189/wlan8189-scan PASS\r\n"
    "[  39.000] wlan8189: join waiting for the supplicant (send 'wpa connect "
    "<ssid> <psk>' on the console)\r\n"
    "[  42.000] wlan0: WPA: Key negotiation completed with "
    "14:a3:2f:18:07:2c [PTK=CCMP GTK=CCMP]\r\n"
    "[  42.100] wlan8189: join-ok bssid=14:a3:2f:18:07:2c state=RUN\r\n"
    "[  42.200] CASE threadx-rk3568-rtl8189/wlan8189-join PASS\r\n"
    "[  43.000] wlan8189: dataplane up, dhcp started\r\n"
    "[  45.000] wlan8189: dhcp bound ip=192.168.0.211/24 gw=192.168.0.1\r\n"
    "[  45.100] wlan8189: ping gw window=9 s\r\n"
    "[  46.000] ping: recv 192.168.0.1 1 ms\r\n"
    "[  47.000] ping: recv 192.168.0.1 2 ms\r\n"
    "[  48.000] ping: recv 192.168.0.1 1 ms\r\n"
    "[  49.000] ping: recv 192.168.0.1 1 ms\r\n"
    "[  54.000] wlan8189: ping window done\r\n"
    "[  54.100] wlan8189: diag rx=42 eapol=8 tx=13 fail=0\r\n"
    "[  54.200] wlan8189: iperf server ready port=5001\r\n"
    "[  54.300] wlan8189: phase 1 ready lps=2\r\n"
    "[  54.310] wlan8189: iperf server ready port=5001\r\n"
    "[  54.320] wlan8189: iperf window 20 s\r\n"
    "[  74.000] wlan8189: iperf window done\r\n"
    "[  74.100] perf census: A cmd52 n=3\r\n"
    "[  74.200] perf census: D pump cycles=95 kick=0 timeout=95 busy=90 empty=5 err=0\r\n"
    "[  74.300] wlan8189: rate bssid=14:a3:2f:18:07:2c bitmap=0x01 ramask=0x00000000000fffff bw=1 sgi=1\r\n"
    "[  74.400] wlan8189: ps lps=2 leisure=1 fw_ps=0 cpwm=0 enter=0 leave=0\r\n"
    "[  74.500] wlan8189: phase 1 done\r\n"
    "[  74.600] wlan8189: phase 2 ready lps=0\r\n"
    "[  74.610] wlan8189: iperf server ready port=5001\r\n"
    "[  94.000] wlan8189: phase 2 done\r\n"
    "[  94.100] wlan8189: phase 3 ready lps=2\r\n"
    "[  94.110] wlan8189: iperf server ready port=5001\r\n"
    "[ 114.000] wlan8189: phase 3 done\r\n"
    "[ 114.100] CASE threadx-rk3568-rtl8189/wlan8189-dataplane PASS\r\n"
    "[  56.000] total function 9\r\n"
    "[  56.010] total variable 2\r\n"
    "[  56.020] wlan8189: attached=1 bss=17\r\n"
    + SENTINEL_LINE
)

#: The console's own selection line for the empty-selection fixture.
EMPTY_SELECTION_TRANSCRIPT = (
    "threadx-rk3568-rtl8189 bring-up selftest-tag\r\n"
    "[   5.000] BOARD-SUITE threadx-rk3568-rtl8189 select window_ms=5000\r\n"
    "[  10.000] BOARD-SUITE threadx-rk3568-rtl8189 start cases=0 sel=only=nothing-matches "
    "list=\r\n"
    "[  10.010] BOARD-SUITE threadx-rk3568-rtl8189 result=pass cases=0 ok=0 exit=0\r\n"
)

#: Mutations that must go red (1 = fail, 2 = configuration error). Each removes
#: or corrupts exactly one piece of evidence, so a judge that stopped checking
#: that piece would let the mutation through.
MUTATIONS = [
    ("sentinel deleted", BASE_TRANSCRIPT.replace(SENTINEL_LINE, ""), 1),
    ("foreign case injected", BASE_TRANSCRIPT.replace(
        "CASE threadx-rk3568-rtl8189/wlan8189-scan PASS",
        "CASE threadx-rk3568-rtl8189/zzz PASS\r\n"
        "CASE threadx-rk3568-rtl8189/wlan8189-scan PASS"), 1),
    ("probe anchor dropped", BASE_TRANSCRIPT.replace(
        "wlan8189: probe ok\r\n", ""), 1),
    ("sdio anchor dropped", BASE_TRANSCRIPT.replace(
        "wlan8189: sdio up card=24c:f179 funcs=1\r\n", ""), 1),
    ("attach anchor dropped", BASE_TRANSCRIPT.replace(
        "wlan8189: driver attached mac=8c:bd:37:95:95:48\r\n", ""), 1),
    ("netdev anchor dropped", BASE_TRANSCRIPT.replace(
        "wlan8189: netdev up rc=0\r\n", ""), 1),
    ("scan start anchor dropped", BASE_TRANSCRIPT.replace(
        "wlan8189: scan start ssid=QiQiJia\r\n", ""), 1),
    ("scan rows anchor dropped", BASE_TRANSCRIPT.replace(
        "wlan8189: scan done rows=17 chans=13\r\n", ""), 1),
    ("gt-check anchor dropped", BASE_TRANSCRIPT.replace(
        "wlan8189: gt-check matched=1/3\r\n", ""), 1),
    ("scan verdict anchor dropped", BASE_TRANSCRIPT.replace(
        "wlan8189: scan ok\r\n", ""), 1),
    ("zero hardware address", BASE_TRANSCRIPT.replace(
        "mac=8c:bd:37:95:95:48", "mac=00:00:00:00:00:00"), 1),
    ("empty scan table", BASE_TRANSCRIPT.replace(
        "wlan8189: scan done rows=17 chans=13", "wlan8189: scan done rows=0 chans=13"), 1),
    ("step response dropped", BASE_TRANSCRIPT.replace(
        "total variable 2\r\n", ""), 1),
    ("status response dropped", BASE_TRANSCRIPT.replace(
        "wlan8189: attached=1 bss=17\r\n", ""), 1),
    ("join wait anchor dropped", BASE_TRANSCRIPT.replace(
        "wlan8189: join waiting for the supplicant (send 'wpa connect "
        "<ssid> <psk>' on the console)\r\n", ""), 1),
    ("join evidence dropped", BASE_TRANSCRIPT.replace(
        "wlan8189: join-ok bssid=14:a3:2f:18:07:2c state=RUN\r\n", "").replace(
        "wlan0: WPA: Key negotiation completed with "
        "14:a3:2f:18:07:2c [PTK=CCMP GTK=CCMP]\r\n", ""), 1),
    ("dhcp bound dropped", BASE_TRANSCRIPT.replace(
        "wlan8189: dhcp bound ip=192.168.0.211/24 gw=192.168.0.1\r\n", ""), 1),
    ("diag line dropped", BASE_TRANSCRIPT.replace(
        "wlan8189: diag rx=42 eapol=8 tx=13 fail=0\r\n", ""), 1),
    ("census snapshot dropped", BASE_TRANSCRIPT.replace(
        "perf census: A cmd52 n=3\r\n", ""), 1),
    ("phase machinery dropped", BASE_TRANSCRIPT.replace(
        "wlan8189: phase 3 done\r\n", ""), 1),
    ("rate readout dropped", BASE_TRANSCRIPT.replace(
        "wlan8189: rate bssid=14:a3:2f:18:07:2c bitmap=0x01 "
        "ramask=0x00000000000fffff bw=1 sgi=1\r\n", ""), 1),
    ("ping replies starved", BASE_TRANSCRIPT.replace(
        "ping: recv 192.168.0.1 2 ms\r\n", ""), 1),
    ("start list disagrees", BASE_TRANSCRIPT.replace(
        "list=shell-up,wlan8189-probe,wlan8189-scan,wlan8189-join,"
        "wlan8189-dataplane",
        "list=shell-up,wlan8189-probe"), 1),
    ("empty selection", EMPTY_SELECTION_TRANSCRIPT, 2),
]

#: The probe backstop: a PASS must have named a hardware address that is not all
#: zero. The driver reads it from the chip's eFuse; an all-zero address means it
#: never reached the chip, whatever its CASE line says.
MAC_RE = re.compile(r"wlan8189: driver attached mac=([0-9a-f:]{17})")

#: The scan backstop: the rows the driver reported are the case's whole claim,
#: so a PASS with an empty table is refused.
ROWS_RE = re.compile(r"wlan8189: scan done rows=(\d+) chans=\d+")

#: The join and dataplane backstops: the anchors are the case's own verdict
#: chain, and these are the independent cross-checks the criteria name (a
#: completed handshake anchor for the join; a bound address plus at least four
#: echo replies for the dataplane - the ping module prints one "ping: recv" line
#: per reply).
JOIN_NEGOTIATION_RE = re.compile(r"Key negotiation completed with .* "
                                 r"\[PTK=CCMP GTK=CCMP\]|wlan8189: join-ok bssid=")
DHCP_BOUND_RE = re.compile(r"wlan8189: dhcp bound ip=(\d+\.\d+\.\d+\.\d+)/")
PING_RECV_RE = re.compile(r"ping: recv")


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

    # the probe backstop: a PASS must have named a hardware address that is not
    # all zero
    if verdicts.get("wlan8189-probe") == "PASS":
        mac = None
        for line in transcript.lines:
            m = MAC_RE.search(line)
            if m:
                mac = m.group(1)
        if mac is None:
            errors.append("wlan8189-probe: no hardware-address line (the driver did not report one)")
        elif mac == "00:00:00:00:00:00":
            errors.append("wlan8189-probe: the hardware address is all zero")

    # the scan backstop: the case's whole claim is a table with rows in it
    if verdicts.get("wlan8189-scan") == "PASS":
        rows = None
        for line in transcript.lines:
            m = ROWS_RE.search(line)
            if m:
                rows = int(m.group(1))
        if rows is None:
            errors.append("wlan8189-scan: no scan-result line")
        elif rows == 0:
            errors.append("wlan8189-scan: the scan reported an empty table")

    # the join backstop: a PASS must carry either the supplicant's negotiation
    # anchor or the case's own join-ok line (the case prints the latter only
    # after both latches fired, so one of the two is the handshake evidence)
    if verdicts.get("wlan8189-join") == "PASS":
        if not any(JOIN_NEGOTIATION_RE.search(line) for line in transcript.lines):
            errors.append("wlan8189-join: no negotiation anchor and no join-ok line")

    # the dataplane backstop: a PASS must show a bound (non-any) address and at
    # least four echo replies; an all-zero or missing address, or a ping that
    # never heard back, is not a data plane
    if verdicts.get("wlan8189-dataplane") == "PASS":
        bound = None
        for line in transcript.lines:
            m = DHCP_BOUND_RE.search(line)
            if m:
                bound = m.group(1)
        if bound is None:
            errors.append("wlan8189-dataplane: no dhcp bound line")
        elif bound == "0.0.0.0":
            errors.append("wlan8189-dataplane: the bound address is 0.0.0.0")
        replies = sum(1 for line in transcript.lines if PING_RECV_RE.search(line))
        if replies < 4:
            errors.append("wlan8189-dataplane: %d echo replies (< 4)" % replies)

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
