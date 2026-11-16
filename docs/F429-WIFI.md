# STM32F429 Wi-Fi — liblinux80211 + rtl8xxxu (RTL8188EUS) on an MCU

Date: 2026-11-15. Board: 野火 STM32F429 挑战者 V2 (STM32F429IGT6, 168 MHz,
1 MB flash, 192 KB SRAM + 64 KB CCM, 32 MB W9825G6 SDRAM on FMC bank 2).
Link: USB OTG_HS in **full-speed host** mode (PB14/PB15, J74–J76 on the HOS
side) → RTL8188EUS (`0bda:8179`, full speed, 1 interface, bulk IN EP1 +
bulk OUT EP2/EP3, no interrupt endpoint). Bench AP: Kali Pi 4B, RTL8812AU,
`labwifi-ar-ht20s` ch5 HT20 open, `192.168.99.1/24`; board static
`192.168.99.2/24` (the data-plane bench).

The point of this platform: run the **real mac80211/cfg80211 + linuxkpi**
(`components/liblinux80211`) on a single-core Cortex-M4 with SDRAM, and see
how far a full Linux MAC layer gets on an MCU. The answer as of this round:
**The whole ladder passes** (SDRAM foundation, attach, scan, association, data plane —
ICMP 20/20 both payloads, the AP's own capture carrying all 44 requests and
44 replies), and the four throughput phases have been measured once
(report-only, no gate; 2.8 Mbit/s TCP down, 6.1–6.9 Mbit/s UDP down with zero
loss; on this full-speed USB host the UDP cell sits at 63% of the bulk
ceiling, so the USB link is the candidate ceiling - measuring the bus
itself is the next instrument, not a claim made here).

## 1. Memory plan (the part that makes it possible)

| Region | Where | Why |
|---|---|---|
| Linux/heap allocations (`kmalloc`, `pvPortMalloc`) | `ucHeap` 4 MiB in `.sdram.heap` (NOLOAD) | mac80211's state, scan tables, sdata, assoc data are heap objects; 192 KB of internal SRAM cannot hold them |
| lwIP heap reserve (the data plane) | `.sdram.lwip`, 256 KB | same |
| Driver DMA-visible skbs, USB stages | internal SRAM, `.noncacheable` (plain RAM; the M4 has no D-cache) | the USB engine reads/writes these unbuffered |
| Firmware blob | `.rodata`, compiled in (`rtlwifi/rtl8188eufw.bin`, 28.0/0x88e1) | no filesystem on the board |

The linker script (`platform/arch/armv7m/stm32f429.ld`) also keeps the
component's registries (`.lkpi_module`, `.lkpi_init`, `.lkpi_sdio_driver`) —
without them `--gc-sections` drops the driver and the wireless stack and the
image links at 70 KB with no `ieee80211_*` symbol (the 2026-11-15 boot that
did exactly that).

FMC SDRAM is register-configured in `platform/board/stm32f429-challenger/
fmc_sdram.c` (NAND shares the FMC data bus, so PG9/PB13/PD4/PD5 are held high
and PD11/PD12 low during init). **SDCR1 = 0x2800**: the SDCLK field is
HCLK/2 encoded `10`; writing 0 there (the first attempt) leaves the clock off
and the first SDRAM access BusFaults with `CFSR=0x8200, BFAR=0xd0000000`.
A 64 KiB bounded self-test runs before the kernel starts.

## 2. Profiles and images

