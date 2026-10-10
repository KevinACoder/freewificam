#!/usr/bin/env python3
"""Judge for the ThreadX board WLAN suite (threadx-rk3568-rtw89).

Input: one serial transcript. Output: the judgment block; exit 0 = pass,
1 = fail (a missing sentinel counts as incomplete), 2 = configuration error
(the declaration and the transcript disagree about what should have run).

Truth sources, all independent of the transcript's own claims:
  * the case list below - reconciled at build time against the image's
    BOARD_TESTS section and suite.mk (tests/board/common/check-case-list.sh);
  * the anchor table below - each row names the emitting site in this
    repository, so a driver pin bump updates a data row, not the logic;
  * the interaction steps - tests/board/threadx-rk3568-rtw89/steps.tsv, the
    same file the host runner sends from.

The ground-truth cross-check (the rtw89-scan case comparing its sweep with the
Linux-lane capture of the same card) is judged on the case's own verdict plus
its printed evidence lines: the case fails itself when nothing matched, and
this judge additionally refuses a PASS whose logs show zero candidates.
"""

import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))

import board_judge as bj  # noqa: E402

SUITE = "threadx-rk3568-rtw89"

#: Case names in execution order (data row; the image table and suite.mk carry
#: the same list and the build reconciles the three).
CASES = [
    "shell-up",
    "rtw89-probe",
    "rtw89-scan",
    "rtw89-join-raw",
    "rtw89-join",
    "rtw89-dataplane",
    "rtw89-floodping",
]

