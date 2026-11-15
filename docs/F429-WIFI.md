# STM32F429 Wi-Fi — liblinux80211 + rtl8xxxu (RTL8188EUS) on an MCU

Date: 2026-11-15. Board: 野火 STM32F429 挑战者 V2 (STM32F429IGT6, 168 MHz,
1 MB flash, 192 KB SRAM + 64 KB CCM, 32 MB W9825G6 SDRAM on FMC bank 2).
Link: USB OTG_HS in **full-speed host** mode (PB14/PB15, J74–J76 on the HOS
side) → RTL8188EUS (`0bda:8179`, full speed, 1 interface, bulk IN EP1 +
bulk OUT EP2/EP3, no interrupt endpoint). Bench AP: Kali Pi 4B, RTL8812AU,
`labwifi-ar-ht20s` ch5 HT20 open, `192.168.99.1/24`; board static
`192.168.99.2/24` (W6).

The point of this platform: run the **real mac80211/cfg80211 + linuxkpi**
(`components/liblinux80211`) on a single-core Cortex-M4 with SDRAM, and see
how far a full Linux MAC layer gets on an MCU. The answer as of this round:
**W0–W5 pass** (SDRAM foundation, attach, scan, association); the data plane
(W6) is the next round. Throughput is report-only by plan.

## 1. Memory plan (the part that makes it possible)

| Region | Where | Why |
|---|---|---|
| Linux/heap allocations (`kmalloc`, `pvPortMalloc`) | `ucHeap` 4 MiB in `.sdram.heap` (NOLOAD) | mac80211's state, scan tables, sdata, assoc data are heap objects; 192 KB of internal SRAM cannot hold them |
| lwIP heap reserve (W6) | `.sdram.lwip`, 256 KB | same |
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
| `stm32f429-sdram` | W0: SDRAM init + self-test + a USB MSC DMA A/B arm when a U disk is present |
| `stm32f429-wifi-c3` | W3: attach — module init, USB driver probe, firmware, `wlan0`, `wiphy`, `dev_open` |
| `stm32f429-wifi-c4` | W4: directed scan on 2432 (wildcard retry), BSS anchor |
| `stm32f429-wifi-c5` | W5: connect (auth + assoc), power-save off, `PASS W5` |
| W6 | not yet defined: the C5 image + lwIP netif + ICMP/TCP/UDP phases |

Memory-relevant Kconfig: `F429_SDRAM`, `F429_HEAP_SDRAM` (heap in SDRAM),
`F429_WIFI_STAGE` (3–5), `LIBLINUX80211_*` (USB backend, WIRELESS,
RTL8XXXU, `NET_NONE` today), the SKB/USB slot counts (8/8, stage 4096 B × 4),
`RUNNER_STACK_KB=32`. The final c5 image is 679,024 B (< 1 MB flash) and its
serial RAM gate leaves ~133 KB of internal SRAM free with the SDRAM sections
NOLOAD.

Flash the **BIN**, not the ELF: mculab rejects the >1 MB debug ELF
(`IMAGE_TOO_LARGE`) but accepts `rtos-freertos.bin` with `--bin-base
0x08000000`.

## 3. The W5 defect and the tick supply (read this before porting another MCU)

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
wi-fi" and `runs/2026-11-15-f429-wifi-*/`.

- `f429_wifi: stage=`, `heap=sdram`, `core ready`, `usb backend ready`,
  `initcalls run`, `usb drivers=`, `fw registered=`
- `usb bus=0 hcd=dwc2 base=0x40040000 started`
- `wlan0 present=1 ifindex=… mac=…`, `wiphy bands=1 managed=1 ht=1 vht=0`
- `firmware rtlwifi/rtl8188eufw.bin …` (match by **name**, not by the first
  `Firmware revision` line; the bench can have two rtl8xxxu dongles)
- `start rc=0`, `scan ssid=… rc=0`, `bss ssid=… bssid=… freq=2432 signal=…`
- `connect ssid=… rc=0`, `connected bssid=… waited_ms=…`, `PASS W5`
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

## 5. W6 (data plane) — **PASS** (2026-11-15 evening)