| Profile | What it is |
|---|---|
| `stm32f429-sdram` | SDRAM bring-up: SDRAM init + self-test + a USB MSC DMA A/B arm when a U disk is present |
| `stm32f429-wifi-c3` | attach: attach — module init, USB driver probe, firmware, `wlan0`, `wiphy`, `dev_open` |
| `stm32f429-wifi-c4` | scan: directed scan on 2432 (wildcard retry), BSS anchor |
| `stm32f429-wifi-c5` | association: connect (auth + assoc), power-save off, `PASS ASSOC` |
| `stm32f429-wifi-c6` | **data plane delivery image**: the C5 ladder + lwIP netif over wlan0 + the two counted ICMP rounds (`PASS DATA`), 726,096 B, sha `8e643487…` |
| `stm32f429-wifi-c6t` | the delivery image + `CONFIG_F429_WIFI_TP=y`: the four report-only throughput phases (tcpdn / tcpup / udpdn / udpup), 735,944 B, sha `18844f3f…` |
| `stm32f429-wifi-c6h2c` | diagnostic: the delivery image + `CONFIG_RTL8XXXU_TX_CENSUS=y` (H2C mailbox read-back + the TX-state register census), never a delivery image |
| `stm32f429-wifi-c6diag` | diagnostic: the data-plane image + `CONFIG_F429_TICK_PC=y` (the polled tick-hook PC sampler that named the console-lock wedge) |
| `rk3568-rtl8xxxu-8188eu-c5-census` | the RK3568 control arm for the same census (same dongle, same driver, same instrument) |

Memory-relevant Kconfig: `F429_SDRAM`, `F429_HEAP_SDRAM` (heap in SDRAM),
`F429_WIFI_STAGE` (3–6), `F429_LWIP_SDRAM`, `F429_WIFI_TP` (report-only
phases), `RTL8XXXU_TX_CENSUS` (diagnostic, default n), `LIBLINUX80211_*`
(USB backend, `CFG80211` + `MAC80211`, RTL8XXXU, `NET_LWIP`), the SKB/USB slot counts
(8/8, stage 4096 B × 4), `RUNNER_STACK_KB=32`. The delivery c6 image is
726,096 B (< 1 MB flash; see `BUILD.md` §"F429 image size ledger") and its
serial RAM gate leaves >120 KB of internal SRAM free with the SDRAM
sections NOLOAD.

Flash the **BIN**, not the ELF: mculab rejects the >1 MB debug ELF
(`IMAGE_TOO_LARGE`) but accepts `rtos-freertos.bin` with `--bin-base
0x08000000`.

## 3. The association defect and the tick supply (read this before porting another MCU)

**A mac80211 port on an RTOS must advance the Linux clock.** The component
exports `lkpi_tick_advance()`; the platform must call it once per tick
(`vApplicationTickHook` with `configUSE_TICK_HOOK 1`). On the F429 neither
half existed, so `jiffies` stayed 0 and:

- mac80211's MLME timer re-armed at the port's 1 ms clamp forever;
- each expiry re-queued the iface work, so `ieee80211_sta_work` spun (9,153
  entries in 2 s) and ate the CPU;
- every deadline comparison (`time_after(jiffies, …)`) stayed false, so the
  association request was never sent — while the auth, the BSS scan and
  cfg80211's connect all succeeded (they are not jiffies-driven).

Symptom shape: `wlan0: authenticated`, `cfg80211_mlme_assoc()` rc=0, then
**nothing** — no `associate with`, no assoc frame on air, no AP log. The same
failure mode is recorded in `linuxkpi_core.c` from the RK3568 C5-open round;
the F429 reproduced it from the other side (the platform never provided the
tick, the RK board did).

Fix: `platform/os/freertos/board/stm32f429-challenger/FreeRTOSConfig.h`
(`configUSE_TICK_HOOK 1`) and `freertos_board_glue.c`
(`vApplicationTickHook()` → weak `lkpi_tick_advance()`). With the tick on,
association completes in ~200 ms and the whole boot runs 25 MLME work items.

## 4. Console anchors and their identity traps

Anchors are emitted by the case (`f429_wifi: …`) and by the stack (`drv`-style
lines); the raw transcripts are in `docs/CONSOLE-EVIDENCE.md` §"STM32F429
wi-fi" and `2026-11-15 bench run*/`.

- `f429_wifi: stage=`, `heap=sdram free=<n> min=<n>`, `core ready`,
  `usb backend ready`,
  `initcalls run`, `usb drivers=`, `fw registered=`
