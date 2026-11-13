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



# --- the 8189FTV vendor stack (SDIO) --------------------------------------
SDIO_HEAD = (
    "rtl8189fs_sdio: core ready\n"
    "rtl8189fs_sdio: initcalls run\n"
    "rtl8189fs_sdio: card 024c:f179 funcs=1 clock=50000000 width=4 blk=512\n"
    "rtl8189fs_sdio: fw RTW: rtl8188f_FirmwareDownload fw: FW_NIC, size: 20306\n"
    "rtl8189fs_sdio: fw RTW: rtl8188f_FirmwareDownload: fw_ver=e fw_subver=0000 "
    "sig=0x88f1, Month=06, Date=07\n"
    "rtl8189fs_sdio: fw RTW: rtl8188f_FirmwareDownload: DLFW OK !\n"
    "rtl8189fs_sdio: wlan0 present=1 after 300 ms\n"
    "rtl8189fs_sdio: mac=8c:bd:37:95:95:48\n"
    "rtl8189fs_sdio: start rc=0\n"
)


def sdio_open() -> str:
    return (BANNER + SDIO_HEAD + CASE_PASS.format(name="rtl8189fs_sdio") +
            RESULT_PASS)


def sdio_bad_fw() -> str:
    return (BANNER + SDIO_HEAD.replace("size: 20306", "size: 12345") +
            CASE_PASS.format(name="rtl8189fs_sdio") + RESULT_PASS)


STA_HEAD = (
    "rtl8189fs_sta: core ready\n"
    "rtl8189fs_sta: initcalls run\n"
    "rtl8189fs_sta: wlan0 present=1 ifindex=3 mac=8c:bd:37:95:95:48\n"
    "rtl8189fs_sta: start rc=0\n"
    "rtl8189fs_sta: scan ssid=labwifi-ar-ht20s freq=2432 rc=0\n"
    "rtl8189fs_sta: bss ssid=labwifi-ar-ht20s bssid=1c:a7:70:de:29:a1 "
    "freq=2432 signal=-38\n"
    "rtl8189fs_sta: connect ssid=labwifi-ar-ht20s bssid=1c:a7:70:de:29:a1 "
    "freq=2432 rc=0\n"
    "rtl8189fs_sta: connected bssid=1c:a7:70:de:29:a1 waited_ms=1200\n"
    "rtl8189fs_sta: PASS C5\n"
    "rtl8189fs_sta: netif up name=wlan0 ip=192.168.99.2/24 "
    "mac=8c:bd:37:95:95:48\n"
    "rtl8189fs_sta: peer window ms=6000\n"
    "rtl8189fs_sta: ping tx=20 rx=20 payload=56 rtt_ms=2/4/9\n"
    "rtl8189fs_sta: ping tx=20 rx=20 payload=1400 rtt_ms=3/6/11\n"
    "rtl8189fs_sta: diag rx_frames=41 tx_frames=43 rx_bytes=60000 "
    "tx_bytes=61000 tx_fail=0\n"
    "rtl8189fs_sta: PASS C6\n"
)


def sta_open() -> str:
    return (BANNER + STA_HEAD + CASE_PASS.format(name="rtl8189fs_sta") +
            RESULT_PASS)


def sta_ping_short() -> str:
    return (BANNER + STA_HEAD.replace(
        "rtl8189fs_sta: ping tx=20 rx=20 payload=1400",
        "rtl8189fs_sta: ping tx=20 rx=19 payload=1400") +
        CASE_PASS.format(name="rtl8189fs_sta") + RESULT_PASS)


def _c6t_phases() -> str:
    names = ("loopback", "tcpdn", "udpdn", "tcpdn2", "udpdn2", "tcpup",
             "udpup")
    out = ["rtl8189fs_c6t: plan=throughput bssid=1c:a7:70:de:29:a1 "
           "dev=wlan0\n",
           "rtl8189fs_c6t: ready netif=1 rx_frames=10 tx_frames=10\n",
           "rtl8189fs_c6t: server tcp=5001 udp=5002 ok=1\n"]
    for i, name in enumerate(names, 1):
        out.append(f"rtl8189fs_c6t: phase {i} begin proto=tcp role=server "
                   f"port=5001 window_ms=30000 name={name}\n")
        out.append(f"rtl8189fs_c6t: phase {i} report bytes=100000 ms=30000 "
                   f"kbit=26666 pkts=0\n")
        if name != "loopback":
            out.append(f"rtl8189fs_c6t: phase {i} done ok=1\n")
    return "".join(out)


def c6t_open() -> str:
    return (BANNER + STA_HEAD + _c6t_phases() +
            CASE_PASS.format(name="rtl8189fs_sta") +
            CASE_PASS.format(name="rtl8189fs_c6t") + RESULT_PASS)


def c6t_short() -> str:
    text = _c6t_phases().replace("phase 7 done ok=1\n", "")
    return (BANNER + STA_HEAD + text +
            CASE_PASS.format(name="rtl8189fs_sta") +
            CASE_PASS.format(name="rtl8189fs_c6t") + RESULT_PASS)