#: Anchors a passing case must have left behind (regex, matched on the
#: stamp-stripped transcript). Emitting sites:
#:   shell-up    ports/cherrysh/shell_port.c (the REPL prompt)
#:   rtw89-probe  the case's own lines plus the driver's own probe log routed
#:               through the linuxkpi sink (third-party/rtw89/fw.c, the
#:               firmware version line, and third-party/rtw89/core.c, the chip
#:               information line), and the linuxkpi class hook's claim line
#:               (components/linuxkpi/usb/lkpi_usbh_class.c)
#:   rtw89-scan   the case's scan lines (start / one row per candidate / done)
ANCHORS = {
    "shell-up": [r"user@rk3568:/\$"],
    "rtw89-probe": [
        r"wlan: usb bus=\d+ hcd=\w+ base=0x[0-9a-f]+ started",
        # the driver's own probe-time lines, through the linuxkpi log sink:
        # the firmware it recognised (rtw89/fw.c) and the chip it read back
        # (rtw89/core.c).  Both are the driver's own text, so a pin bump that
        # changes them is a data change here, not a logic change.  The
        # "lkpiu:" module tag between the sink's "lkpi:" prefix and the
        # driver's message is the linuxkpi device name the class hook sets
        # (components/linuxkpi/usb/lkpi_usbh_class.c), i.e. the same dev_info
        # path a Linux printk would carry.
        r"lkpi: lkpiu: Firmware version \d+\.\d+\.\d+\.\d+",
        r"lkpi: lkpiu: chip info CID: [0-9a-f]+, CV: [0-9a-f]+, AID: [0-9a-f]+, ACV: [0-9a-f]+, RFE: \d+",
        r"wlan: probe done attached=1",
    ],
    "rtw89-scan": [
        r"wlan: scan start ssid=",
        r"wlan: scan done rows=",
        r"wlan: gt-check matched=[1-9]\d*/",
    ],
    # the case's own verdict line, printed only when the driver's open-auth
    # exchange completed within the prompt ceiling (rtw89_join_raw.c)
    "rtw89-join-raw": [
        r"wlan: join-raw-ok filter=",
    ],
    "rtw89-join": [
        r"wlan: join waiting for the supplicant",
        r"wlan: join-ok bssid=",
    ],
    "rtw89-dataplane": [
        r"wlan: dataplane up, dhcp started",
        r"wlan: dhcp bound ip=\d+\.\d+\.\d+\.\d+/",
        r"wlan: ping window done",
        r"wlan: iperf server ready port=5001",
    ],
    # the case's own stream anchors and window markers; the clean phase's
    # totals are the gated ones, the host reads the open marker to start its
    # own load, and the loaded totals are recorded inside the window
    "rtw89-floodping": [
        r"wlan: flood board ping start peer=",
        r"wlan: flood board ping cycles=\d+ replies=\d+ loss=\d+%",
        r"wlan: flood host window open peer=",
        r"wlan: flood board ping loaded cycles=\d+ replies=\d+ loss=\d+%",
        r"wlan: flood host window close",
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
    "[  66.360] BOARD-SUITE threadx-rk3568-rtw89 "
    "result=pass cases=7 ok=7 exit=0\r\n"
)

BASE_TRANSCRIPT = (
    "threadx-rk3568-rtw89 bring-up selftest-tag\r\n"
    "[   0.000] wlan: chain wired (backend + registry + firmware + bridge)\r\n"
    "[   5.000] BOARD-SUITE threadx-rk3568-rtw89 select window_ms=5000\r\n"
    "[  10.000] BOARD-SUITE threadx-rk3568-rtw89 start cases=7 sel=all "
    "list=shell-up,rtw89-probe,rtw89-scan,rtw89-join-raw,rtw89-join,"
    "rtw89-dataplane,rtw89-floodping\r\n"
    + PROMPT
    + "[  10.010] CASE threadx-rk3568-rtw89/shell-up PASS\r\n"
    "[  10.020] wlan: usb bus=3 hcd=xhci base=0xfe000000 started\r\n"
    "[  10.030] usbh: lkpi claim attempt 0bda:b851\r\n"
    "[  11.000] lkpi: lkpiu: Firmware version 0.29.22.0 (6a0d1e39), cmd version 1, "
    "type 1\r\n"
    "[  11.500] lkpi: lkpiu: chip info CID: 1, CV: 1, AID: 0, ACV: 0, RFE: 4\r\n"
    "[  12.000] wlan: probe done attached=1 rx=0 mgt_tx=0 scan=0/0\r\n"
    "[  12.010] CASE threadx-rk3568-rtw89/rtw89-probe PASS\r\n"
    "[  12.020] wlan: scan start ssid=QiQiJia\r\n"
    "[  15.000] wlan: bss 14:a3:2f:18:07:2c ssid=QiQiJia freq=2412 rssi=-53\r\n"
    "[  15.010] wlan: bss 14:a3:2f:18:07:31 ssid=QiQiJia-2 freq=2412 rssi=-57\r\n"
    "[  15.020] wlan: gt-match idx=0 bssid=14:a3:2f:18:07:2c freq=2412 seen=1\r\n"
    "[  15.030] wlan: scan done rows=2 chans=12\r\n"
    "[  15.040] wlan: gt-check matched=1/3 rows=2\r\n"
    "[  15.050] CASE threadx-rk3568-rtw89/rtw89-scan PASS\r\n"
    "[  15.060] wlan: join-raw sweep table rows=2 chans=12\r\n"
    "[  15.070] wlan: join-raw narrow join rc=0 state=AUTH\r\n"
    "[  15.080] wlan: join-raw narrow t=0ms state=AUTH bad_auth=0 "
    "mgt_tx=1 txc=1 rx=90 bcnT=1 tuned=2412 tunes=1\r\n"
    "[  15.180] wlan: join-raw narrow t=100ms state=ASSOC bad_auth=0 "
    "mgt_tx=2 txc=2 rx=95 bcnT=2 tuned=2412 tunes=1\r\n"
    "[  15.190] wlan: join-raw-ok filter=narrow ms=100\r\n"
    "[  15.200] CASE threadx-rk3568-rtw89/rtw89-join-raw PASS\r\n"
    "[  16.000] wlan: join waiting for the supplicant (send 'wpa connect "
    "<ssid> <psk>' on the console)\r\n"
    "[  22.000] wlan0: WPA: Key negotiation completed with "
    "14:a3:2f:18:07:2c [PTK=CCMP GTK=CCMP]\r\n"
    "[  22.100] wlan: join-ok bssid=14:a3:2f:18:07:2c state=RUN mgt_tx=15 "
    "data_tx=2 rx=90\r\n"
    "[  22.200] CASE threadx-rk3568-rtw89/rtw89-join PASS\r\n"
    "[  23.000] wlan: dataplane up, dhcp started\r\n"
    "[  25.000] wlan: dhcp bound ip=192.168.0.211/24 gw=192.168.0.1\r\n"
    "[  25.100] wlan: ping gw window=9 s\r\n"
    "[  26.000] ping: recv 192.168.0.1 1 ms\r\n"
    "[  27.000] ping: recv 192.168.0.1 2 ms\r\n"
    "[  28.000] ping: recv 192.168.0.1 1 ms\r\n"
    "[  29.000] ping: recv 192.168.0.1 1 ms\r\n"
    "[  34.000] wlan: ping window done\r\n"
    "[  34.100] wlan: iperf server ready port=5001\r\n"
    "[  34.200] CASE threadx-rk3568-rtw89/rtw89-dataplane PASS\r\n"
    "[  34.300] wlan: flood board ping start peer=192.168.0.18 secs=20\r\n"
    "[  35.000] ping: recv 192.168.0.18 1 ms\r\n"
    "[  36.000] ping: recv 192.168.0.18 2 ms\r\n"
    "[  54.400] wlan: flood board ping cycles=360 replies=360 loss=0%\r\n"
    "[  54.500] wlan: flood host window open peer=192.168.0.18 secs=45\r\n"
    "[  84.400] wlan: flood board ping loaded cycles=540 replies=534 loss=1%\r\n"
    "[  84.500] wlan: flood host window close\r\n"
    "[  84.600] CASE threadx-rk3568-rtw89/rtw89-floodping PASS\r\n"
    + SENTINEL_LINE
    + PROMPT + "==> help\r\n"
    + PROMPT + "help\x1b[0J\r\n"
    "total function 7\r\n"
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
    "threadx-rk3568-rtw89 bring-up selftest-tag\r\n"
    "[   5.000] BOARD-SUITE threadx-rk3568-rtw89 select window_ms=5000\r\n"
    "[  10.000] BOARD-SUITE threadx-rk3568-rtw89 start cases=0 sel=skip=* list=\r\n"
    "[  10.010] BOARD-SUITE-note select empty\r\n"
    "[  10.020] BOARD-SUITE threadx-rk3568-rtw89 result=pass cases=0 ok=0 exit=0\r\n"
)

MUTATIONS = [
    ("drop a CASE line", BASE_TRANSCRIPT.replace(
        "CASE threadx-rk3568-rtw89/rtw89-scan PASS\r\n", ""), 1),
    ("sentinel counts disagree", BASE_TRANSCRIPT.replace(
        "cases=7 ok=7 exit=0", "cases=7 ok=6 exit=0"), 1),
    ("sentinel deleted", BASE_TRANSCRIPT.replace(SENTINEL_LINE, ""), 1),
    ("foreign case injected", BASE_TRANSCRIPT.replace(
        "CASE threadx-rk3568-rtw89/rtw89-scan PASS",
        "CASE threadx-rk3568-rtw89/zzz PASS\r\n"
        "CASE threadx-rk3568-rtw89/rtw89-scan PASS"), 1),
    ("anchor dropped (probe done)", BASE_TRANSCRIPT.replace(
        "wlan: probe done attached=1 rx=0 mgt_tx=0 scan=0/0\r\n", ""), 1),
    ("anchor dropped (chip info)", BASE_TRANSCRIPT.replace(
        "[  11.500] lkpi: lkpiu: chip info CID: 1, CV: 1, AID: 0, ACV: 0, RFE: 4\r\n",
        ""), 1),
    ("anchor dropped (scan totals)", BASE_TRANSCRIPT.replace(
        "wlan: scan done rows=2 chans=12\r\n", ""), 1),
    ("gt match dropped", BASE_TRANSCRIPT.replace(
        "wlan: gt-check matched=1/3 rows=2\r\n", ""), 1),
    ("step response dropped", BASE_TRANSCRIPT.replace("total variable 2\r\n", ""), 1),
    ("start list disagrees", BASE_TRANSCRIPT.replace(
        "list=shell-up,rtw89-probe,rtw89-scan,rtw89-join-raw,rtw89-join,"
        "rtw89-dataplane,rtw89-floodping",
        "list=shell-up,rtw89-probe"), 1),
    ("raw join evidence dropped", BASE_TRANSCRIPT.replace(
        "wlan: join-raw-ok filter=narrow ms=100\r\n", ""), 1),
    ("join evidence dropped", BASE_TRANSCRIPT.replace(
        "wlan: join-ok bssid=14:a3:2f:18:07:2c state=RUN mgt_tx=15 "
        "data_tx=2 rx=90\r\n", "").replace(
        "wlan0: WPA: Key negotiation completed with "
        "14:a3:2f:18:07:2c [PTK=CCMP GTK=CCMP]\r\n", ""), 1),
    ("dhcp bound dropped", BASE_TRANSCRIPT.replace(
        "wlan: dhcp bound ip=192.168.0.211/24 gw=192.168.0.1\r\n", ""), 1),
    ("ping replies starved", BASE_TRANSCRIPT.replace(
        "ping: recv 192.168.0.1 2 ms\r\n", "").replace(
        "ping: recv 192.168.0.1 1 ms\r\n", ""), 1),
    ("flood totals dropped", BASE_TRANSCRIPT.replace(
        "wlan: flood board ping cycles=360 replies=360 loss=0%\r\n", ""), 1),
    ("flood loaded totals dropped", BASE_TRANSCRIPT.replace(
        "wlan: flood board ping loaded cycles=540 replies=534 loss=1%\r\n", ""), 1),
    ("empty selection", EMPTY_SELECTION_TRANSCRIPT, 2),
]

#: A scan that reports zero rows or zero matches invalidates the case even if
#: its CASE line says PASS (the case is supposed to fail itself; this row is
#: the backstop against a transcript that papered over it).
SCAN_ROW_RE = re.compile(r"wlan: scan done rows=(\d+)")
SCAN_MATCH_RE = re.compile(r"wlan: gt-check matched=(\d+)/")

#: The join and dataplane backstops: the anchors are the case's own verdict
#: chain, and these are the independent cross-checks the spec's criteria name
#: (a completed handshake anchor for A11; a bound address plus at least four
#: echo replies for A12 - the ping module prints one "ping: recv" line per
#: reply).
JOIN_NEGOTIATION_RE = re.compile(r"Key negotiation completed with .* "
                                 r"\[PTK=CCMP GTK=CCMP\]|wlan: join-ok bssid=")
DHCP_BOUND_RE = re.compile(r"wlan: dhcp bound ip=(\d+\.\d+\.\d+\.\d+)/")
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
    if expected is not None and not expected:
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
    if verdicts.get("rtw89-scan") == "PASS":
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
            errors.append("rtw89-scan: no rows= total line (the case did not report its sweep)")
        elif row_match == 0:
            errors.append("rtw89-scan: the sweep reported zero candidates")
        if match_match is None:
            errors.append("rtw89-scan: no gt-check line (the case did not compare with the ground truth)")
        elif match_match == 0:
            errors.append("rtw89-scan: zero ground-truth matches")

    # A11 backstop: a join PASS must carry either the supplicant's negotiation
    # anchor or the case's own join-ok line (the case prints the latter only
    # after both latches fired, so one of the two is the handshake evidence).
    if verdicts.get("rtw89-join") == "PASS":
        if not any(JOIN_NEGOTIATION_RE.search(line) for line in transcript.lines):
            errors.append("rtw89-join: no negotiation anchor and no join-ok line")

    # A12 backstop: a dataplane PASS must show a bound (non-any) address and
    # at least four echo replies; an all-zero or missing address, or a ping
    # that never heard back, is not a data plane.
    if verdicts.get("rtw89-dataplane") == "PASS":
        bound = None
        for line in transcript.lines:
            m = DHCP_BOUND_RE.search(line)
            if m:
                bound = m.group(1)
        if bound is None:
            errors.append("rtw89-dataplane: no dhcp bound line")
        elif bound == "0.0.0.0":
            errors.append("rtw89-dataplane: the bound address is 0.0.0.0")
        replies = sum(1 for line in transcript.lines if PING_RECV_RE.search(line))
        if replies < 4:
            errors.append("rtw89-dataplane: %d echo replies (< 4)" % replies)

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