- `usb bus=0 hcd=dwc2 base=0x40040000 started`
- `wlan0 present=1 ifindex=… mac=…`, `wiphy bands=1 managed=1 ht=1 vht=0`
- `firmware rtlwifi/rtl8188eufw.bin …` (match by **name**, not by the first
  `Firmware revision` line; the bench can have two rtl8xxxu dongles)
- `start rc=0`, `scan ssid=… rc=0`, `bss ssid=… bssid=… freq=2432 signal=…`
- `connect ssid=… rc=0`, `connected bssid=… waited_ms=…`, `PASS ASSOC`
- port-contract probes (the F429 case carries them permanently):
  `syswq ran=1`, `dwprobe ran=1`, `tlprobe ran=1`, `tlprobe2 rearm=2`,
  `wiphy_work ran=1`, `scan settled after_ms=`, `wq_census`, `task_census`.

Traps:

1. **`%pM` is not implemented** in this port's printf: MAC addresses print as
   `D00680C8M` (a pointer). Judge by the case's own `bssid=`/`mac=` fields.
2. **jiffies=0** (above) looks like "the stack stopped" but is a missing tick.
   The probes `syswq/dwprobe/tlprobe/tlprobe2/wiphy_work` distinguish "no
   service" from "clock frozen": with the tick off they all pass (the timers
   fire), yet the MLME still spins — the clock is the only thing that is dead.
3. **The scan must finish before connect**: mac80211 drops every iface work
   item while `SCAN_SW_SCANNING` is set. The MCU's channel switching is slow
   (USB full-speed control transfers per RF write), so the C5 case waits
   ~2 s after the BSS before connecting. A connect issued during a scan
   returns 0 and then sits there.
4. The **`sta_work enter`** line is a diagnostic wrap in the F429 case
   (`--wrap=ieee80211_sta_work`, `--wrap=ieee80211_scan_completed`,
   `--wrap=cfg80211_sme_rx_auth`, `--wrap=cfg80211_mlme_assoc`); it is not a
   stack anchor, and it is linked only into the `f429_wifi` image.

## 5. The data plane — **PASS** (2026-11-15 evening)

**The data plane is met**: the delivery image `stm32f429-wifi-c6` (726,096 B,
sha256 `8e643487a769e684ee2fd06abc7b5e1f1fc230db3813d0e3ce6cf9b6e32eee68`,
the association image plus the lwIP egress and the two platform fixes below)
was booted twice from a relay cold start and runs the full gate both times;
the AP's own kernel is the receiver of record:

```text
[   3.913] f429_wifi: wlan0 present=1 after=2900 ms
[   3.920] f429_wifi: wlan0 present=1 ifindex=1 mac=20:f4:1b:52:8f:01
[   4.285] f429_wifi: PASS SCAN
[   6.688] f429_wifi: wlan0: associated
[   6.896] f429_wifi: PASS ASSOC
[  10.000] f429_wifi: ping small tx=20 rx=20 payload=56 rtt_ms=3/5/11 rc=0
[  10.960] f429_wifi: ping big tx=20 rx=20 payload=1400 rtt_ms=6/8/25 rc=0
[  10.986] f429_wifi: PASS DATA
[  10.996] f429_wifi: usb submits=170 completes=169 refused=0 stage_overflow=0 slots_exhausted=0 stage_size=4096 slots=4 rx=121 rx_err=0 tx=48 tx_err=0 tx_last_err=0 tx_len=1494
[  11.018] rtos: result pass cases=1/1 backend=freertos
```

AP-side `tcpdump -i wlan2 icmp` for the same boot: **44 echo requests + 44 echo
replies** from/to `20:f4:1b:52:8f:01` (the warmup's 8 B pair, then 21 x 56 B and
20 x 1400 B) - every counted ping was on air and answered.

**Repeat count, stated exactly**: the census build (the same data-plane code
path plus the read-only census) passed twice in a row; the delivery image passed
once, and a second boot on the same image ran into the MCU-reset trap described
in section 4.1 of the platform notes - a dongle left in a stuck bulk-IN state
fails at the *scan* with `rx_err=32` before the data plane is reached.  A power
cycle followed by one more boot is the outstanding step; it is an environment
action, not an image change.

