#!/usr/bin/env python3
"""Judge one RTOS board boot from its serial log.

The image prints a fixed anchor language (see tests/board/rtos/app/cases.h and the case
files); this module turns that text into per-case verdicts.  It reads nothing
but the log, so the same judge runs on a captured file, on the host tier's
stdout, and on a live capture.

Usage:
    tools/rtos/judge.py <serial.log> [--json out.json] [--expect key=value ...]

Exit code: 0 all cases passed, 1 a case failed or an anchor is missing.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

CASES = (
    "tick",
    "cmsis2",
    "net",
    "emmc",
    "wlan",
    "pcie",
    "pcie_bench",
    "pcie_7260",
    "iwl7260",
    "iwl7260_scan",
    "iwl7260_join",
    "iwl7260_dataplane",
    "iwl7260_net",
    "usb_xhci",
    "usb_ehci",
    "rtw89",
    "rtw89_scan",
    "rtw89_join",
    "rtw89_dataplane",
    "rtl8xxxu",
    "rtl8xxxu_scan",
    "rtl8xxxu_join",
    "rtl8xxxu_dataplane",
    "rtl8xxxu_net",
)

# Every anchor is one line of a long capture: the patterns are line-oriented.
FLAGS = re.MULTILINE

# One pattern per claim.  The patterns match the anchor text, not a rewrite of
# it: a log line that does not match is an anchor the image did not print.
PATTERNS = {
    "banner": re.compile(r"rtos: banner backend=(?P<backend>\S+) kernel=(?P<kernel>.+?) build=(?P<build>\S+)$", re.MULTILINE),
    "kernel_ready": re.compile(r"rtos: kernel ready tick=(?P<tick>\d+)Hz", re.MULTILINE),
    "result": re.compile(r"rtos: result (?P<result>pass|fail) cases=(?P<ok>\d+)/(?P<total>\d+) backend=(?P<backend>\S+)", re.MULTILINE),
    "tick_ladder": re.compile(r"tick: ladder beats=(?P<beats>\d+) err_max_ms=(?P<err>-?\d+) tol_ms=(?P<tol>\d+)", re.MULTILINE),
    "tick_pass": re.compile(r"tick: PASS", re.MULTILINE),
    "cmsis_checks": re.compile(r"cmsis2: checks=(?P<checks>\d+) failures=(?P<failures>\d+)", re.MULTILINE),
    "cmsis_pass": re.compile(r"cmsis2: PASS", re.MULTILINE),
    "mutex": re.compile(r"cmsis2: mutex acquire=(?P<acq>\d+) wait_us=(?P<wait>\d+) hold_us=(?P<hold>\d+) try_us=(?P<try>\d*)", re.MULTILINE),
    "flags": re.compile(r"cmsis2: flags or=(?P<or>0x[0-9a-f]+) and=(?P<and>0x[0-9a-f]+) timeout=(?P<timeout>0x[0-9a-f]+)", re.MULTILINE),
    "net_link": re.compile(r"net: link up speed=(?P<speed>\d+) duplex=(?P<duplex>\d+)", re.MULTILINE),
    "net_addr": re.compile(r"net: addr (?P<addr>\d+\.\d+\.\d+\.\d+/\d+) mac=(?P<mac>[0-9a-f:]+)", re.MULTILINE),
    "net_arp": re.compile(r"net: arp (?P<ip>\d+\.\d+\.\d+\.\d+) -> (?P<mac>[0-9a-f:]+)", re.MULTILINE),
    "net_ping": re.compile(r"net: ping tx=(?P<tx>\d+) rx=(?P<rx>\d+) of=(?P<of>\d+) seen=(?P<seen>\d+)", re.MULTILINE),
    "net_down": re.compile(r"net: iperf down done=(?P<done>\d+) bytes=(?P<bytes>\d+) ms=(?P<ms>\d+) kbit=(?P<kbit>\d+)", re.MULTILINE),
    "net_up": re.compile(r"net: iperf up done=(?P<done>\d+) bytes=(?P<bytes>\d+) ms=(?P<ms>\d+) kbit=(?P<kbit>\d+)", re.MULTILINE),
    "net_pass": re.compile(r"net: PASS", re.MULTILINE),
    "emmc_cid": re.compile(r"emmc: cid manufacturer=(?P<manufacturer>\S+) oem=(?P<oem>\S+) product=(?P<product>\S*) rev=(?P<rev>\S+) serial=(?P<serial>\S+) date=(?P<date>\S+)", re.MULTILINE),
    "emmc_csd": re.compile(r"emmc: csd structure=(?P<structure>\d+) spec=(?P<spec>\d+) size=(?P<size>\d+) read_bl_len=(?P<rbl>\d+)", re.MULTILINE),
    "emmc_extcsd": re.compile(r"emmc: extcsd sectors=(?P<sectors>\d+) rev=(?P<rev>\d+) structure=(?P<structure>\d+) type=(?P<type>\S+) bus_width=(?P<bus>\d+) hs_timing=(?P<hs>\d+)", re.MULTILINE),
    "emmc_mode": re.compile(r"emmc: cardsize blocks=(?P<blocks>\d+) clock=(?P<clock>\d+)Hz width=(?P<width>\d+) timing=(?P<timing>\d+) cmd23=(?P<cmd23>\d)", re.MULTILINE),
    "emmc_rw": re.compile(r"emmc: rw crc_w=(?P<w>0x[0-9a-f]+) crc_r=(?P<r>0x[0-9a-f]+) verify=(?P<verify>\w+)", re.MULTILINE),
    "emmc_loop": re.compile(r"emmc: loop n=(?P<n>\d+) errors=(?P<errors>\d+)", re.MULTILINE),
    "emmc_badlba": re.compile(r"emmc: badlba lba=\d+ write_rc=(?P<w>-?\d+) read_rc=(?P<r>-?\d+) nohang=(?P<nohang>\w+)", re.MULTILINE),
    "emmc_pass": re.compile(r"emmc: PASS", re.MULTILINE),
    # 8189FTV vendor-stack case: the SDIO identity, the driver's netdev, and the
    # hardware address the driver read from the chip.
    "wlan_sdio": re.compile(r"wlan: sdio up card=(?P<vendor>[0-9a-fA-F]{4}):(?P<device>[0-9a-fA-F]{4}) funcs=(?P<funcs>\d+) cccr=(?P<cccr>\d+) clock=(?P<clock>\d+) width=(?P<width>\d+) hs=(?P<hs>\d+)", re.MULTILINE),
    "wlan_netdev_up": re.compile(r"wlan: netdev up rc=(?P<rc>-?\d+)", re.MULTILINE),
    "wlan_attach": re.compile(r"wlan: attach mac=(?P<mac>[0-9a-fA-F:]+)", re.MULTILINE),
    "wlan_netdev_addr": re.compile(r"wlan: netdev addr=(?P<mac>[0-9a-fA-F:]+)", re.MULTILINE),
    "wlan_stats": re.compile(r"wlan: sdio stats cards=(?P<cards>\d+) drivers=(?P<drivers>\d+) probes=(?P<probes>\d+) io_direct=(?P<direct>\d+) io_ext=(?P<ext>\d+) errors=(?P<errors>\d+)", re.MULTILINE),
    "wlan_scan": re.compile(r"wlan: scan rows=(?P<rows>\d+) ssid=(?P<ssid>\S*) bssid=(?P<bssid>[0-9a-fA-F:]+) freq=(?P<freq>\d+) signal=(?P<signal>-?\d+)", re.MULTILINE),
    "wlan_join": re.compile(r"wlan: join state=(?P<state>\d+) bssid=(?P<bssid>[0-9a-fA-F:]+) status=(?P<status>\d+)", re.MULTILINE),
    "wlan_ping": re.compile(r"wlan: ping tx=(?P<tx>\d+) rx=(?P<rx>\d+)", re.MULTILINE),
    "wlan_pass": re.compile(r"wlan: PASS", re.MULTILINE),
    "wlan_fail": re.compile(r"wlan: FAIL (?P<reason>.+)$", re.MULTILINE),
    # PCIe/NVMe ladder (B0/B1): the root complex, the endpoint list, the
    # namespace geometry and the message-interrupt delivery.  A completion
    # without a delivery is not a PASS: the CQE is written either way.
    "pcie_ctrl": re.compile(
        r"pcie: ctrl (?P<n>\d+) link up, downstream bus (?P<bus>\d+)",
        re.MULTILINE,
    ),
    "pcie_nvme": re.compile(
        r"nvme0: (?P<vid>[0-9a-fA-F]{4}):(?P<did>[0-9a-fA-F]{4}) at 0x(?P<bar>[0-9a-fA-F]+), "
        r"ns1: (?P<blocks>\d+) blocks of (?P<sector>\d+) bytes",
        re.MULTILINE,
    ),
    "pcie_irq": re.compile(
        r"nvme0: message interrupts delivered \(intid (?P<intid>\d+), (?P<so_far>\d+) so far\)",
        re.MULTILINE,
    ),
    "pcie_delta": re.compile(r"pcie: irqs=(?P<before>\d+)->(?P<after>\d+) completions=(?P<completions>\d+)", re.MULTILINE),
    "pcie_platform": re.compile(r"pcie: platform-inactive (?P<reason>.+)$", re.MULTILINE),
    "pcie_bench_platform": re.compile(r"pcie-bench: platform-inactive (?P<reason>.+)$", re.MULTILINE),
    # Throughput-round instruments (measurement only: they are read out for the
    # ledger, they never turn a pass into a fail).
    "net_rtt": re.compile(r"net: rtt n=(?P<n>\d+) min_us=(?P<min>\d+) avg_us=(?P<avg>\d+) max_us=(?P<max>\d+)", re.MULTILINE),
    "emmc_clk": re.compile(r"emmc: clk asked=(?P<asked>\d+)Hz achieved=(?P<achieved>\d+)Hz src_sel=(?P<sel>\d+) clk_ctrl=0x(?P<clk_ctrl>[0-9a-f]+) host_ctrl1=0x(?P<host_ctrl1>[0-9a-f]+) lines=(?P<lines>\d+) extcsd_bus=(?P<bus>\d+)", re.MULTILINE),
    "emmc_census": re.compile(r"emmc: census (?P<tag>\S+) transfers=(?P<transfers>\d+) blocks=(?P<blocks>\d+) boundaries=(?P<boundaries>\d+) data_polls=(?P<polls>\d+) cmd_us=(?P<cmd_us>\d+) data_us=(?P<data_us>\d+)", re.MULTILINE),
    # The wireless ladder's own instruments, once per boot.  `wq=-` is a
    # system workqueue that was never created, i.e. every schedule_work() in
    # the driver silently dropped; the two lkpi lines are the driver's half of
    # the completion paths whose bridge half is in the scan-done / join-ok
    # lines.
    "iwl7260_core": re.compile(
        r"iwl7260: core wq=(?P<wq>\S+) runs=(?P<runs>\d+) "
        r"full=(?P<full>\d+) max=(?P<max>\d+)",
        re.MULTILINE,
    ),
    "iwl7260_scan_lkpi": re.compile(
        r"iwl7260: scan lkpi completions=(?P<completions>\d+) "
        r"tx=(?P<tx>\d+) wq=(?P<wq>\d+)",
        re.MULTILINE,
    ),
    "iwl7260_join_lkpi": re.compile(
        r"iwl7260: join lkpi tx=(?P<tx>\d+) scan=(?P<scan>\d+) wq=(?P<wq>\d+)",
        re.MULTILINE,
    ),
    # USB mass-storage rows (A stage) and the RTL8851BU ladder (B stage).
    "usb_msc_dev": re.compile(
        r"usbh: msc bus=(?P<bus>\d+) hcd=(?P<hcd>\w+) vid=(?P<vid>[0-9a-f]{4}) "
        r"pid=(?P<pid>[0-9a-f]{4}) speed=(?P<speed>\d+) blocks=(?P<blocks>\d+) "
        r"bsize=(?P<bsize>\d+) serial=(?P<serial>\S*)",
        re.MULTILINE,
    ),
    "usb_msc_ep": re.compile(
        r"usbh: msc ep in=(?P<in_ep>0x[0-9a-f]{2}) type=(?P<in_type>\w+) "
        r"maxp=(?P<in_mps>\d+) out=(?P<out_ep>0x[0-9a-f]{2}) "
        r"type=(?P<out_type>\w+) maxp=(?P<out_mps>\d+)",
        re.MULTILINE,
    ),
    "usb_msc_blk0": re.compile(r"usbh: msc blk0 ok", re.MULTILINE),
    "usb_msc_roundtrip": re.compile(
        r"usbh: msc roundtrip ok lba=(?P<lba>\d+) len=(?P<len>\d+)",
        re.MULTILINE,
    ),
    "usb_msc_pass": re.compile(r"usbh: msc PASS", re.MULTILINE),
    "usb_msc_fail": re.compile(r"usbh: msc FAIL (?P<reason>.+)$", re.MULTILINE),
    "rtw89_buses": re.compile(r"rtw89: usb buses=(?P<buses>\S+)", re.MULTILINE),
    "rtw89_claim": re.compile(
        r"lkpi: .*lkpi claim attempt (?P<vid>[0-9a-f]{4}):(?P<pid>[0-9a-f]{4})",
        re.MULTILINE,
    ),
    # the driver's own sink prefix ("lkpiu: ") rides between the case tag and
    # the firmware line; accept it with or without.
    "rtw89_fw": re.compile(
        r"rtw89: fw (?:lkpiu: )?Firmware version (?P<version>\S+)",
        re.MULTILINE,
    ),
    "rtw89_attach": re.compile(
        r"rtw89: attach ok after (?P<ms>\d+) ms mac=(?P<mac>[0-9a-f:]+)",
        re.MULTILINE,
    ),
    "rtw89_start": re.compile(
        r"rtw89: start ok attached=(?P<attached>\d+) rx=(?P<rx>\d+)",
        re.MULTILINE,
    ),
    "rtw89_usb_stats": re.compile(
        r"rtw89: usb submits=(?P<submits>\d+) completes=(?P<completes>\d+) "
        r"refused=(?P<refused>\d+) stage_overflow=(?P<stage_overflow>\d+) "
        r"slots_exhausted=(?P<slots_exhausted>\d+)",
        re.MULTILINE,
    ),
    "rtw89_fail": re.compile(r"rtw89: FAIL (?P<reason>.+)$", re.MULTILINE),
    "rtw89_scan_start": re.compile(
        r"rtw89: scan start state=(?P<state>-?\d+)", re.MULTILINE
    ),
    "rtw89_scan_done": re.compile(
        r"rtw89: scan done rows=(?P<rows>\d+) bcn=(?P<bcn>\d+) "
        r"bcnT=(?P<bcnT>\d+) prsp=(?P<prsp>\d+) fcs=(?P<fcs>\d+) "
        r"scan=(?P<starts>\d+)/(?P<completes>\d+) state=(?P<state>-?\d+) "
        r"entered=(?P<entered>\d+) close=(?P<close>\d+) rx=(?P<rx>\d+)",
        re.MULTILINE,
    ),
    "rtw89_scan_hit": re.compile(
        r"rtw89: scan hit bssid=(?P<bssid>[0-9a-f:]+) ssid=(?P<ssid>\S+) "
        r"freq=(?P<freq>\d+) rssi=(?P<rssi>-?\d+)",
        re.MULTILINE,
    ),
    "rtw89_scan_lkpi": re.compile(
        r"rtw89: scan lkpi completions=(?P<completions>\d+) tx=(?P<tx>\d+) "
        r"wq=(?P<wq>\d+)",
        re.MULTILINE,
    ),
    "rtw89_scan_fail": re.compile(
        r"rtw89: scan FAIL (?P<reason>.+)$", re.MULTILINE
    ),
    "rtw89_join_start": re.compile(
        r"rtw89: join start ssid=(?P<ssid>\S+) bssid=(?P<bssid>[0-9a-f:]+) "
        r"state=(?P<state>-?\d+) rc=(?P<rc>-?\d+)",
        re.MULTILINE,
    ),
    "rtw89_join_ok": re.compile(
        r"rtw89: join ok state=(?P<state>\d+) mgt_tx=(?P<mgt_tx>\d+) "
        r"txc=(?P<txc>\d+) rx=(?P<rx>\d+)",
        re.MULTILINE,
    ),
    "rtw89_join_fail": re.compile(
        r"rtw89: join FAIL (?P<reason>.+)$", re.MULTILINE
    ),
    "rtw89_join_lkpi": re.compile(
        r"rtw89: join lkpi tx=(?P<tx>\d+) scan=(?P<scan>\d+) wq=(?P<wq>\d+)",
        re.MULTILINE,
    ),
    "rtw89_data_arp": re.compile(
        r"rtw89: data arp reply src=(?P<src>[0-9a-f:]+) tx=(?P<tx>\d+)",
        re.MULTILINE,
    ),
    "rtw89_data_ping": re.compile(
        r"rtw89: data ping tx=(?P<tx>\d+) rx=(?P<rx>\d+) "
        r"min_ms=(?P<min>-?\d+) max_ms=(?P<max>-?\d+) lost=(?P<lost>\S*)",
        re.MULTILINE,
    ),
    "rtw89_data_counters": re.compile(
        r"rtw89: data counters data_tx=(?P<data_tx>\d+) "
        r"tx_done=(?P<tx_done>\d+) rx=(?P<rx>\d+) rx_data=(?P<rx_data>\d+)",
        re.MULTILINE,
    ),
    "rtw89_data_fail": re.compile(
        r"rtw89: data FAIL (?P<reason>.+)$", re.MULTILINE
    ),
    # --- RTL8188EUS (rtl8xxxu) ladder -------------------------------------
    # The bring-up: the controller list, the class hook's claim line (echoed,
    # not required), the firmware revision, the MAC and the start state.
    "rtl8xxxu_buses": re.compile(r"rtl8xxxu: usb buses=(?P<buses>\S+)", re.MULTILINE),
    "rtl8xxxu_bus_started": re.compile(
        r"rtl8xxxu: usb bus=(?P<bus>\d+) hcd=(?P<hcd>\w+) "
        r"base=0x(?P<base>[0-9a-f]+) started",
        re.MULTILINE,
    ),
    "rtl8xxxu_claim": re.compile(
        r"rtl8xxxu: claim .*0bda:8179", re.MULTILINE
    ),
    # The driver's device-name prefix is whatever the linuxkpi printk's %s
    # carries (it can be empty on this port), so the anchor is matched from
    # "Firmware revision" and not from a fixed prefix.
    "rtl8xxxu_fw": re.compile(
        r"rtl8xxxu: fw .*Firmware revision "
        r"(?P<version>\d+\.\d+) \(signature 0x(?P<sig>[0-9a-fA-F]+)\)",
        re.MULTILINE,
    ),
    "rtl8xxxu_fw_image": re.compile(
        r"rtl8xxxu: fw image .*Loading firmware (?P<name>\S+)", re.MULTILINE
    ),
    "rtl8xxxu_attach": re.compile(
        r"rtl8xxxu: attach ok after (?P<ms>\d+) ms mac=(?P<mac>[0-9a-f:]+)",
        re.MULTILINE,
    ),
    "rtl8xxxu_start": re.compile(
        r"rtl8xxxu: start ok attached=(?P<attached>\d+) rx=(?P<rx>\d+)",
        re.MULTILINE,
    ),
    "rtl8xxxu_usb_stats": re.compile(
        r"rtl8xxxu: usb submits=(?P<submits>\d+) completes=(?P<completes>\d+) "
        r"refused=(?P<refused>\d+) stage_overflow=(?P<stage_overflow>\d+) "
        r"slots_exhausted=(?P<slots_exhausted>\d+)",
        re.MULTILINE,
    ),
    "rtl8xxxu_fail": re.compile(r"rtl8xxxu: FAIL (?P<reason>.+)$", re.MULTILINE),
    # scan: the software sweep's window and its table
    "rtl8xxxu_scan_start": re.compile(
        r"rtl8xxxu: scan start state=(?P<state>-?\d+)", re.MULTILINE
    ),
    "rtl8xxxu_scan_done": re.compile(
        r"rtl8xxxu: scan done rows=(?P<rows>\d+) bcn=(?P<bcn>\d+) "
        r"bcnT=(?P<bcnT>\d+) prsp=(?P<prsp>\d+) fcs=(?P<fcs>\d+) "
        r"scan=(?P<starts>\d+)/(?P<completes>\d+) state=(?P<state>-?\d+) "
        r"entered=(?P<entered>\d+) close=(?P<close>\d+) rx=(?P<rx>\d+)",
        re.MULTILINE,
    ),
    "rtl8xxxu_scan_hit": re.compile(
        r"rtl8xxxu: scan hit bssid=(?P<bssid>(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}) "
        r"ssid=(?P<ssid>\S*) freq=(?P<freq>\d+) rssi=(?P<rssi>-?\d+)",
        re.MULTILINE,
    ),
    "rtl8xxxu_scan_lkpi": re.compile(
        r"rtl8xxxu: scan lkpi completions=(?P<completions>\d+) "
        r"tx=(?P<tx>\d+) wq=(?P<wq>\d+)",
        re.MULTILINE,
    ),
    "rtl8xxxu_scan_fail": re.compile(
        r"rtl8xxxu: scan FAIL (?P<reason>.+)$", re.MULTILINE
    ),
    # join: the scan-table lookup, the exchange and its completion
    "rtl8xxxu_join_bssid": re.compile(
        r"rtl8xxxu: join bssid=(?P<bssid>(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}) "
        r"ssid=(?P<ssid>\S+) freq=(?P<freq>-?\d+) rssi=(?P<rssi>-?\d+)",
        re.MULTILINE,
    ),
    "rtl8xxxu_join_start": re.compile(
        r"rtl8xxxu: join start ssid=(?P<ssid>\S+) "
        r"bssid=(?P<bssid>(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}) "
        r"state=(?P<state>-?\d+) rc=(?P<rc>-?\d+) attempt=(?P<attempt>\d+)",
        re.MULTILINE,
    ),
    "rtl8xxxu_join_lkpi": re.compile(
        r"rtl8xxxu: join lkpi tx=(?P<tx>\d+) scan=(?P<scan>\d+) "
        r"wq=(?P<wq>\d+) rx_in=(?P<rx_in>\d+) rx_ref=(?P<rx_ref>\d+) "
        r"rx_drop=(?P<rx_drop>\d+) rx_frames=(?P<rx_frames>\d+) "
        r"rx_q=(?P<rx_q>\d+)",
        re.MULTILINE,
    ),
    "rtl8xxxu_join_ok": re.compile(
        r"rtl8xxxu: join ok state=(?P<state>\d+) mgt_tx=(?P<mgt_tx>\d+) "
        r"txc=(?P<txc>\d+) rx=(?P<rx>\d+)",
        re.MULTILINE,
    ),
    "rtl8xxxu_join_fail": re.compile(
        r"rtl8xxxu: join FAIL (?P<reason>.+)$", re.MULTILINE
    ),
    # the raw-L2 data plane: one ARP exchange and the echo series
    "rtl8xxxu_data_arp": re.compile(
        r"rtl8xxxu: data arp reply src=(?P<src>(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}) "
        r"tx=(?P<tx>\d+)",
        re.MULTILINE,
    ),
    "rtl8xxxu_data_ping": re.compile(
        r"rtl8xxxu: data ping tx=(?P<tx>\d+) rx=(?P<rx>\d+) "
        r"min_ms=(?P<min_ms>\d+) max_ms=(?P<max_ms>\d+) lost=(?P<lost>\S*)",
        re.MULTILINE,
    ),
    "rtl8xxxu_data_counters": re.compile(
        r"rtl8xxxu: data counters data_tx=(?P<data_tx>\d+) "
        r"tx_done=(?P<tx_done>\d+) rx=(?P<rx>\d+) rx_data=(?P<rx_data>\d+)",
        re.MULTILINE,
    ),
    "rtl8xxxu_data_fail": re.compile(
        r"rtl8xxxu: data FAIL (?P<reason>.+)$", re.MULTILINE
    ),
    # the IP presentation layer: netif up, ICMP, one TCP session each way
    "rtl8xxxu_net_up": re.compile(
        r"rtl8xxxu: net up ip=(?P<ip>\d+\.\d+\.\d+\.\d+/\d+) "
        r"mac=(?P<mac>(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}) mtu=(?P<mtu>\d+)",
        re.MULTILINE,
    ),
    "rtl8xxxu_net_ping": re.compile(
        r"rtl8xxxu: net ping tx=(?P<tx>\d+) rx=(?P<rx>\d+) "
        r"min_ms=(?P<min_ms>\d+) max_ms=(?P<max_ms>\d+)",
        re.MULTILINE,
    ),
    "rtl8xxxu_net_down": re.compile(
        r"rtl8xxxu: net iperf down done=(?P<done>\d+) bytes=(?P<bytes>\d+) "
        r"ms=(?P<ms>\d+) kbit=(?P<kbit>\d+)",
        re.MULTILINE,
    ),
    "rtl8xxxu_net_up_done": re.compile(
        r"rtl8xxxu: net iperf up done=(?P<done>\d+) bytes=(?P<bytes>\d+) "
        r"ms=(?P<ms>\d+) kbit=(?P<kbit>\d+)",
        re.MULTILINE,
    ),
    "rtl8xxxu_net_counters": re.compile(
        r"rtl8xxxu: net counters tx=(?P<tx>\d+) txb=(?P<txb>\d+) "
        r"txfail=(?P<txfail>\d+) rx=(?P<rx>\d+) rxr=(?P<rxr>\d+) "
        r"rxfail=(?P<rxfail>\d+) bridge_tx=(?P<bridge_tx>\d+) "
        r"bridge_done=(?P<bridge_done>\d+) bridge_rx=(?P<bridge_rx>\d+)",
        re.MULTILINE,
    ),
    "rtl8xxxu_net_fail": re.compile(
        r"rtl8xxxu: net FAIL (?P<reason>.+)$", re.MULTILINE
    ),
}

# The anchors that repeat once per phase / per sweep size: collected as lists so
# a phase's absence stays visible as an absence and a second run does not hide
# the first.
LIST_PATTERNS = {
    "net_phase_begin": re.compile(r"net: phase (?P<n>\d+) begin proto=(?P<proto>\S+) role=(?P<role>\S+) port=(?P<port>\d+) window_ms=(?P<window>\d+) name=(?P<name>\S+)", re.MULTILINE),
    "net_phase_done": re.compile(r"net: phase (?P<n>\d+) done name=(?P<name>\S+) ok=(?P<ok>\d+)", re.MULTILINE),
    "net_census": re.compile(r"net: census .*", re.MULTILINE),
    "net_loopback": re.compile(r"net: loopback proto=(?P<proto>\S+) bytes=(?P<bytes>\d+) ms=(?P<ms>\d+) kbit=(?P<kbit>\d+) done=(?P<done>\d+)", re.MULTILINE),
    "net_udp_sink": re.compile(r"net: udp sink port=(?P<port>\d+) pkts=(?P<pkts>\d+) bytes=(?P<bytes>\d+) ms=(?P<ms>\d+) kbit=(?P<kbit>\d+)", re.MULTILINE),
    "net_udp_blast": re.compile(r"net: udp blast port=(?P<port>\d+) sent=(?P<sent>\d+) pkts=(?P<pkts>\d+) ms=(?P<ms>\d+) kbit=(?P<kbit>\d+)", re.MULTILINE),
    "emmc_sweep": re.compile(r"emmc: sweep (?P<dir>read|write) chunk=(?P<kib>\d+) KiB xfers=(?P<xfers>\d+) bytes=(?P<bytes>\d+) us=(?P<us>\d+) kibs=(?P<kibs>\d+)", re.MULTILINE),
    # Observation-image evidence: read out for the ledger, never a pass gate.
    "pcie_bench_median": re.compile(
        r"pcie-bench: median bs=(?P<bs>\d+) cmd_bytes=(?P<cmd_bytes>\d+) "
        r"qd=(?P<qd>\d+) dir=(?P<dir>\S+) mib_s=(?P<mib_s>\d+) us=(?P<us>\d+)",
        re.MULTILINE,
    ),
    "pcie_7260_dev": re.compile(
        r"pcie-7260: dev (?P<bdf>[0-9a-fA-F:.]+) 8086:08b1 class 0280 rev 0x(?P<rev>[0-9a-fA-F]+)",
        re.MULTILINE,
    ),
    "pcie_7260_bar": re.compile(
        r"pcie-7260: bar0=0x(?P<bar>[0-9a-fA-F]+) hw_rev=0x(?P<rev>[0-9a-fA-F]+) "
        r"type=0x(?P<type>[0-9a-fA-F]+)",
        re.MULTILINE,
    ),
    "pcie_7260_vec": re.compile(
        r"pcie-7260: (?P<kind>msix|msi) vectors=(?P<n>\d+) intid=(?P<intid>\d+)",
        re.MULTILINE,
    ),
    "pcie_7260_lnk": re.compile(
        r"pcie-7260: lnksta speed=(?P<speed>\d+) width=(?P<width>\d+) "
        r"lnkctl=0x(?P<ctl>[0-9a-fA-F]+) aspm=(?P<aspm>\d+)",
        re.MULTILINE,
    ),
    "iwl7260_attach": re.compile(r"iwl7260: attach ok after (?P<ms>\d+) ms", re.MULTILINE),
    "iwl7260_start": re.compile(
        r"iwl7260: start ok attached=(?P<attached>\d+) rx=(?P<rx>\d+)", re.MULTILINE
    ),
    "iwl7260_alive": re.compile(
        r"loaded firmware version (?P<version>\S+)", re.MULTILINE
    ),
    "iwl7260_platform": re.compile(
        r"iwl7260: platform-inactive (?P<reason>.+)$", re.MULTILINE
    ),
    # the ladder cases: scan -> open join -> raw-L2 dataplane
    "iwl7260_scan_start": re.compile(
        r"iwl7260: scan start state=-?\d+", re.MULTILINE
    ),
    "iwl7260_scan_done": re.compile(
        r"iwl7260: scan done rows=(?P<rows>\d+) bcn=(?P<bcn>\d+) "
        r"bcnT=(?P<bcnT>\d+) prsp=(?P<prsp>\d+) fcs=(?P<fcs>\d+) "
        r"scan=(?P<starts>\d+)/(?P<completes>\d+) state=-?\d+ "
        r"entered=(?P<entered>\d+) close=(?P<close>\d+) rx=(?P<rx>\d+)",
        re.MULTILINE,
    ),
    "iwl7260_scan_hit": re.compile(
        r"iwl7260: scan hit bssid=(?P<bssid>(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}) "
        r"ssid=(?P<ssid>\S*) freq=(?P<freq>\d+) rssi=(?P<rssi>-?\d+)",
        re.MULTILINE,
    ),
    "iwl7260_scan_fail": re.compile(
        r"iwl7260: scan FAIL (?P<reason>.+)$", re.MULTILINE
    ),
    "iwl7260_join_start": re.compile(
        r"iwl7260: join start ssid=(?P<ssid>\S+) "
        r"bssid=(?P<bssid>(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}) "
        r"state=-?\d+ rc=(?P<rc>-?\d+)",
        re.MULTILINE,
    ),
    "iwl7260_join_ok": re.compile(
        r"iwl7260: join ok state=(?P<state>\d+) mgt_tx=(?P<mgt_tx>\d+) "
        r"txc=(?P<txc>\d+) rx=(?P<rx>\d+)",
        re.MULTILINE,
    ),
    "iwl7260_join_fail": re.compile(
        r"iwl7260: join FAIL (?P<reason>.+)$", re.MULTILINE
    ),
    "iwl7260_data_arp": re.compile(
        r"iwl7260: data arp reply src=(?P<src>(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}) "
        r"tx=(?P<tx>\d+)",
        re.MULTILINE,
    ),
    "iwl7260_data_ping": re.compile(
        r"iwl7260: data ping tx=(?P<tx>\d+) rx=(?P<rx>\d+) "
        r"min_ms=(?P<min_ms>\d+) max_ms=(?P<max_ms>\d+) lost=(?P<lost>\S*)",
        re.MULTILINE,
    ),
    "iwl7260_data_counters": re.compile(
        r"iwl7260: data counters data_tx=(?P<data_tx>\d+) "
        r"tx_done=(?P<tx_done>\d+) rx=(?P<rx>\d+) rx_data=(?P<rx_data>\d+)",
        re.MULTILINE,
    ),
    "iwl7260_data_fail": re.compile(
        r"iwl7260: data FAIL (?P<reason>.+)$", re.MULTILINE
    ),
    # the IP presentation layer over the bridge: lwIP up, ICMP, and one TCP
    # session in each direction against the bench's iperf peer.
    "iwl7260_net_up": re.compile(
        r"iwl7260: net up ip=(?P<ip>\d+\.\d+\.\d+\.\d+/\d+) "
        r"mac=(?P<mac>(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}) mtu=(?P<mtu>\d+)",
        re.MULTILINE,
    ),
    "iwl7260_net_ping": re.compile(
        r"iwl7260: net ping tx=(?P<tx>\d+) rx=(?P<rx>\d+) "
        r"min_ms=(?P<min_ms>\d+) max_ms=(?P<max_ms>\d+)",
        re.MULTILINE,
    ),
    "iwl7260_net_down": re.compile(
        r"iwl7260: net iperf down done=(?P<done>\d+) bytes=(?P<bytes>\d+) "
        r"ms=(?P<ms>\d+) kbit=(?P<kbit>\d+)",
        re.MULTILINE,
    ),
    "iwl7260_net_up_done": re.compile(
        r"iwl7260: net iperf up done=(?P<done>\d+) bytes=(?P<bytes>\d+) "
        r"ms=(?P<ms>\d+) kbit=(?P<kbit>\d+)",
        re.MULTILINE,
    ),
    "iwl7260_net_counters": re.compile(
        r"iwl7260: net counters tx=(?P<tx>\d+) txb=(?P<txb>\d+) "
        r"txfail=(?P<txfail>\d+) rx=(?P<rx>\d+) rxr=(?P<rxr>\d+) "
        r"rxfail=(?P<rxfail>\d+) bridge_tx=(?P<bridge_tx>\d+) "
        r"bridge_done=(?P<bridge_done>\d+) bridge_rx=(?P<bridge_rx>\d+)",
        re.MULTILINE,
    ),
    "iwl7260_net_pass": re.compile(r"iwl7260: net PASS", re.MULTILINE),
    "iwl7260_net_fail": re.compile(
        r"iwl7260: net FAIL (?P<reason>.+)$", re.MULTILINE
    ),
    "pcie_7260_platform": re.compile(
        r"pcie-7260: platform-inactive (?P<reason>.+)$", re.MULTILINE
    ),
    "pcie_bench_fit": re.compile(
        r"pcie-bench: fit qd=(?P<qd>\d+) dir=(?P<dir>\S+) c_us=(?P<c_us>-?\d+) "
        r"bw_mib_s=(?P<bw_mib_s>-?\d+) r2_permille=(?P<r2>-?\d+)",
        re.MULTILINE,
    ),
    "pcie_dev": re.compile(
        r"pcie: dev (?P<bdf>[0-9a-fA-F:.]+) (?P<vid>[0-9a-fA-F]{4}):(?P<did>[0-9a-fA-F]{4}) "
        r"class (?P<cc>[0-9a-fA-F]{4})",
        re.MULTILINE,
    ),
    "wlan_obs": re.compile(r"wlan: obs .*", re.MULTILINE),
    "wlan_udp": re.compile(r"wlan: udp .*", re.MULTILINE),
    "wlan_ping_seq": re.compile(r"wlan: ping (?:tx|rx) seq=\d+", re.MULTILINE),
    "usb_msc_io": re.compile(
        r"usbh: msc io (?P<op>read|write) size_kib=(?P<kib>\d+) "
        r"runs=(?P<runs>\d+) bytes=(?P<bytes>\d+) ms=(?P<ms>\d+) "
        r"rate=(?P<rate>\d+\.\d+) mib_s per_cmd_us=(?P<per_cmd>\d+)",
        re.MULTILINE,
    ),
    "usb_msc_sweep": re.compile(
        r"usbh: msc sweep (?P<op>read|write) chunk=(?P<kib>\d+) KiB "
        r"(?P<mib>\d+) MiB in (?P<ms>\d+) ms = (?P<rate>\d+\.\d+) MiB/s "
        r"per_cmd_us=(?P<per_cmd>\d+)",
        re.MULTILINE,
    ),
}


def parse(text: str) -> dict:
    """Collect the anchors that are present; absence stays absence."""
    found: dict = {"lines": text.splitlines()}
    for name, pattern in PATTERNS.items():
        match = pattern.search(text)
        found[name] = match.groupdict() if match else None
    for name, pattern in LIST_PATTERNS.items():
        found[name] = [m.groupdict() for m in pattern.finditer(text)]
    found["case_verdicts"] = {
        case: re.findall(rf"rtos: case {case} (PASS|FAIL)", text)
        for case in CASES
    }
    return found


def judge(text: str, expect: dict | None = None) -> dict:
    found = parse(text)
    expect = expect or {}
    errors: list[str] = []
    notes: list[str] = []

    banner = found["banner"]
    if not banner:
        errors.append("no banner line")
    else:
        if "backend" in expect and banner["backend"] != expect["backend"]:
            errors.append(f"backend {banner['backend']} != {expect['backend']}")
        if "build" in expect and banner["build"] != expect["build"]:
            errors.append(f"build {banner['build']} != {expect['build']}")

    if not found["kernel_ready"]:
        errors.append("no kernel-ready line")
    elif found["kernel_ready"]["tick"] != "1000":
        errors.append(f"tick frequency {found['kernel_ready']['tick']} Hz")

    cases: dict[str, dict] = {}
    for case in expect.get("cases", CASES):
        verdicts = found["case_verdicts"].get(case, [])
        if not verdicts:
            cases[case] = {"verdict": "missing", "detail": "no verdict line"}
        elif verdicts[-1] != "PASS":
            cases[case] = {"verdict": "fail", "detail": "case printed FAIL"}
        else:
            cases[case] = {"verdict": "pass", "detail": "case printed PASS"}

    # Per-case evidence checks: a PASS without its evidence anchors is a
    # broken image, not a pass.
    if cases.get("tick", {}).get("verdict") == "pass":
        ladder = found["tick_ladder"]
        if not ladder:
            errors.append("tick: PASS without a ladder line")
        else:
            notes.append(f"tick: err_max={ladder['err']}ms of {ladder['tol']}ms")

    if cases.get("cmsis2", {}).get("verdict") == "pass":
        checks = found["cmsis_checks"]
        if not checks:
            errors.append("cmsis2: PASS without a checks line")
        elif int(checks["failures"]) != 0:
            errors.append(f"cmsis2: {checks['failures']} failed checks")
        else:
            notes.append(f"cmsis2: {checks['checks']} checks")

    if cases.get("net", {}).get("verdict") == "pass":
        ping = found["net_ping"]
        if not ping:
            errors.append("net: PASS without a ping line")
        elif int(ping["rx"]) != int(ping["of"]):
            errors.append(f"net: ping {ping['rx']}/{ping['of']}")
        else:
            notes.append(f"net: ping {ping['rx']}/{ping['of']}")
        if found["net_addr"] is None:
            errors.append("net: no address line")
        for direction in ("net_down", "net_up"):
            row = found[direction]
            if not row or int(row["bytes"]) == 0:
                errors.append(f"{direction}: no bytes")
            else:
                notes.append(f"{direction}: {int(row['kbit'])} kbit/s over {row['ms']} ms")

        # The phase machine: every phase that is not the attribution-only
        # loopback has to have reported ok=1, and a phase that never closed is
        # a phase that never ran to its end.
        begun = {row["name"]: row for row in found["net_phase_begin"]}
        done = {row["name"]: row for row in found["net_phase_done"]}
        for name in begun:
            if name not in done:
                errors.append(f"net: phase {name} never closed")
            elif done[name]["ok"] != "1" and name != "loopback":
                errors.append(f"net: phase {name} reported ok=0")
        if begun:
            notes.append("net: phases " + " ".join(begun))
        if found["net_rtt"]:
            notes.append(
                f"net: rtt {found['net_rtt']['min']}/{found['net_rtt']['avg']}/"
                f"{found['net_rtt']['max']} us (min/avg/max, n={found['net_rtt']['n']})"
            )
        for row in found["net_loopback"]:
            notes.append(f"net: loopback {int(row['kbit'])} kbit/s (attribution only)")
        for row in found["net_udp_sink"]:
            notes.append(f"net: udp in {row['pkts']} pkts {row['bytes']} B in {row['ms']} ms")
        for row in found["net_udp_blast"]:
            notes.append(f"net: udp out {row['sent']} B offered in {row['ms']} ms (peer is the witness)")

    if cases.get("wlan", {}).get("verdict") == "pass":
        sdio = found["wlan_sdio"]
        if not sdio:
            errors.append("wlan: PASS without an sdio-up line")
        elif (sdio["vendor"].lower(), sdio["device"].lower()) != ("024c", "f179"):
            errors.append(
                f"wlan: card {sdio['vendor']}:{sdio['device']} != 024c:f179"
            )
        else:
            notes.append(
                f"wlan: card {sdio['vendor']}:{sdio['device']} "
                f"funcs={sdio['funcs']} cccr={sdio['cccr']} "
                f"clock={sdio['clock']} width={sdio['width']}"
            )
        up = found["wlan_netdev_up"]
        if not up:
            errors.append("wlan: PASS without a netdev-up line")
        elif int(up["rc"]) != 0:
            errors.append(f"wlan: netdev up rc={up['rc']}")
        attach = found["wlan_attach"]
        if not attach:
            errors.append("wlan: PASS without an attach line")
        else:
            mac = attach["mac"].lower()
            if mac in ("00:00:00:00:00:00", "0:0:0:0:0:0"):
                errors.append("wlan: hardware address is all zero")
            else:
                notes.append(f"wlan: attach mac={mac}")
        stats = found["wlan_stats"]
        if stats and int(stats["errors"]) != 0:
            errors.append(f"wlan: sdio errors={stats['errors']}")
        scan = found["wlan_scan"]
        if not scan:
            errors.append("wlan: PASS without a scan line")
        elif (int(scan["rows"]) < 1) or (scan["ssid"] != "lab8189b") or \
             (scan["bssid"].lower() == "00:00:00:00:00:00"):
            errors.append(
                f"wlan: scan rows={scan['rows']} ssid={scan['ssid']} "
                f"bssid={scan['bssid']}"
            )
        else:
            notes.append(
                f"wlan: scan rows={scan['rows']} ssid={scan['ssid']} "
                f"bssid={scan['bssid']} freq={scan['freq']} "
                f"signal={scan['signal']}"
            )
        join = found["wlan_join"]
        if not join:
            errors.append("wlan: PASS without a join line")
        elif (int(join["state"]) != 4) or (int(join["status"]) != 0):
            errors.append(
                f"wlan: join state={join['state']} status={join['status']}"
            )
        else:
            notes.append(f"wlan: join bssid={join['bssid']}")
        ping = found["wlan_ping"]
        if not ping:
            errors.append("wlan: PASS without a ping line")
        elif (int(ping["tx"]) != 10) or (int(ping["rx"]) != int(ping["tx"])):
            errors.append(f"wlan: ping {ping['rx']}/{ping['tx']}")
        else:
            notes.append(f"wlan: ping {ping['rx']}/{ping['tx']}")
        fail = found["wlan_fail"]
        if fail:
            errors.append(f"wlan: {fail['reason']}")

    if cases.get("iwl7260", {}).get("verdict") == "pass":
        if found["iwl7260_platform"]:
            cases["iwl7260"]["platform_inactive"] = found["iwl7260_platform"]["reason"]
            errors.append(
                f"iwl7260: platform-inactive ({found['iwl7260_platform']['reason']})"
            )
        attach = (found["iwl7260_attach"] or [None])[0]
        start = (found["iwl7260_start"] or [None])[0]
        alive = (found["iwl7260_alive"] or [None])[0]
        if not attach:
            errors.append("iwl7260: PASS without an attach line")
        else:
            notes.append(f"iwl7260: attach after {attach['ms']} ms")
        if not start:
            errors.append("iwl7260: PASS without a start line")
        elif int(start["attached"]) == 0:
            errors.append("iwl7260: start returned but not attached")
        else:
            notes.append(f"iwl7260: start attached={start['attached']} rx={start['rx']}")
        if not alive:
            errors.append("iwl7260: no firmware ALIVE evidence (loaded firmware version)")
        else:
            notes.append(f"iwl7260: firmware {alive['version']}")
        core = found["iwl7260_core"]
        if not core:
            errors.append("iwl7260: PASS without the core-services line")
        elif core["wq"] == "-":
            errors.append(
                "iwl7260: the linuxkpi system workqueue was never created "
                "(wq=-): every schedule_work() in the driver was dropped"
            )
        else:
            notes.append(
                f"iwl7260: core wq={core['wq']} runs={core['runs']} "
                f"max={core['max']}"
            )

    if cases.get("iwl7260_scan", {}).get("verdict") == "pass":
        done = (found["iwl7260_scan_done"] or [None])[0]
        hit = (found["iwl7260_scan_hit"] or [None])[0]
        fail = (found["iwl7260_scan_fail"] or [None])[0]
        if fail:
            errors.append(f"iwl7260_scan: {fail['reason']}")
        if not done:
            errors.append("iwl7260_scan: PASS without a scan-done line")
        else:
            if int(done["rows"]) == 0:
                errors.append("iwl7260_scan: empty scan table")
            if int(done["starts"]) < 1 or int(done["completes"]) < 1:
                errors.append("iwl7260_scan: the sweep never completed")
            if int(done["rx"]) == 0:
                errors.append("iwl7260_scan: no frame reached the stack")
            notes.append(
                f"iwl7260_scan: rows={done['rows']} bcn={done['bcn']} "
                f"prsp={done['prsp']} close={done['close']} rx={done['rx']}"
            )
        if not hit:
            errors.append("iwl7260_scan: target AP not in the table")
        elif (
            hit["bssid"].lower() != "d8:3a:dd:26:91:1e"
            or hit["ssid"] != "labwifi-brmc"
        ):
            errors.append(
                f"iwl7260_scan: hit {hit['bssid']}/{hit['ssid']} is not the bench AP"
            )
        else:
            notes.append(
                f"iwl7260_scan: hit {hit['ssid']} freq={hit['freq']} "
                f"rssi={hit['rssi']}"
            )
        lkpi = found["iwl7260_scan_lkpi"]
        if not lkpi:
            errors.append("iwl7260_scan: PASS without the driver-completion line")
        elif int(lkpi["completions"]) < 1:
            errors.append(
                "iwl7260_scan: the driver never completed the sweep "
                f"(lkpi=0 wq={lkpi['wq']})"
            )
        else:
            notes.append(
                f"iwl7260_scan: driver completions={lkpi['completions']} "
                f"tx={lkpi['tx']} wq={lkpi['wq']}"
            )

    if cases.get("iwl7260_join", {}).get("verdict") == "pass":
        start = (found["iwl7260_join_start"] or [None])[0]
        ok = (found["iwl7260_join_ok"] or [None])[0]
        fail = (found["iwl7260_join_fail"] or [None])[0]
        if not start:
            errors.append("iwl7260_join: PASS without a join start line")
        elif int(start["rc"]) != 0:
            errors.append(f"iwl7260_join: start rc={start['rc']}")
        if fail:
            errors.append(f"iwl7260_join: {fail['reason']}")
        if not ok:
            errors.append("iwl7260_join: PASS without a join-ok line")
        elif int(ok["state"]) != 4:
            errors.append(f"iwl7260_join: state={ok['state']} is not RUN")
        elif int(ok["mgt_tx"]) == 0:
            errors.append("iwl7260_join: RUN with no management frame")
        elif int(ok["txc"]) == 0:
            errors.append("iwl7260_join: no transmit completion")
        else:
            notes.append(
                f"iwl7260_join: RUN mgt_tx={ok['mgt_tx']} txc={ok['txc']} "
                f"rx={ok['rx']}"
            )
        lkpi = found["iwl7260_join_lkpi"]
        if not lkpi:
            errors.append("iwl7260_join: PASS without the driver-completion line")
        elif int(lkpi["tx"]) < 1:
            errors.append(
                "iwl7260_join: no completion reached the substitute "
                f"(tx=0 wq={lkpi['wq']})"
            )
        else:
            notes.append(
                f"iwl7260_join: driver tx={lkpi['tx']} scan={lkpi['scan']} "
                f"wq={lkpi['wq']}"
            )

    if cases.get("iwl7260_dataplane", {}).get("verdict") == "pass":
        arp = (found["iwl7260_data_arp"] or [None])[0]
        ping = (found["iwl7260_data_ping"] or [None])[0]
        cnt = (found["iwl7260_data_counters"] or [None])[0]
        fail = (found["iwl7260_data_fail"] or [None])[0]
        if fail:
            errors.append(f"iwl7260_dataplane: {fail['reason']}")
        if not arp:
            errors.append("iwl7260_dataplane: PASS without an arp reply")
        else:
            notes.append(f"iwl7260_dataplane: arp from {arp['src']}")
        if not ping:
            errors.append("iwl7260_dataplane: PASS without a ping line")
        elif int(ping["tx"]) != 10:
            errors.append(f"iwl7260_dataplane: ping tx={ping['tx']}")
        elif int(ping["rx"]) < 9:
            errors.append(f"iwl7260_dataplane: ping {ping['rx']}/{ping['tx']}")
        else:
            notes.append(
                f"iwl7260_dataplane: ping {ping['rx']}/{ping['tx']} "
                f"rtt={ping['min_ms']}-{ping['max_ms']} ms"
            )
        if not cnt:
            errors.append("iwl7260_dataplane: PASS without the counter line")
        elif int(cnt["data_tx"]) == 0 or int(cnt["rx_data"]) == 0:
            errors.append("iwl7260_dataplane: data counters are zero")
        else:
            notes.append(
                f"iwl7260_dataplane: data_tx={cnt['data_tx']} "
                f"rx_data={cnt['rx_data']}"
            )

    if cases.get("iwl7260_net", {}).get("verdict") == "pass":
        up = (found["iwl7260_net_up"] or [None])[0]
        ping = (found["iwl7260_net_ping"] or [None])[0]
        down = (found["iwl7260_net_down"] or [None])[0]
        up_done = (found["iwl7260_net_up_done"] or [None])[0]
        cnt = (found["iwl7260_net_counters"] or [None])[0]
        fail = (found["iwl7260_net_fail"] or [None])[0]
        if fail:
            errors.append(f"iwl7260_net: {fail['reason']}")
        if not up:
            errors.append("iwl7260_net: PASS without the netif line")
        elif up["ip"] != "192.168.99.2/24":
            errors.append(f"iwl7260_net: address {up['ip']} is not the bench net")
        else:
            notes.append(f"iwl7260_net: up {up['ip']} mac={up['mac']}")
        if not ping:
            errors.append("iwl7260_net: PASS without a ping line")
        elif int(ping["rx"]) < 9:
            errors.append(f"iwl7260_net: ping {ping['rx']}/{ping['tx']}")
        else:
            notes.append(
                f"iwl7260_net: ping {ping['rx']}/{ping['tx']} "
                f"rtt={ping['min_ms']}-{ping['max_ms']} ms"
            )
        if not down:
            errors.append("iwl7260_net: PASS without a downlink line")
        elif int(down["done"]) != 1 or int(down["bytes"]) == 0:
            errors.append(
                f"iwl7260_net: downlink carried {down['bytes']} bytes "
                f"(done={down['done']})"
            )
        else:
            notes.append(
                f"iwl7260_net: down {down['bytes']} bytes in {down['ms']} ms "
                f"= {down['kbit']} kbit/s"
            )
        if not up_done:
            errors.append("iwl7260_net: PASS without an uplink line")
        elif int(up_done["done"]) != 1 or int(up_done["bytes"]) == 0:
            errors.append(
                f"iwl7260_net: uplink carried {up_done['bytes']} bytes "
                f"(done={up_done['done']})"
            )
        else:
            notes.append(
                f"iwl7260_net: up {up_done['bytes']} bytes in "
                f"{up_done['ms']} ms = {up_done['kbit']} kbit/s"
            )
        if not cnt:
            errors.append("iwl7260_net: PASS without the counter line")
        elif int(cnt["tx"]) == 0 or int(cnt["rx"]) == 0:
            errors.append("iwl7260_net: netif counters are zero")
        else:
            notes.append(
                f"iwl7260_net: netif tx={cnt['tx']} rx={cnt['rx']} "
                f"txfail={cnt['txfail']} rxfail={cnt['rxfail']}"
            )

    if cases.get("pcie_7260", {}).get("verdict") == "pass":
        if found["pcie_7260_platform"]:
            cases["pcie_7260"]["platform_inactive"] = found["pcie_7260_platform"]["reason"]
            errors.append(
                f"pcie_7260: platform-inactive ({found['pcie_7260_platform']['reason']})"
            )
        dev = (found["pcie_7260_dev"] or [None])[0]
        bar = (found["pcie_7260_bar"] or [None])[0]
        vec = (found["pcie_7260_vec"] or [None])[0]
        lnk = (found["pcie_7260_lnk"] or [None])[0]
        if not dev:
            errors.append("pcie_7260: PASS without a 8086:08b1 device line")
        else:
            notes.append(f"pcie_7260: dev {dev['bdf']} rev={dev['rev']}")
        if not bar:
            errors.append("pcie_7260: PASS without a BAR/HW_REV line")
        elif bar["type"].lower() != "14":
            errors.append(f"pcie_7260: hw_rev type=0x{bar['type']} != 0x14")
        else:
            notes.append(
                f"pcie_7260: bar0=0x{bar['bar']} hw_rev=0x{bar['rev']}"
            )
        if not vec:
            errors.append("pcie_7260: PASS without a message-vector line")
        elif int(vec["intid"]) < 8192:
            errors.append(f"pcie_7260: intid={vec['intid']} is not an LPI")
        else:
            notes.append(
                f"pcie_7260: {vec['kind']} vectors={vec['n']} intid={vec['intid']}"
            )
        if not lnk:
            errors.append("pcie_7260: PASS without a link-status line")
        else:
            notes.append(
                f"pcie_7260: lnksta speed={lnk['speed']} width={lnk['width']} "
                f"aspm={lnk['aspm']}"
            )

    if cases.get("pcie_bench", {}).get("verdict") == "pass":
        if found["pcie_bench_platform"]:
            cases["pcie_bench"]["platform_inactive"] = found["pcie_bench_platform"]["reason"]
            errors.append(
                f"pcie_bench: platform-inactive ({found['pcie_bench_platform']['reason']})"
            )
        medians = found["pcie_bench_median"]
        if len(medians) < 12:
            errors.append(f"pcie_bench: {len(medians)}/12 cell medians")
        bad = [m for m in medians if int(m["mib_s"]) == 0]
        if bad:
            errors.append(f"pcie_bench: {len(bad)} cells with zero bandwidth")
        for m in medians:
            notes.append(
                f"pcie_bench: bs={m['bs']} cmd={m['cmd_bytes']} qd={m['qd']} "
                f"{m['dir']} {m['mib_s']} MiB/s"
            )
        if found["pcie_bench_fit"]:
            notes.append(f"pcie_bench: {len(found['pcie_bench_fit'])} fit rows")

    if cases.get("pcie", {}).get("verdict") == "pass":
        if found["pcie_platform"]:
            # A platform verdict is not a code pass; keep it visible so the
            # round can exclude the boot from the denominator.
            cases["pcie"]["platform_inactive"] = found["pcie_platform"]["reason"]
            errors.append(f"pcie: platform-inactive ({found['pcie_platform']['reason']})")
        ctrl = found["pcie_ctrl"]
        if not ctrl:
            errors.append("pcie: PASS without a controller link line")
        else:
            notes.append(f"pcie: ctrl={ctrl['n']} bus={ctrl['bus']}")
        nvme = found["pcie_nvme"]
        if not nvme:
            errors.append("pcie: PASS without an nvme geometry line")
        elif int(nvme["blocks"]) == 0 or int(nvme["sector"]) == 0:
            errors.append("pcie: namespace geometry is empty")
        else:
            notes.append(
                f"pcie: nvme {nvme['vid']}:{nvme['did']} at 0x{nvme['bar']} "
                f"{nvme['blocks']}x{nvme['sector']}B"
            )
        irq = found["pcie_irq"]
        delta = found["pcie_delta"]
        if not irq:
            errors.append("pcie: PASS without a message-interrupt line")
        elif int(irq["so_far"]) == 0:
            errors.append("pcie: no message interrupt delivered")
        else:
            notes.append(f"pcie: irq intid={irq['intid']} so_far={irq['so_far']}")
        if not delta:
            errors.append("pcie: PASS without an irq delta line")
        elif int(delta["after"]) <= int(delta["before"]):
            errors.append(
                f"pcie: irqs {delta['before']}->{delta['after']} (no delivery)"
            )
        else:
            notes.append(
                f"pcie: irqs {delta['before']}->{delta['after']} "
                f"completions={delta['completions']}"
            )
        devices = found["pcie_dev"]
        if not any(row["cc"].lower() == "0108" for row in devices):
            errors.append("pcie: no NVMe class function in the device list")
        else:
            notes.append(f"pcie: devices={len(devices)}")

    if cases.get("emmc", {}).get("verdict") == "pass":
        rw = found["emmc_rw"]
        if not rw or rw["verify"] != "ok":
            errors.append("emmc: readback mismatch")
        loop = found["emmc_loop"]
        if not loop or int(loop["errors"]) != 0:
            errors.append("emmc: loop errors")
        bad = found["emmc_badlba"]
        if not bad or bad["nohang"] != "yes":
            errors.append("emmc: error path did not return")
        for key in ("emmc_cid", "emmc_csd", "emmc_extcsd"):
            if found[key] is None:
                errors.append(f"{key}: no identity line")
            else:
                notes.append(f"{key}: {found[key]}")
        if found["emmc_clk"]:
            row = found["emmc_clk"]
            notes.append(
                f"emmc: clk asked={row['asked']} achieved={row['achieved']} "
                f"sel={row['sel']} clk_ctrl=0x{row['clk_ctrl']} "
                f"host_ctrl1=0x{row['host_ctrl1']} lines={row['lines']}"
            )
        if found["emmc_sweep"]:
            notes.append(f"emmc: sweep {len(found['emmc_sweep'])} points")
        if "emmc_product" in expect and found["emmc_cid"]:
            if found["emmc_cid"]["product"] != expect["emmc_product"]:
                errors.append("emmc: product name differs from the Linux readback")

    for case in ("usb_xhci", "usb_ehci"):
        if cases.get(case, {}).get("verdict") != "pass":
            continue
        fail = found["usb_msc_fail"]
        # a pattern without named groups matches to an empty dict, which is
        # falsy: absence is None, not falsiness, for these two anchors.
        if fail and found["usb_msc_pass"] is None:
            errors.append(f"{case}: {fail['reason']}")
        dev = found["usb_msc_dev"]
        if not dev:
            errors.append(f"{case}: PASS without the device identity line")
        else:
            if dev["vid"] == "0000" or dev["pid"] == "0000":
                errors.append(f"{case}: device id is 0000:0000")
            if int(dev["blocks"]) == 0:
                errors.append(f"{case}: disk geometry has zero blocks")
            if int(dev["bsize"]) != 512:
                errors.append(f"{case}: block size {dev['bsize']} != 512")
            notes.append(
                f"{case}: bus={dev['bus']} {dev['vid']}:{dev['pid']} "
                f"speed={dev['speed']} blocks={dev['blocks']}"
            )
        ep = found["usb_msc_ep"]
        if not ep:
            errors.append(f"{case}: PASS without the endpoint line")
        elif ep["in_type"] != "bulk" or ep["out_type"] != "bulk":
            # the P5-e root cause: a bulk endpoint programmed as interrupt
            # pins the rate to one packet per micro-frame
            errors.append(
                f"{case}: endpoint types in={ep['in_type']} out={ep['out_type']} "
                "(want bulk)"
            )
        else:
            notes.append(
                f"{case}: ep in={ep['in_ep']} out={ep['out_ep']} bulk"
            )
        if found["usb_msc_blk0"] is None:
            errors.append(f"{case}: block zero was never read")
        rt = found["usb_msc_roundtrip"]
        if not rt:
            errors.append(f"{case}: no verified write/read round trip")
        else:
            notes.append(
                f"{case}: roundtrip lba={rt['lba']} len={rt['len']}"
            )
        io = found["usb_msc_io"]
        ops = {row["op"] for row in io}
        for need in ("read", "write"):
            if need not in ops:
                errors.append(f"{case}: no {need} throughput arm")
        for row in io:
            if int(row["bytes"]) == 0:
                errors.append(f"{case}: {row['op']} {row['kib']} KiB moved 0 bytes")
            notes.append(
                f"{case}: {row['op']} {row['kib']} KiB {row['rate']} MiB/s "
                f"({row['per_cmd']} us/cmd)"
            )
        if len(found["usb_msc_sweep"]) < 4:
            errors.append(
                f"{case}: {len(found['usb_msc_sweep'])} sweep points"
            )
        else:
            notes.append(f"{case}: sweep {len(found['usb_msc_sweep'])} points")

    if cases.get("rtw89", {}).get("verdict") == "pass":
        if found["rtw89_fail"]:
            errors.append(f"rtw89: {found['rtw89_fail']['reason']}")
        claim = found["rtw89_claim"]
        if claim:
            if (claim["vid"].lower(), claim["pid"].lower()) != ("0bda", "b851"):
                errors.append(
                    f"rtw89: claimed {claim['vid']}:{claim['pid']} != 0bda:b851"
                )
            else:
                notes.append("rtw89: claimed 0bda:b851")
        else:
            errors.append("rtw89: no class-hook claim line")
        attach = found["rtw89_attach"]
        if not attach:
            errors.append("rtw89: PASS without an attach line")
        else:
            notes.append(
                f"rtw89: attach after {attach['ms']} ms mac={attach['mac']}"
            )
        fw = found["rtw89_fw"]
        if not fw:
            errors.append("rtw89: PASS without a firmware version line")
        else:
            notes.append(f"rtw89: firmware {fw['version']}")
        start = found["rtw89_start"]
        if not start:
            errors.append("rtw89: PASS without a start line")
        elif int(start["attached"]) == 0:
            errors.append("rtw89: start returned but not attached")
        else:
            notes.append(f"rtw89: start attached=1 rx={start['rx']}")
        stats = found["rtw89_usb_stats"]
        if not stats:
            errors.append("rtw89: PASS without the USB ledger line")
        else:
            if int(stats["refused"]) != 0:
                errors.append(f"rtw89: {stats['refused']} USB submits refused")
            if int(stats["stage_overflow"]) != 0:
                errors.append(
                    f"rtw89: {stats['stage_overflow']} transfers larger than the stage"
                )
            notes.append(
                f"rtw89: usb submits={stats['submits']} completes={stats['completes']} "
                f"stage_overflow={stats['stage_overflow']} "
                f"slots_exhausted={stats['slots_exhausted']}"
            )

    if cases.get("rtw89_scan", {}).get("verdict") == "pass":
        fail = found["rtw89_scan_fail"]
        if fail:
            errors.append(f"rtw89_scan: {fail['reason']}")
        done = found["rtw89_scan_done"]
        if not done:
            errors.append("rtw89_scan: PASS without a scan-done line")
        else:
            if int(done["rows"]) == 0:
                errors.append("rtw89_scan: empty scan table")
            if int(done["entered"]) < 1:
                errors.append("rtw89_scan: the stack never entered S_SCAN")
            if int(done["rx"]) == 0:
                errors.append("rtw89_scan: no frame reached the stack")
            if int(done["close"]) == 0:
                errors.append("rtw89_scan: the window never closed")
            notes.append(
                f"rtw89_scan: rows={done['rows']} bcn={done['bcn']} "
                f"prsp={done['prsp']} close={done['close']} rx={done['rx']}"
            )
        hit = found["rtw89_scan_hit"]
        if not hit:
            errors.append("rtw89_scan: target AP not in the table")
        elif (
            hit["bssid"].lower() != "d8:3a:dd:26:91:1e"
            or hit["ssid"] != "labwifi-brmc"
        ):
            errors.append(
                f"rtw89_scan: hit {hit['bssid']}/{hit['ssid']} is not the bench AP"
            )
        else:
            notes.append(
                f"rtw89_scan: hit {hit['ssid']} freq={hit['freq']} "
                f"rssi={hit['rssi']}"
            )
        lkpi = found["rtw89_scan_lkpi"]
        if not lkpi:
            errors.append("rtw89_scan: PASS without the driver-completion line")
        elif int(lkpi["completions"]) < 1:
            if not done or int(done["close"]) != 2:
                errors.append(
                    "rtw89_scan: neither the driver nor the walk closed the "
                    f"sweep (lkpi=0 close={done['close'] if done else '?'})"
                )
            else:
                notes.append("rtw89_scan: closed by the software walk")
        else:
            notes.append(
                f"rtw89_scan: driver completions={lkpi['completions']} "
                f"wq={lkpi['wq']}"
            )

    if cases.get("rtw89_join", {}).get("verdict") == "pass":
        start = found["rtw89_join_start"]
        if not start:
            errors.append("rtw89_join: PASS without a join start line")
        elif int(start["rc"]) != 0:
            errors.append(f"rtw89_join: start rc={start['rc']}")
        fail = found["rtw89_join_fail"]
        if fail:
            errors.append(f"rtw89_join: {fail['reason']}")
        ok = found["rtw89_join_ok"]
        if not ok:
            errors.append("rtw89_join: PASS without a join-ok line")
        elif int(ok["state"]) != 4:
            errors.append(f"rtw89_join: state={ok['state']} is not RUN")
        elif int(ok["mgt_tx"]) == 0:
            errors.append("rtw89_join: RUN with no management frame")
        elif int(ok["txc"]) == 0:
            errors.append("rtw89_join: no transmit completion")
        else:
            notes.append(
                f"rtw89_join: RUN mgt_tx={ok['mgt_tx']} txc={ok['txc']} "
                f"rx={ok['rx']}"
            )
        lkpi = found["rtw89_join_lkpi"]
        if not lkpi:
            errors.append("rtw89_join: PASS without the driver-completion line")
        elif int(lkpi["tx"]) < 1:
            errors.append(
                f"rtw89_join: no completion reached the substitute "
                f"(tx=0 wq={lkpi['wq']})"
            )

    if cases.get("rtw89_dataplane", {}).get("verdict") == "pass":
        fail = found["rtw89_data_fail"]
        if fail:
            errors.append(f"rtw89_dataplane: {fail['reason']}")
        arp = found["rtw89_data_arp"]
        if not arp:
            errors.append("rtw89_dataplane: no ARP reply")
        else:
            notes.append(f"rtw89_dataplane: arp reply src={arp['src']} tx={arp['tx']}")
        ping = found["rtw89_data_ping"]
        if not ping:
            errors.append("rtw89_dataplane: no ping line")
        elif int(ping["tx"]) != 10 or int(ping["rx"]) < 9:
            errors.append(f"rtw89_dataplane: ping {ping['rx']}/{ping['tx']}")
        else:
            notes.append(
                f"rtw89_dataplane: ping {ping['rx']}/10 rtt {ping['min']}-"
                f"{ping['max']} ms lost={ping['lost']}"
            )
        ctr = found["rtw89_data_counters"]
        if not ctr:
            errors.append("rtw89_dataplane: no data counter line")
        elif int(ctr["data_tx"]) == 0 or int(ctr["rx_data"]) == 0:
            errors.append(
                f"rtw89_dataplane: data_tx={ctr['data_tx']} rx_data={ctr['rx_data']}"
            )

    if cases.get("rtl8xxxu", {}).get("verdict") == "pass":
        fail = found["rtl8xxxu_fail"]
        if fail:
            errors.append(f"rtl8xxxu: {fail['reason']}")
        attach = found["rtl8xxxu_attach"]
        if not attach:
            errors.append("rtl8xxxu: PASS without an attach line")
        else:
            mac = attach["mac"].lower()
            if mac in ("00:00:00:00:00:00", "0:0:0:0:0:0"):
                errors.append("rtl8xxxu: hardware address is all zero")
            else:
                notes.append(f"rtl8xxxu: attach after {attach['ms']} ms mac={mac}")
        fw = found["rtl8xxxu_fw"]
        if not fw:
            errors.append("rtl8xxxu: no firmware revision line "
                          "(the probe did not reach the firmware report)")
        else:
            notes.append(f"rtl8xxxu: firmware {fw['version']} sig=0x{fw['sig']}")
        start = found["rtl8xxxu_start"]
        if not start:
            errors.append("rtl8xxxu: PASS without a start line")
        elif int(start["attached"]) == 0:
            errors.append("rtl8xxxu: start returned but not attached")
        else:
            notes.append(f"rtl8xxxu: start attached={start['attached']} "
                         f"rx={start['rx']}")
        if not found["rtl8xxxu_bus_started"]:
            errors.append("rtl8xxxu: no controller start line")

    if cases.get("rtl8xxxu_scan", {}).get("verdict") == "pass":
        fail = found["rtl8xxxu_scan_fail"]
        if fail:
            errors.append(f"rtl8xxxu_scan: {fail['reason']}")
        done = found["rtl8xxxu_scan_done"]
        hit = found["rtl8xxxu_scan_hit"]
        if not done:
            errors.append("rtl8xxxu_scan: PASS without a scan-done line")
        else:
            if int(done["rows"]) == 0:
                errors.append("rtl8xxxu_scan: empty scan table")
            if int(done["starts"]) < 1:
                errors.append("rtl8xxxu_scan: the sweep never started")
            if int(done["rx"]) == 0:
                errors.append("rtl8xxxu_scan: no frame reached the stack")
            notes.append(f"rtl8xxxu_scan: rows={done['rows']} bcn={done['bcn']} "
                         f"prsp={done['prsp']} close={done['close']} "
                         f"rx={done['rx']}")
        if not hit:
            errors.append("rtl8xxxu_scan: PASS without a bench-AP hit line")
        else:
            notes.append(f"rtl8xxxu_scan: hit {hit['bssid']} {hit['ssid']} "
                         f"{hit['freq']} MHz {hit['rssi']} dBm")

    if cases.get("rtl8xxxu_join", {}).get("verdict") == "pass":
        fail = found["rtl8xxxu_join_fail"]
        if fail:
            errors.append(f"rtl8xxxu_join: {fail['reason']}")
        ok = found["rtl8xxxu_join_ok"]
        if not ok:
            errors.append("rtl8xxxu_join: PASS without a join-ok line")
        else:
            if int(ok["state"]) != 4:
                errors.append(f"rtl8xxxu_join: state={ok['state']} (want RUN=4)")
            if int(ok["mgt_tx"]) == 0:
                errors.append("rtl8xxxu_join: no management frame reached the driver")
            if int(ok["txc"]) == 0:
                errors.append("rtl8xxxu_join: no transmit completion")
            notes.append(f"rtl8xxxu_join: state={ok['state']} "
                         f"mgt_tx={ok['mgt_tx']} txc={ok['txc']} rx={ok['rx']}")
        if not found["rtl8xxxu_join_bssid"]:
            errors.append("rtl8xxxu_join: no scan-table BSSID line")

    if cases.get("rtl8xxxu_dataplane", {}).get("verdict") == "pass":
        fail = found["rtl8xxxu_data_fail"]
        if fail:
            errors.append(f"rtl8xxxu_dataplane: {fail['reason']}")
        arp = found["rtl8xxxu_data_arp"]
        ping = found["rtl8xxxu_data_ping"]
        if not arp:
            errors.append("rtl8xxxu_dataplane: PASS without an ARP reply")
        else:
            notes.append(f"rtl8xxxu_dataplane: arp src={arp['src']} "
                         f"tx={arp['tx']}")
        if not ping:
            errors.append("rtl8xxxu_dataplane: PASS without a ping line")
        elif int(ping["tx"]) < 20 or int(ping["rx"]) < 19:
            errors.append(f"rtl8xxxu_dataplane: echo {ping['rx']}/{ping['tx']} "
                          f"(want 19 of 20)")
        else:
            notes.append(f"rtl8xxxu_dataplane: ping {ping['rx']}/{ping['tx']} "
                         f"{ping['min_ms']}-{ping['max_ms']} ms lost={ping['lost']}")

    if cases.get("rtl8xxxu_net", {}).get("verdict") == "pass":
        fail = found["rtl8xxxu_net_fail"]
        if fail:
            errors.append(f"rtl8xxxu_net: {fail['reason']}")
        up = found["rtl8xxxu_net_up"]
        if not up:
            errors.append("rtl8xxxu_net: PASS without a netif-up line")
        ping = found["rtl8xxxu_net_ping"]
        if not ping:
            errors.append("rtl8xxxu_net: PASS without a ping line")
        elif int(ping["rx"]) < 9:
            errors.append(f"rtl8xxxu_net: ping {ping['rx']}/{ping['tx']}")
        else:
            notes.append(f"rtl8xxxu_net: ping {ping['rx']}/{ping['tx']} "
                         f"{ping['min_ms']}-{ping['max_ms']} ms")
        for direction, name in (("down", "rtl8xxxu_net_down"),
                                ("up", "rtl8xxxu_net_up_done")):
            row = found[name]
            if not row or int(row["bytes"]) == 0:
                errors.append(f"rtl8xxxu_net: {direction} carried nothing")
            else:
                notes.append(f"rtl8xxxu_net: {direction} {row['kbit']} kbit/s "
                             f"over {row['ms']} ms ({row['bytes']} bytes)")

    result = found["result"]
    if not result:
        errors.append("no result line")
    else:
        notes.append(f"image result: {result['result']} {result['ok']}/{result['total']}")
        if result["result"] != "pass":
            errors.append("image reported a fail")

    return {
        "ok": not errors,
        "errors": errors,
        "notes": notes,
        "cases": cases,
        "anchors": {k: v for k, v in found.items() if k not in ("lines",)},
    }


def render(verdict: dict, title: str = "") -> str:
    out = [f"# judge: {title}" if title else "# judge", ""]
    out.append(f"- overall: **{'PASS' if verdict['ok'] else 'FAIL'}**")
    for case, row in verdict["cases"].items():
        out.append(f"- {case}: {row['verdict']} ({row['detail']})")
    for note in verdict["notes"]:
        out.append(f"- {note}")
    if verdict["errors"]:
        out.append("")
        out.append("## errors")
        for error in verdict["errors"]:
            out.append(f"- {error}")
    return "\n".join(out) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", type=Path)
    ap.add_argument("--json", type=Path, default=None)
    ap.add_argument("--title", default="")
    ap.add_argument("--expect", action="append", default=[])
    args = ap.parse_args()

    text = args.log.read_text(errors="replace")
    expect: dict = {}
    for item in args.expect:
        key, _, value = item.partition("=")
        if key == "cases":
            expect["cases"] = value.split(",")
        else:
            expect[key] = value

    verdict = judge(text, expect)
    print(render(verdict, args.title or str(args.log)), end="")
    if args.json:
        args.json.write_text(json.dumps(verdict, indent=2) + "\n")
    return 0 if verdict["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())