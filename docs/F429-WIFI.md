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

## 5. W6 (next round)

- Enable `LIBLINUX80211_NET_LWIP` for the F429 and size lwIP for the board:
  `MEM_SIZE` 128–256 KB in SDRAM, PBUF_POOL 24, TCP_SEG 32 KB? (32 double
  words), stacks 4–6 KB, `TCP_WND`/`SND_BUF` 8–16 KB, lwIP heap via the
  SDRAM `pvPortMalloc` seam.
- Attach the netif to `wlan0` (`lkpinetif` port), `192.168.99.2/24`, gateway
  `192.168.99.1`; ICMP 56/1400 B × 20 with 0 % loss as the C6 gate; then
  TCP down (`iperf2` client on the Pi → lwiperf server on the board), TCP up
  (lwiperf client, 10 s), and UDP 1440 blast/sink; report-only (no 80 % gate).
- Witnesses: AP `iw station dump` (rate/retransmits), monitor pcap per phase
  if the dwell lands, `lkpi_net_diag` counters on both sides.