**W6 is met**: one boot of the delivery image `stm32f429-wifi-c6` (726,712 B,
sha256 `42beedc00ed38d038dc7d09baa3d04edbcea6d377bc8cb70b4e91c7d439387fa`, the
W5 image plus the lwIP egress and the two platform fixes below) runs the full
gate, and the AP's own kernel is the receiver of record:

```
[   7.308] f429_wifi: ping warmup ret=0 rx=1
[   8.620] f429_wifi: ping small tx=20 rx=20 payload=56  rtt_ms=4/6/10   rc=0
[   9.596] f429_wifi: ping big   tx=20 rx=20 payload=1400 rtt_ms=6/10/29  rc=0
[   9.607] f429_wifi: net rx_frames=45 tx_frames=45 rx_bytes=31090 tx_bytes=31090
[   9.617] f429_wifi: PASS W6
[   9.627] f429_wifi: usb submits=171 completes=170 refused=0 stage_overflow=0
           slots_exhausted=0 rx=122 rx_err=0 tx=48 tx_err=0
[   9.654] rtos: result pass cases=1/1 backend=freertos
```

AP-side `tcpdump -i wlan2 icmp` for the same boot: **44 echo requests + 44 echo
replies** from/to `20:f4:1b:52:8f:01` (3 warmup 8 B, 21 x 56 B, 20 x 1400 B — the
case's exact traffic), i.e. every counted ping was on air and answered.

**Repeat count, stated exactly**: the census image (same W6 code path, plus the
read-only census) is **2/2** (`boot11`+`boot12`); the delivery image is **1/2**
(`boot13`), because the second boot ran into the platform trap recorded in `AGENTS.md` section 4.1 -
an MCU reset does not reset the 8188EUS, so a device left in a stuck bulk-IN
state fails at the *scan* with `rx_err=32` before W6 is reached.  A board power
cycle followed by one more boot is the outstanding step; it is an environment
action, not an image change.  Evidence: `runs/2026-11-15-f429-wifi-w6b/`.

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

### 5.3 Throughput: measured once, report-only, no table

The `stm32f429-wifi-c6t` image then ran the four report-only phases
(`CONFIG_F429_WIFI_TP=y`: tcpdn / tcpup / udpdn / udpup against the Pi's
iperf 2.2.1 and the UDP counting sink).  All four moved data in both boots;
the numbers are samples, not a table:

| phase | board's own count | peer's end-to-end view |
|---|---|---|
| tcpdn (Pi->board) | 9.04 MB / 15.0 s = 4.81 Mbit/s | 8.63 MB / 25.5 s = 2.84 Mbit/s |
| tcpup (board->Pi) | 4.61 MB / 10.0 s = 3.68 Mbit/s | 4.39 MB / 19.9 s = 1.85 Mbit/s |
| udpdn (Pi->board) | 15.5 MB / 20.3 s = 6.12 Mbit/s, 10,550 pkts, drop=0 | 14.7 MB / 18 s = 6.86 Mbit/s offered |
| udpup (board->Pi blast) | offered 241 KB / 12 s = 0.16 Mbit/s | sink sample (other boot): 960 KB / 653 pkts arrived, 90% of the 1.07 MB offered (the 12 s blast runs at ~0.64 Mbit/s) |

**How to read it**: this board's USB host is **full speed** (12 Mbit/s); a bulk
transfer tops out near 9.7 Mbit/s, so 6-7 Mbit/s of UDP is the *USB* ceiling
approaching, not the radio's.  The downlink TCP rate is stable across two boots
(2.78 / 2.84 Mbit/s); the uplink TCP and the UDP blast are not (0.36 vs 1.85
Mbit/s, 0.16 vs 0.71 Mbit/s offered), and neither has been explained yet.
These numbers do not transfer to the RK3568 line (high-speed USB) or to a GT
arm.  Full record and the harness caveats: `runs/2026-11-15-f429-wifi-tp/`.

Profiles: `stm32f429-wifi-c6` (pings, the W6 gate) and
`stm32f429-wifi-c6t` (`CONFIG_F429_WIFI_TP=y`, the four report-only phases:
tcp down/up + udp down/up against the Pi's iperf2 and UDP counting arms).
Throughput stays **report-only and unclaimed**: the W6 gate is the two ICMP
rounds above, not a number.
