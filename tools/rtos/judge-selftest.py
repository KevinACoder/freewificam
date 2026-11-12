#!/usr/bin/env python3
"""The judge's own test for the 5 GHz round's anchors (docs/HANDOFF-5GHZ.md).

Why this file exists: the judge is what turns a serial log into a verdict, so a
new case's anchors need a positive and a negative here before the case is
allowed to be judged on a board.  A pattern that silently never matches, a
verdict block that passes without its evidence, and a PASS on a regulatory
channel that cannot beacon are all failures of the judge, not of the image, and
they must be found on the host in seconds instead of in a boot.

Each sample is a synthetic serial log: the banner, the kernel-ready line, the
case's anchors and the runner's per-case verdict lines.  The expected outcome is
"no errors" or a substring that must appear in the errors.

    python3 tools/rtos/judge-selftest.py            # all samples
    python3 tools/rtos/judge-selftest.py -v         # also print the verdicts
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import judge  # noqa: E402

BANNER = (
    "rtos: banner backend=freertos kernel=6.1.0-rtos build=selftest\n"
    "rtos: kernel ready tick=1000Hz\n"
)

# The runner's own summary: the judge rejects a log without it, so every
# sample carries one (a synthetic log that omits it would test nothing).
RESULT_PASS = "rtos: result pass cases=1/1 backend=freertos\n"
RESULT_FAIL = "rtos: result fail cases=0/1 backend=freertos\n"

CASE_PASS = "rtos: case {name} PASS\n"
CASE_FAIL = "rtos: case {name} FAIL\n"

AP5G_HEAD = (
    "iwl7260_ap5g: core ready\n"
    "iwl7260_ap5g: fw registered=0\n"
    "iwl7260_ap5g: initcalls run\n"
    "iwl7260_ap5g: pci devs=1\n"
    "iwl7260_ap5g: wlan0 present=1 ifindex=3 "
    "mac=6c:29:95:12:f0:d9 iftype=2\n"
    "iwl7260_ap5g: bands 2g=1 5g=1 2g_chans=13 5g_chans=25 5g_ht=1\n"
)

AP5G_CAND_OK = (
    "iwl7260_ap5g: cand freq=5180 chan=36 present=1 flags=0x00000000 "
    "can_beacon=1\n"
    "iwl7260_ap5g: cand freq=5200 chan=40 present=1 flags=0x00000000 "
    "can_beacon=1\n"
)

AP5G_CAND_NOIR = (
    "iwl7260_ap5g: cand freq=5180 chan=36 present=1 flags=0x00000002 "
    "can_beacon=0\n"
    "iwl7260_ap5g: cand freq=5200 chan=40 present=1 flags=0x00000002 "
    "can_beacon=0\n"
)

AP5G_TAIL_OK = (
    "iwl7260_ap5g: chan freq=5180 chan=36 present=1 flags=0x00000000 "
    "can_beacon=1\n"
    "iwl7260_ap5g: set_iface rc=0 type=3\n"
    "iwl7260_ap5g: start rc=0\n"
    "iwl7260_ap5g: netif up name=wl0 ip=192.168.99.1/24 "
    "mac=6c:29:95:12:f0:d9\n"
    "iwl7260_ap5g: security wpa2=0 ssid=rtos-7260-ap-5g hw_mode=a "
    "freq=5180 chan=36\n"
    "iwl7260_ap5g: hostap start rc=0 ready=1\n"
    "iwl7260_ap5g: peer window ms=12000\n"
    "iwl7260_ap5g: hostap up ready=1 sta=1\n"
    "iwl7260_ap5g: sta add n=1 mac=1c:a7:70:de:29:a1\n"
    "iwl7260_ap5g: link freq=5180\n"
    "iwl7260_ap5g: ping tx=20 rx=20 payload=56 rtt_ms=2/3/7\n"
    "iwl7260_ap5g: ping tx=20 rx=20 payload=1400 rtt_ms=2/4/9\n"
)

AP5G_WPA2 = (
    "iwl7260_ap5g: security wpa2=1 ssid=rtos-7260-ap-5g hw_mode=a "
    "freq=5180 chan=36\n"
    "iwl7260_ap5g: wpa2 station sta=1 ptk=1 gtk=1 fail=0 wait_ms=180\n"
    "iwl7260_ap5g: eapol tx=4 rx=2 drop=0\n"
)

STA5G_HEAD = (
    "iwl7260_sta5g: core ready\n"
    "iwl7260_sta5g: fw registered=0\n"
    "iwl7260_sta5g: initcalls run\n"
    "iwl7260_sta5g: pci devs=1\n"
    "iwl7260_sta5g: wlan0 present=1 ifindex=3 "
    "mac=6c:29:95:12:f0:d9\n"
    "iwl7260_sta5g: bands 2g=1 5g=1 5g_chans=25 5g_ht=1\n"
)

STA5G_BODY = (
    "iwl7260_sta5g: chan freq=5180 chan=36 present=1 flags=0x00000002\n"
    "iwl7260_sta5g: start rc=0\n"
    "iwl7260_sta5g: netif up name=wl0 ip=192.168.99.2/255.255.255.0 "
    "mac=6c:29:95:12:f0:d9\n"
    "iwl7260_sta5g: security wpa2=0 ssid=labwifi-5g freq=5180\n"
    "iwl7260_sta5g: sta start rc=0\n"
    "iwl7260_sta5g: sta state=7 waited_ms=1200 "
    "bssid=1c:a7:70:de:29:a1\n"
    "iwl7260_sta5g: link freq=5180\n"
    "iwl7260_sta5g: mcast allmulti=1\n"
    "iwl7260_sta5g: peer ip=192.168.99.1\n"
    "iwl7260_sta5g: ping tx=20 rx=20 payload=56 rtt_ms=2/3/6\n"
    "iwl7260_sta5g: ping tx=20 rx=20 payload=1400 rtt_ms=3/4/8\n"
)


def ap5g_open() -> str:
    return (BANNER + AP5G_HEAD + AP5G_CAND_OK + AP5G_TAIL_OK
            + "iwl7260_ap5g: PASS AP5\n"
            + CASE_PASS.format(name="iwl7260_ap5g") + RESULT_PASS)


def ap5g_wpa2() -> str:
    body = (AP5G_HEAD + AP5G_CAND_OK + AP5G_TAIL_OK).replace(
        "security wpa2=0", "security wpa2=1")
    return (BANNER + body + AP5G_WPA2
            + "iwl7260_ap5g: PASS AP5W\n"
            + CASE_PASS.format(name="iwl7260_ap5g") + RESULT_PASS)


def ap5g_noir() -> str:
    """The band is passive: the case must fail, and a PASS must be rejected."""
    return (BANNER + AP5G_HEAD + AP5G_CAND_NOIR
            + "iwl7260_ap5g: FAIL no beaconable 5 GHz channel (flags above are "
            "the regulatory fact)\n"
            + CASE_FAIL.format(name="iwl7260_ap5g") + RESULT_FAIL)


def ap5g_noir_but_pass() -> str:
    """The image lies: PASS while every candidate is NO_IR."""
    return (BANNER + AP5G_HEAD + AP5G_CAND_NOIR
            + "iwl7260_ap5g: PASS AP5\n"
            + CASE_PASS.format(name="iwl7260_ap5g") + RESULT_PASS)


def ap5g_no_bands() -> str:
    """PASS without the band census."""
    return (BANNER + AP5G_HEAD.replace(
        "iwl7260_ap5g: bands 2g=1 5g=1 2g_chans=13 5g_chans=25 5g_ht=1\n", "")
        + AP5G_CAND_OK + AP5G_TAIL_OK
        + "iwl7260_ap5g: PASS AP5\n"
        + CASE_PASS.format(name="iwl7260_ap5g") + RESULT_PASS)


def ap5g_wrong_band() -> str:
    """A PASS on 2.4 GHz dressed as the 5 GHz round."""
    body = (AP5G_HEAD + AP5G_TAIL_OK).replace("5180", "2462").replace(
        "chan=36", "chan=11").replace("hw_mode=a", "hw_mode=g")
    return (BANNER + body + "iwl7260_ap5g: PASS AP5\n"
            + CASE_PASS.format(name="iwl7260_ap5g") + RESULT_PASS)


def sta5g_open() -> str:
    return (BANNER + STA5G_HEAD + STA5G_BODY
            + "iwl7260_sta5g: PASS STA5\n"
            + CASE_PASS.format(name="iwl7260_sta5g") + RESULT_PASS)


def sta5g_fell_back() -> str:
    """Associated on 2.4 GHz: the band gate must reject it."""
    body = STA5G_BODY.replace("link freq=5180", "link freq=2432")
    return (BANNER + STA5G_HEAD + body
            + "iwl7260_sta5g: PASS STA5\n"
            + CASE_PASS.format(name="iwl7260_sta5g") + RESULT_PASS)


def sta5g_no_link() -> str:
    """PASS without the link frequency (the band is unproven)."""
    body = STA5G_BODY.replace("iwl7260_sta5g: link freq=5180\n", "")
    return (BANNER + STA5G_HEAD + body
            + "iwl7260_sta5g: PASS STA5\n"
            + CASE_PASS.format(name="iwl7260_sta5g") + RESULT_PASS)


SAMPLES = [
    ("ap5g-open", ap5g_open, None, {"cases": ["iwl7260_ap5g"]}),
    ("ap5g-wpa2", ap5g_wpa2, None, {"cases": ["iwl7260_ap5g"]}),
    ("ap5g-noir", ap5g_noir, "no beaconable", {"cases": ["iwl7260_ap5g"]}),
    ("ap5g-noir-but-pass", ap5g_noir_but_pass, "no candidate channel",
     {"cases": ["iwl7260_ap5g"]}),
    ("ap5g-no-bands", ap5g_no_bands, "band census",
     {"cases": ["iwl7260_ap5g"]}),
    ("ap5g-wrong-band", ap5g_wrong_band, "not 5 GHz",
     {"cases": ["iwl7260_ap5g"]}),
    ("ap5g-expect-freq", ap5g_open, "expected 5200",
     {"cases": ["iwl7260_ap5g"], "freq": "5200"}),
    ("sta5g-open", sta5g_open, None, {"cases": ["iwl7260_sta5g"]}),
    ("sta5g-fell-back", sta5g_fell_back, "not 5 GHz",
     {"cases": ["iwl7260_sta5g"]}),
    ("sta5g-no-link", sta5g_no_link, "link frequency",
     {"cases": ["iwl7260_sta5g"]}),
    ("sta5g-expect-freq", sta5g_open, "expected 5200",
     {"cases": ["iwl7260_sta5g"], "freq": "5200"}),
]


def main() -> int:
    verbose = "-v" in sys.argv[1:]
    failures = 0

    for name, build, want, expect in SAMPLES:
        verdict = judge.judge(build(), dict(expect))
        errors = verdict.get("errors", [])
        ok = (not errors) if want is None else any(want in e for e in errors)
        status = "ok" if ok else "FAIL"
        print(f"judge-selftest: {name:22s} {status}")
        if not ok:
            failures += 1
            print(f"  expected: {'no errors' if want is None else want}")
            for e in errors:
                print(f"  got: {e}")
        elif verbose:
            for e in errors:
                print(f"  error: {e}")
            for n in verdict.get("notes", []):
                print(f"  note: {n}")

    total = len(SAMPLES)
    print(f"judge-selftest: {total - failures}/{total} samples behaved")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