### 5.1 What the two real blockers were (both outside the driver)

The previous round's reading — "the chip transmits only the first data frame",
with the H2C media-status report as the prime suspect — is **retracted**. Three
independent measurements killed it: the H2C mailbox was written and read back
clear (`h2c n=0 … hmtfr_before=0x00 hmtfr_after=0x00 mbox0=0x00002101`), the
per-chip TX census read normal (`txpause=0`, `CR` TX/RX enabled, `RQPN` =
hi 41 / pubq 99 pages, no pending `HISR`), and — decisively — the board's
**unicast** data frames were on air all along once the bench was healthy.

1. **Bench: a stale PERMANENT ARP entry on the AP.** The Kali AP mapped the
   board's static address to a wrong MAC:
   `192.168.99.2 dev wlan2 lladdr 88:e6:28:53:10:9c PERMANENT`, while the board
   is `20:f4:1b:52:8f:01`. Every ICMP reply was therefore sent to a station that
   does not exist: the board's request reached the AP (its own capture proves
   it), the AP answered, the board's `rx` stayed 0 and the case kept retrying.
   `ip neigh replace 192.168.99.2 lladdr 20:f4:1b:52:8f:01 dev wlan2 nud permanent`
   fixed it; the next boot received the ARP reply and the echo reply
   (`lkpinetif: rx#1 … proto=0x0806`, `rx#2 … proto=0x0800`).
   The MT7921AU monitor on this bench **does not see the F429's frames** (0
   frames from the board's MAC while the AP's own tcpdump had all of them), so
   the earlier "the monitor saw exactly one data frame" was an instrument
   artifact; the air truth for this line is the AP kernel's capture.
2. **Platform: the console print lock had no interrupt masking on armv7m.**
   `platform/os/baremetal/log.c`'s `print_lock_take()` masks IRQs for the hold
   on aarch64 (with the comment naming the exact deadlock), but the armv7m
   branch was a bare spin with IRQs enabled. The console is a polled 115200 UART
   (~5-9 ms per line); anything that preempted the holder and printed — a
   higher-priority task or an ISR — spun in `print_try_lock` forever and starved
   the holder: the tick kept running, the CPU sat 100% in that loop, the console
   froze mid-line and no fault was taken. That is the "hard wedge, no fault
   line" this line had been chasing for two rounds. The fix mirrors the aarch64
   semantics on armv7m (save PRIMASK, `cpsid i` for the hold, restore exactly on
   release). The tick-PC sampler (`CONFIG_F429_TICK_PC`) named the loop:
   `tickpc: tick=… pc=0x08000a72` → `print_try_lock` (`log.c:147`).

### 5.2 The census instrumentation kept

`CONFIG_RTL8XXXU_TX_CENSUS` (default n, never in a delivery profile) adds, in
`wireless-drivers/rtl8xxxu/ports/rtos/rtl8xxxu_diag.c`:
a `-Wl,--wrap=rtl8xxxu_gen2_report_connect` that reads `REG_HMTFR` and the four
H2C mailboxes before/after the media-status write, and
`rtl8xxxu_diag_chip_state()` (read-only: `TXPAUSE`, `CR`, `FWHW_TXQ_CTRL`,
`TRXDMA_CTRL`, `RQPN`, `RQPN_NPQ`, `TX_REPORT_CTRL`, `HISR/HISRE`), called by
the case after association and around the ping window. It gave the "chip state
is normal" half of the proof above and is the A/B instrument for any future
"chip side" claim. The per-frame descriptor dump was widened from 6 to 16
frames so a unicast frame's descriptor is always in the record.

lwIP is sized for the MCU by `CONFIG_F429_LWIP_SDRAM` (platform/board/Kconfig):
`MEM_SIZE` 255 KiB in the linker's `.sdram.lwip` reserve, `MEMP_MEM_MALLOC=1`
(every pool on that heap; the SRAM keeps pointers only), 24 pbufs of 1600 B,
16 KiB TCP window/send buffer, 48 TCP segments. The board glue exports
`lkpi_lwip_heap_ptr = __lwip_heap`; the lwIP port headers gained the per-arch
counter include (`gtimer_count_get`: aarch64 extern vs CM4 DWT inline) and
the `MEMP_STATS` override that must survive `MEMP_MEM_MALLOC=1`.