# --- the RTL8851BU (rtw89, USB) -------------------------------------------
RTW89_C3_HEAD = (
    "rtw89_c3: core ready\n"
    "rtw89_c3: usb backend ready\n"
    "rtw89_c3: initcalls run\n"
    "rtw89_c3: usb drivers=1\n"
    "rtw89_c3: fw registered=0 name=rtw89/rtw8851b_fw-1.bin\n"
    "rtw89_c3: usb buses=1,0\n"
    "rtw89_c3: usb bus=1 hcd=ehci base=0xfd880000 started\n"
    "rtw89_c3: firmware rtw89: Firmware version 0.29.41.5 (4bd6ebac), "
    "type 5/3\n"
    "rtw89_c3: wlan0 present=1 after 300 ms\n"
    "rtw89_c3: mac=90:de:80:1b:25:ed\n"
    "rtw89_c3: start rc=0\n"
)

RTW89_C3_TAIL = (
    "rtw89_c3: usb submits=120 completes=118 refused=0 stage_overflow=0 "
    "slots_exhausted=0 stage_size=32768 slots=16 rx=64 rx_err=0\n"
    "rtw89_c3: PASS\n"
)


def rtw89_c3_open() -> str:
    return (BANNER + RTW89_C3_HEAD + RTW89_C3_TAIL
            + CASE_PASS.format(name="rtw89_c3") + RESULT_PASS)


def rtw89_c3_no_fw() -> str:
    """PASS without the driver's firmware version line."""
    body = RTW89_C3_HEAD.replace(
        "rtw89_c3: firmware rtw89: Firmware version 0.29.41.5 (4bd6ebac), "
        "type 5/3\n", "")
    return (BANNER + body + RTW89_C3_TAIL
            + CASE_PASS.format(name="rtw89_c3") + RESULT_PASS)


RTW89_C4_HEAD = (
    "rtw89_c4: core ready\n"
    "rtw89_c4: usb backend ready\n"
    "rtw89_c4: initcalls run\n"
    "rtw89_c4: usb drivers=1\n"
    "rtw89_c4: fw registered=0 name=rtw89/rtw8851b_fw-1.bin\n"
    "rtw89_c4: usb buses=1,0\n"
    "rtw89_c4: usb bus=1 hcd=ehci base=0xfd880000 started\n"
    "rtw89_c4: firmware rtw89: Firmware version 0.29.41.5 (4bd6ebac), "
    "type 5/3\n"
    "rtw89_c4: wlan0 present=1 ifindex=3\n"
    "rtw89_c4: start rc=0\n"
)

RTW89_C4_TAIL = (
    "rtw89_c4: scan ssid=labwifi-ar-ht20s freq=2432 rc=0\n"
    "rtw89_c4: bss ssid=labwifi-ar-ht20s bssid=1c:a7:70:de:29:a1 "
    "freq=2432 signal=-46\n"
    "rtw89_c4: PASS\n"
)


def rtw89_c4_open() -> str:
    return (BANNER + RTW89_C4_HEAD + RTW89_C4_TAIL
            + CASE_PASS.format(name="rtw89_c4") + RESULT_PASS)


def rtw89_c4_wrong_band() -> str:
    """A PASS with the BSS on the wrong channel."""
    body = RTW89_C4_TAIL.replace("freq=2432 signal", "freq=5180 signal")
    return (BANNER + RTW89_C4_HEAD + body
            + CASE_PASS.format(name="rtw89_c4") + RESULT_PASS)


RTW89_C5_HEAD = (
    "rtw89_c5: core ready\n"
    "rtw89_c5: usb backend ready\n"
    "rtw89_c5: initcalls run\n"
    "rtw89_c5: usb drivers=1\n"
    "rtw89_c5: fw registered=0 name=rtw89/rtw8851b_fw-1.bin\n"
    "rtw89_c5: usb buses=1,0\n"
    "rtw89_c5: usb bus=1 hcd=ehci base=0xfd880000 started\n"
    "rtw89_c5: firmware rtw89: Firmware version 0.29.41.5 (4bd6ebac), "
    "type 5/3\n"
    "rtw89_c5: wlan0 present=1 ifindex=3 mac=90:de:80:1b:25:ed\n"
    "rtw89_c5: start rc=0\n"
)