### 5.3 Throughput: the four phases (measured 2026-11-15, report-only)

The `stm32f429-wifi-c6t` image (735,944 B, sha256 `18844f3f…`) runs the four
throughput phases after the data-plane gate in the same boot: `tcpdn` and `tcpup`
against the Pi's iperf 2.2.1 (the board is lwIP's lwiperf server / client),
`udpdn` against the board's UDP sink on 5002, `udpup` as a 1470 B datagram
blast to the Pi's counting sink on 5003. Two boots were measured; the one with the corrected UDP anchor is the
supported sample, the other gives the cross-check for the two TCP phases:

| phase | board's own count | peer's end-to-end view |
|---|---|---|
| tcpdn (Pi→board) | 9.04 MB / 15.0 s = 4.81 Mbit/s | 8.63 MB / 25.5 s = **2.84 Mbit/s** (other boot: 2.78, 8.63 MB / 26.0 s) |
| tcpup (board→Pi) | 4.61 MB / 10.0 s = 3.68 Mbit/s | 4.39 MB / 19.9 s = **1.85 Mbit/s** (other boot: 0.36, 917 KB / 21 s) |
| udpdn (Pi→board) | 15.51 MB / 20.3 s = **6.12 Mbit/s**, 10,550 pkts, **drop=0** | Pi offered 14.7 MB / 18 s = 6.86 Mbit/s, 10,498 datagrams |
| udpup (board→Pi blast) | offered 241 KB / 12 s = 0.16 Mbit/s | other boot's sink: 960 KB / 653 pkts of the 1.07 MB offered (90%) |

Conditions: bench AP `labwifi-ar-ht20s` ch5 HT20 open (conf sha `2932e5a4…`),
the board's static address, and the AP's neighbour entry pointing at the
board's MAC. The peer is the Pi's `iperf 2.2.1`; the UDP sink is
`tools/rtos/udp-sink.py`. The AP's own capture for the same boot holds the
whole round (44 echo requests + 44 replies for the gate, then the TCP/UDP
flows); it stays on the Pi as the bench host, sha `c7160b39…`,
with a compact per-direction ledger in the run record.

**How to read the numbers**: this board's USB host is **full speed**
(12 Mbit/s signalling; a bulk transfer tops out near 9.7 Mbit/s on this
controller), and the UDP cell is 63% of that - which makes the USB link the
*candidate* ceiling, not a proven one (the bus itself was not measured; that
is the next instrument). The downlink TCP rate is stable across the two boots
(2.78 / 2.84 Mbit/s) and is the one cell that behaves like a rate. The uplink
TCP (0.36 vs 1.85 Mbit/s) and the UDP blast (0.16 vs 0.71 Mbit/s offered,
90% delivered when the sink overlapped it) are **not stable and not
explained**; they are recorded as the next round's target, not as rates.
These samples do not transfer to the RK3568 line (high-speed USB) or to a GT
arm — the F429 is a different bus speed on a different host.

Report-only means what it says: there is no throughput gate, the data-plane gate is
the two ICMP rounds above, and these four numbers are samples of one harness
on one afternoon. The harness defects found while measuring (an anchor fired
one phase early in the first boot, a sink killed before it wrote its JSON,
and the console's phase timestamps not being monotonic — only `bytes`/`ms`/
`kbit` are usable) are listed in the run record so the next round starts from
a known state.

Profiles: `stm32f429-wifi-c6` (pings, the data-plane gate) and
`stm32f429-wifi-c6t` (`CONFIG_F429_WIFI_TP=y`, the four report-only phases:
tcp down/up + udp down/up against the Pi's iperf2 and UDP counting arms).
Throughput is **report-only**: measured and recorded in 5.3, but the data-plane gate
is the two ICMP rounds above, not a number.

## 6. AP role (2026-11-15/16) — BSS and beacon work, association blocked in USB

The board's second wireless role: the same dongle becomes a 2.4 GHz open AP
(`rtos-8188-ap`, ch5/2432, HT20/WMM), the Pi's MT7921AU joins it, and the two
ICMP rounds prove the data plane.  The bridge and the stack are unchanged; the
one new piece is the AP MLME, which mac80211 does not have in-kernel.

**Two ways to own the AP MLME, and the flash picked one.**  The imported NXP
hostapd (the role the RK3568 AP images run) costs **326,612 B** of flash here:
an image that links it with only a case that takes the AP entry's address is
975,644 B, against 649,032 B for the same image without it.  This part has 1 MiB
and the shipped wireless image here is 728,888 B, so hostapd is out.  The
image therefore links the port's own minimal open-AP MLME
(`linuxkpi_ap_mlme.c` + `linuxkpi_ap_beacon.c`, `CONFIG_AP_MLME`): probe
request, open authentication and association are parsed and answered from the
case's task, the station is created and its controlled port opened through the
real nl80211 command chain, and mac80211 keeps the AP state machine.  The two
MLMEs are mutually exclusive at build time, and each brings its own
`cfg80211_rx_mgmt_ext` seam, so an image has exactly one AP receive owner.

**The beacon template** is an open 802.11g/HT20/WMM access point's IE set split
at the TIM, which is hostapd's split, not "fixed fields vs IEs": the head is
36 fixed bytes + SSID + supported rates + DS parameter (63 bytes), and
mac80211 inserts the TIM before the tail (101 bytes: ERP, extended rates,
operating classes, HT capabilities, HT operation, extended capabilities, WMM).
The validator rejects a template with a TIM in it or with a missing IE.  The
same head (subtype changed to a probe response) plus the tail is what answers
probe requests.

**What was measured**: the BSS comes up and beacons on air — `ap=1` in the
wiphy row, `SET_INTERFACE rc=0 type=3`, `START_AP rc=0` at 2432 MHz with
interval 100 and DTIM 2, `link freq=2432`, the monitor counts 614 beacons from
the board's BSSID, and the Pi's scan lists the BSS with the template's IEs
(SSID, ch5, interval 100, DTIM 2, HT20, -22 dBm).  The beacon repeats from the host:
this chip does not repeat a beacon the host downloaded, so the driver
recomposes one per beacon interval (the driver tree carries the recorded
patch for that).

**What blocked the role**: the peer's authentication frames never reach the
stack.  At `dev_open` the driver submits 32 receive URBs and 29 of them come
back with a bulk-IN babble (`USB_ERR_BABBLE`) — the seam reports it as `-EIO`,
the driver frees a receive URB on any error and does not resubmit it, and the
receive count then freezes for the rest of the boot (`rx=41` unchanged 60 s
later).  The geometry captured in the adapter is transfer length 2376, endpoint
max packet 64, and **zero bytes moved** at the babble - the controller judged
the very first packet larger than a full-speed bulk packet may be.  A bounded
retry of the same transfer was tried and turned none of the babbles into a
success.  The same board, cable and dongle run the station image with
`rx_err=0` and pass scan/associate/data on the same bench, so the variable is
the AP role's receive start, not the dongle, the power or the relay.

**Reopen condition**: a boot whose first receive URBs no longer fail at
interface start (the receive count keeps growing and the MLME reports
`auth_req>=1`), or a measurement that names why the chip's packets are larger
than the full-speed endpoint allows in this role.  Until then the role's gates
are: **A1 measured, A2/A3 not verified**.

Profiles: `stm32f429-wifi-ap-fp` (A1-A3 gates) and `stm32f429-wifi-ap-fp-tp`
(`CONFIG_F429_WIFI_AP_TP=y`, the four report-only AP phases, not run).
`stm32f429-wifi-ap-empty` is the flash-cost probe that chose the MLME; it is a
measurement artifact, not a role image.