RTW89_C5_BODY = (
    "rtw89_c5: scan ssid=labwifi-ar-ht20s freq=2432 rc=0\n"
    "rtw89_c5: bss ssid=labwifi-ar-ht20s bssid=1c:a7:70:de:29:a1 "
    "freq=2432 signal=-46\n"
    "rtw89_c5: connect ssid=labwifi-ar-ht20s bssid=1c:a7:70:de:29:a1 "
    "freq=2432 rc=0\n"
    "rtw89_c5: connected bssid=1c:a7:70:de:29:a1 waited_ms=420\n"
    "rtw89_c5: PASS C5\n"
    "rtw89_c5: netif up name=wl0 ip=192.168.99.2/24 mac=90:de:80:1b:25:ed\n"
    "rtw89_c5: peer window ms=6000\n"
    "rtw89_c5: ping tx=20 rx=20 payload=56 rtt_ms=2/3/7\n"
    "rtw89_c5: ping tx=20 rx=20 payload=1400 rtt_ms=3/5/9\n"
    "rtw89_c5: diag rx_frames=42 tx_frames=44 rx_bytes=2100 tx_bytes=2300 "
    "tx_fail=0\n"
    "rtw89_c5: PASS C6\n"
    "rtw89_c5: PASS\n"
)


def rtw89_c5_open() -> str:
    return (BANNER + RTW89_C5_HEAD + RTW89_C5_BODY
            + CASE_PASS.format(name="rtw89_c5") + RESULT_PASS)


def rtw89_c5_short_ping() -> str:
    """A C6 PASS with one 1400 B round lost."""
    body = RTW89_C5_BODY.replace(
        "rtw89_c5: ping tx=20 rx=20 payload=1400",
        "rtw89_c5: ping tx=20 rx=19 payload=1400")
    return (BANNER + RTW89_C5_HEAD + body
            + CASE_PASS.format(name="rtw89_c5") + RESULT_PASS)


# --- the RTL8851BU C6T arm ------------------------------------------------
def _rtw89_c6_body(phases: int, dones: int) -> str:
    names = ("loopback", "tcpdn", "udpdn", "tcpdn2", "udpdn2", "tcpup", "udpup")
    protos = ("tcp", "tcp", "udp", "tcp", "udp", "tcp", "tcp")
    roles = ("client", "server", "server", "server", "server", "client",
             "client")
    body = ("rtw89_c6: plan=throughput bssid=1c:a7:70:de:29:a1 dev=wlan0\n"
            "rtw89_c6: ready netif=1 rx_frames=10 tx_frames=8\n"
            "rtw89_c6: server tcp=5001 udp=5002 ok=1\n")
    for i in range(phases):
        body += (f"rtw89_c6: phase {i + 1} begin proto={protos[i]} "
                 f"role={roles[i]} port=5001 window_ms=30000 "
                 f"name={names[i]}\n")
    for i in range(phases):
        body += (f"rtw89_c6: phase {i + 1} report bytes={1000000 + i} "
                 f"ms=30000 kbit=4000 pkts=900\n")
    for i in range(dones):
        body += f"rtw89_c6: phase {i + 1} done ok=1\n"
    return body


def rtw89_c6_open() -> str:
    return (BANNER + _rtw89_c6_body(7, 7)
            + "rtw89_c6: PASS C6T\n"
            + CASE_PASS.format(name="rtw89_c6") + RESULT_PASS)


def rtw89_c6_short() -> str:
    """PASS C6T with two phases never closed."""
    return (BANNER + _rtw89_c6_body(7, 5)
            + "rtw89_c6: PASS C6T\n"
            + CASE_PASS.format(name="rtw89_c6") + RESULT_PASS)


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
    ("sdio-open", sdio_open, None, {"cases": ["rtl8189fs_sdio"]}),
    ("sdio-bad-fw", sdio_bad_fw, "FW_NIC size",
     {"cases": ["rtl8189fs_sdio"]}),
    ("sta-open", sta_open, None, {"cases": ["rtl8189fs_sta"]}),
    ("sta-ping-short", sta_ping_short, "ping 1400B 19/20",
     {"cases": ["rtl8189fs_sta"]}),
    ("c6t-open", c6t_open, None,
     {"cases": ["rtl8189fs_sta", "rtl8189fs_c6t"]}),
    ("c6t-short", c6t_short, "only 5 of 6 traffic phases closed",
     {"cases": ["rtl8189fs_sta", "rtl8189fs_c6t"]}),
    ("rtw89-c3-open", rtw89_c3_open, None, {"cases": ["rtw89_c3"]}),
    ("rtw89-c3-no-fw", rtw89_c3_no_fw, "firmware version",
     {"cases": ["rtw89_c3"]}),
    ("rtw89-c4-open", rtw89_c4_open, None, {"cases": ["rtw89_c4"]}),
    ("rtw89-c4-wrong-band", rtw89_c4_wrong_band, "expected labwifi-ar-ht20s@2432",
     {"cases": ["rtw89_c4"]}),
    ("rtw89-c5-open", rtw89_c5_open, None, {"cases": ["rtw89_c5"]}),
    ("rtw89-c5-short-ping", rtw89_c5_short_ping, "ping 1400B tx=20 rx=19",
     {"cases": ["rtw89_c5"]}),
    ("rtw89-c6-open", rtw89_c6_open, None, {"cases": ["rtw89_c6"]}),
    ("rtw89-c6-short", rtw89_c6_short, "only 5 of 7 phases closed",
     {"cases": ["rtw89_c6"]}),
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
