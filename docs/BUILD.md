# BUILD

The build is the previous line's: one Kconfig tree, one generated fragment, one
make entry, component-owned source lists. This document records what that
means in this repository and how a new world is added. The mechanisms are not
new; the worlds are.

## 1. One configuration channel

```
configs/<profile>.defconfig
  -> tools/build/kconfig-gen.py
  -> build/kconfig/<profile>/{freewificam_config, freewificam_config.h, .config}
  -> the make fragment is included by the platform Makefile
  -> the header is force-included by platform/build_defs.h
```

A profile is one defconfig under `tests/board/rtos/configs/`. `config.mk` only
names the default. The generated fragment and header are build outputs and are
git-ignored; nothing generated is committed.

**No configuration macro comes from a compile line.** `tools/build/check-no-d.sh`
fails the build when a compile or link command carries a `-D`; the header and
`build_defs.h` are the only sources. `check-build-contract.sh` enforces the
second half of the same rule: one `KCONTRACT_CFLAGS` definition, every C flag
world derives from it, and no world pins its own `-O` level.
`check-config-used.py` (part of `./make.rtos check`) fails when a Kconfig
symbol has no consumer at all: the size round found `HEAP_SIZE` (a ThreadX
link symbol no FreeRTOS image passed) and `USB_STAGE_SIZE` (no consumer)
that way.

## 2. Profiles

| Profile | Cases | What it accepts |
|---|---|---|
| `rk3568-tick` | tick | console, MMU, GICv3, FreeRTOS port, CMSIS-RTOS2 facade, runner |
| `rk3568-net` | tick cmsis2 net | lwIP + GMAC: link, address, ping, TCP both ways, UDP probes, census |
| `rk3568-pcie-nvme` | tick pcie | DWC root complex, ITS/MSI, NVMe namespace, one real I/O completion |
| `rk3568-emmc` | tick cmsis2 emmc | fsl_sdmmc + DW SDHCI: card ID, block round trip, error path, sweep |
| `rk3568-usb-msc` | tick usb_ehci | CherryUSB EHCI: enumerate the U-disk, block zero, round trip (identity arm on a small disk) |
| `stm32f429-usb-msc` | tick usb_dwc2 | STM32F429 OTG_HS in full-speed host mode + CherryUSB's DWC2 port + MSC: enumerate the U disk, read block 0 / eight blocks, then write and read back one 512 B sector in the disk's last 1 MiB window; verdict `usbh: msc PASS` |
| `stm32f429-uvc-detect` | uvc_detect | F429 OTG_HS full-speed host + CherryUSB's DWC2 port + the video class: identify the UVC camera, print its format/frame/alt table and raw interval descriptors, and probe the 720-line modes without opening a stream; the 720p feasibility census |
| `stm32f429-uvc-480` | uvc_detect uvc_open uvc_iso_smoke uvc_stream | the same host/class path opened at 640x480 MJPEG; the DWC2 isochronous engine's raw 16-URB smoke and a 20 s stream window; camera/frame pool in SDRAM |
| `stm32f429-uvc-800x600` | uvc_detect uvc_open uvc_iso_smoke uvc_stream | the same at 800x600 MJPEG, the Pro 9000's largest full-speed mode; report-only rate, same four-case gate |
| `stm32f429-uvc-720p` | uvc_detect uvc_open uvc_iso_smoke uvc_stream | the 1280x720 target image for a camera whose full-speed descriptor set actually contains a 720-line mode; the bench Pro 9000 does not, so its verdict for this camera is the descriptor-table negative |
| `stm32f429-eth` | f429_eth | F429's LAN8720A (RMII) on the STM32F4 ETH MAC, driven by the imported ST HAL ETH driver: PHY identity, the negotiated link, a DHCP lease from the bench router, the gateway's ARP and echo, name resolution (report only), and at least one public echo reply; verdict `f429_eth: PASS DATA` |
| `rk3568-kpi-live` | tick pcie kpi_live | liblinux80211's PCI engine over the board's real root complex |
| `rk3568-mac80211-c2` | tick mac80211_c2 | the real mac80211/cfg80211 stack registers a wiphy (C2); no PCI engine |
| `rk3568-iwl7260-mac80211` | tick pcie iwl7260_c3 | the same stack + the vendored iwlwifi (72 TUs) over the AC 7260: probe, firmware ALIVE, register, `dev_open` start (C3) |
| `rk3568-iwl7260-c4` | tick pcie iwl7260_c4 | C3's attach plus the real `NL80211_CMD_TRIGGER_SCAN` stream through nl80211: hw scan, BSS table, bench AP (C4) |
| `rk3568-iwl7260-c5` | tick pcie iwl7260_c5 | C4's scan plus `NL80211_CMD_CONNECT` through nl80211: open association, wl0 over wlan0, 56/1400 B ICMP 20/20 (C5/C6-open) |
| `rk3568-iwl7260-c6` | tick pcie iwl7260_c5 iwl7260_c6 | C5's join in the same boot, then the seven C6T throughput phases (loopback/tcpdn/udpdn repeats/tcpup/udpup) with the adapter and lwIP phase census |
| `rk3568-iwl7260-c6-pool128` | tick pcie iwl7260_c5 iwl7260_c6 | the same, plus `LKPI_SKB_DATA_POOL_NUM=128`; the accepted C6T lever (uplink 19.58 -> 23.18 Mbit/s, +18.4%) |
| `rk3568-iwl7260-stab` | tick pcie iwl7260_c5 iwl7260_stab | C5's join plus the stability case: 30-minute TCP downlink with a 30 s health line, then ten disconnect/scan/connect/ping cycles; carries the accepted pool128 configuration |
| `rk3568-iwl7260-ap` | tick pcie iwl7260_ap | the AC 7260 becomes the AP: `SET_INTERFACE` on the down interface, `START_AP` with the GT-derived beacon template, the AP phase table; A1 (beacon on air) reaches 2/2, association waits on the userspace AP MLME (docs/decisions/2026-11-11-ap-mode.md) |
| `rk3568-iwl7260-ap-wpa2` | tick pcie iwl7260_ap | the same AP with WPA2-PSK/CCMP (`CONFIG_AP_WPA2=y`): passphrase, 4-way, PTK+GTK `set_key`, `PASS APW`; it stops after A3 and does not run the A4 phase table (A4 is blocked independently) |
| `rk3568-iwl7260-ap-a4` | tick pcie iwl7260_ap | the A4 transmit-stall diagnostic (`CONFIG_AP_DIAG=y`, `CONFIG_LKPI_PCIE_CSR_SNAPSHOT=y`): the A1–A3 bring-up, then the post-ping probe, short UDP windows, the 1 s interrupt-freeze detector, NAPI/MMIO/CSR snapshots and the pre-registered `diag verdict`; its PASS line is `PASS APD`, the throughput arm stays the plain AP profile |
| `rk3568-iwl7260-ap-a4-idle` | tick pcie iwl7260_ap | the same diagnostic with `CONFIG_AP_DIAG_IDLE=y`: no peer, no station, the AP idles for 60 s and the run records the interrupt-delivery delta (the traffic arm's baseline, not a stall verdict) |
| `rk3568-iwl7260-ap5g` | tick pcie iwl7260_ap5g | the AP on the 5 GHz band (`CONFIG_AP_FREQ=5180`, `CONFIG_AP_HW_MODE="a"`, the 5 GHz round): the case reports the wiphy's 5 GHz band and every candidate channel's flags/beacon verdict, then starts hostapd with `hw_mode=a` on the frequency it found beaconable; `PASS AP5` |
| `rk3568-iwl7260-ap5g-wpa2` | tick pcie iwl7260_ap5g | the same with `CONFIG_AP_WPA2=y`: passphrase, 4-way, `ptk=1 gtk=1 fail=0`, `PASS AP5W` (P2) |
| `rk3568-iwl7260-sta5g` | tick pcie iwl7260_sta5g | the station on the 5 GHz band (`CONFIG_STA_FREQ=5180`, empty passphrase = open): the imported supplicant associates, the case reads the association's frequency back out of cfg80211 and fails unless it is 5 GHz, then 56/1400 B ICMP 20/20; `PASS STA5` (P3) |
| `rk3568-iwl7260-sta5g-wpa2` | tick pcie iwl7260_sta5g | the same with `CONFIG_STA_PSK` set: EAPOL both ways, `ptk=1 gtk=1 fail=0`, `PASS STA5W` (P4) |
| `rk3568-iwl7260-mon` | tick pcie iwl7260_mon | the AC 7260 as an RX-only monitor on `CONFIG_MON_FREQ` (2432) for `CONFIG_MON_WINDOW_MS` (15 s); the role change is `SET_INTERFACE(MONITOR)` with the device down, then `dev_open` **then** `SET_WIPHY(freq)` (cfg80211 refuses the channel set while no monitor is running), and the frames go to the component's monitor sink, never to lwIP; `PASS MON` requires >=8 beacons/s, `bad=0` radiotap, an unchanged lwIP `rx_frames` and `tx_packets=0` |
| `rk3568-kpi-mock` | tick kpi_mock | the same engines over a synthetic host/card and the null egress |
| `rk3568-rtl8189fs-sdio` | tick rtl8189fs_sdio | the board's SDIO 8189FTV attaches through the imported fsl_sdmmc stack to the vendored vendor stack: card `024c:f179`, embedded firmware `FW_NIC 20306`, MAC, wiphy + netdev, `dev_open` (attach) |
| `rk3568-rtl8189fs-sta` | tick rtl8189fs_sta | attach plus the real `NL80211_CMD_TRIGGER_SCAN`/`CONNECT` streams through nl80211, dispatched to the vendor driver's cfg80211 ops: scan, open association, lwIP netif, 56/1400 B ICMP both ways (scan, association and data) |
| `rk3568-rtl8189fs-throughput` | tick rtl8189fs_sta rtl8189fs_throughput | the same join, then the throughput phase table: 30 s sampled TCP downlink, 35 Mbit/s UDP downlink sink, TCP/UDP uplink, board-local loopback; each phase carries the adapter/stack/pool and the SDIO claim/pump + vendor queue gauges |
| `rk3568-rtl8189fs-udpdn` | tick rtl8189fs_sta rtl8189fs_throughput | the throughput diagnostic ordering `CONFIG_THROUGHPUT_UDP_FIRST`: the receive-only downlink is offered before any TCP phase, so one boot separates an RX-path stall from a transmit/receive concurrency stall |
| `rk3568-rtl8189fs-ap` | tick rtl8189fs_ap | the same card as an AP: the ported NXP `hostapd` brings `wlan0` up on `CONFIG_AP_FREQ` (2432) with a 100 TU beacon and an open BSS, the peer associates (association), both ICMP payload sizes cross in both directions (data) |
| `rk3568-rtl8189fs-ap-throughput` | tick rtl8189fs_ap rtl8189fs_throughput | the AP role plus the throughput phase table (`CONFIG_THROUGHPUT_PUMP_MODE=1`, `CONFIG_THROUGHPUT_PUMP_YIELD=y`): loopback, TCP downlink twice, the UDP downlink ladder 15/25/35, TCP uplink, UDP uplink; the runner runs a counting sink on the peer for each downlink rung |
| `rk3568-rtw89-8851bu-c3` | tick rtw89_c3 | the RTL8851BU (`0bda:b851`, USB) attaches through the linuxkpi USB engine and CherryUSB: the engine's `.lkpi_module` walk registers the driver, the board's USB domain starts the DWC3 xHCI pair (the card hangs on the instance at `0xFCC00000`), the driver's probe downloads `rtw8851b_fw-1.bin` from the compiled-in registry, `ieee80211_register_hw` creates `wlan0`, and `dev_open` reaches start (C3) |
| `rk3568-rtw89-8851bu-c4` | tick rtw89_c4 | attach plus the real `NL80211_CMD_TRIGGER_SCAN` stream through nl80211: mac80211 -> rtw89 -> the USB bulk endpoints, the BSS table carries the bench AP (C4) |
| `rk3568-rtw89-8851bu-c5` | tick rtw89_c5 | C4 plus `NL80211_CMD_CONNECT`: open association, the lwIP netif over `wlan0`, 56/1400 B ICMP 20/20 (C5/C6-open) |
| `rk3568-rtw89-8851bu-c6` | tick rtw89_c6 | the seven-phase C6T table over `wl0`: 30 s TCP down twice, 40 M UDP down twice, 12 s TCP up, 20 s UDP up (board-side blast), with the adapter/stack/pool and TX-health census per phase; the clean arm is 2/2 `PASS C6T`, downlink 0.75x GT and uplink 0.81x - the 2026-11-14 bench run |
| `rk3568-rtw89-8851bu-c6-diag` | tick rtw89_c6 | the C6T diagnostic arm (`CONFIG_RTW89_C6_SKIP_LOOPBACK=y`): the skb-queue owner walk, the WARN call-site/`memcmp` census and the pool/timer counters; it is attribution only and never a throughput verdict |
| `rk3568-rtw89-8851bu-ap` | tick rtw89_ap | the same USB card as an AP: `SET_INTERFACE(AP)`, the lwIP netif, the ported NXP `hostapd` start and beacon (100 TU, ch5/open), the Pi's MT7921AU joins, and 56/1400 B ICMP 20/20 both ways; `PASS AP` (A1-A3) - the 2026-11-14 bench run |
| `rk3568-rtw89-8851bu-ap-tp` | tick rtw89_ap | the AP role plus `CONFIG_RTW89_AP_TP`: the five-phase A4 table (tcpdn, tcpup, tcpdn2, 10 M udpdn, udpup) with the per-phase census; 2/2 `PASS APT`; the report's verdict is `fail` because TCP up is 78.4% - the 2026-11-14 bench run |
| `rk3568-rtw89-8851bu-ap-diag` | tick rtw89_ap | the A4 diagnostic arm (`CONFIG_RTW89_AP_STAGE`, `CONFIG_RTW89_AP_NO_MARGIN`, `CONFIG_APP_WATCHDOG`): three stage anchors per phase, no 3 s peer margin and a 2 s realtime watchdog that prints the core-lock owner and the lwIP heartbeat; it exists to name the an earlier A4 boot silence, not to pass a gate |
| `rk3568-rtw89-8851bu-mon` | tick rtw89_mon | the same card as an RX-only monitor: `SET_INTERFACE(MONITOR)` with the device down, `dev_open` (devtype 803), then `SET_WIPHY(2432)`; the frames go to the port's sink, not lwIP; `PASS MON` requires >=8 beacons/s, `bad=0`, unchanged lwIP `rx_frames` and `tx_packets=0` - the 2026-11-14 bench run |
| `rk3568-rtl8xxxu-8188eu-c3` | tick rtl8xxxu_c3 | the RTL8188EUS (`0bda:8179`, USB, behind the external hub on xHCI0/busid 2) attaches through the linuxkpi USB engine and CherryUSB: `.lkpi_module` registers the driver, the probe reads the efuse map, downloads `rtlwifi/rtl8188eufw.bin` from the compiled-in registry (`Firmware revision 28.0`, `signature 0x88e1`), `ieee80211_register_hw` creates `wlan0`, and `dev_open` reaches start (C3). The claim line carries `bus/hub/port`, which is how the round pins the topology |
| `rk3568-rtl8xxxu-8188eu-c4` | tick rtl8xxxu_c4 | attach plus the real `NL80211_CMD_TRIGGER_SCAN` stream: mac80211's SW scan drives rtl8xxxu's channel changes over the USB bulk endpoints, the BSS table carries `labwifi-ar-ht20s` at 2432 MHz (C4) |
| `rk3568-rtl8xxxu-8188eu-c5` | tick rtl8xxxu_c5 | C4 plus `NL80211_CMD_CONNECT`: open association, the lwIP netif over `wlan0` (`192.168.99.2/24`), power save off through the nl80211 handler, and 56 B / 1400 B ICMP 20/20 in both directions with the link held open for the host's AP->board arm (C5/C6-open) |
| `rk3568-rtl8xxxu-8188eu-ap` | tick rtl8xxxu_ap | the same card as a 2.4 GHz AP (`rtos-8188-ap`, ch5/2432, HT20, open) behind the hub on xHCI0/busid 3: the imported NXP hostapd owns the AP MLME and `SET_INTERFACE(AP)` + `START_AP` bring the BSS up; the board's MT7921AU peer joins and both ICMP payload sizes cross 20/20. The beacon is host-composed and host-sent every beacon interval (`host_beacon_repeat`, patch 0003) - the first host-sent beacon on this line; the monitor's beacon capture is A1's witness (A1-A3) |
| `rk3568-rtl8xxxu-8188eu-ap-arm4` | tick rtl8xxxu_ap | diagnostic arm, never a delivery profile: the AP profile plus a one-shot clear of `REG_BCN_CTRL` bit4 after the start-up beacon, asking whether that is the chip's missing beacon arming. It is not (the diagnostic AP arm still produced one beacon), which is what selected the host-repeat patch - the 2026-11-14 bench run |
| `rk3568-rtl8xxxu-8188eu-mon` | tick rtl8xxxu_mon | the same card as an RX-only monitor on ch5/2432: `SET_INTERFACE(MONITOR)` (the driver's fourth recorded patch, `supports_monitor` + `WANT_MONITOR_VIF` + the promiscuous RCR), `dev_open devtype=803`, `SET_WIPHY 2432`, then a 15 s window counted by the port's sink. The frames reach the sink (beacons/probes/data with mac80211's radiotap), never lwIP; the bench AP's BSSID is named from the sink's per-BSSID beacon census, and the A/B arm counts the same beacons with the Pi's MT7921AU monitor - the 2026-11-14 bench run |
| `rk3568-rtl8xxxu-8188eu-c6` | tick rtl8xxxu_c5 rtl8xxxu_c6 | the station plus the C6T throughput table (loopback, tcpdn, udpdn, tcpdn2, udpdn2, tcpup, udpup): the GT denominator is the same pin's in-tree `rtl8xxxu` on the same bench (same firmware blob), and the case's repeat phases are samples, not gates - the 2026-11-14 bench run |
| `rk3568-rtl8723bu-c3` | tick rtl8xxxu_c3 | the RTL8723BU (`0bda:b720`, USB, the board's EHCI dongle behind the `1a86:8091` hub at `bus=1 hub=2 port=2`, interface 2) attaches through the same linuxkpi USB engine: the probe reads `RTL8723BU rev E ... 1T1R, TX queues 3`, downloads `rtlwifi/rtl8723bu_nic.bin` from the compiled-in registry (`Firmware revision 35.0`, `signature 0x5301`), registers `wlan0` (MAC `88:e6:28:53:10:9c`) and `dev_open` reaches start (C3) |
| `rk3568-rtl8723bu-c4` | tick rtl8xxxu_c4 | attach plus the real `NL80211_CMD_TRIGGER_SCAN`: the SW scan finds `labwifi-ar-ht20s` at 2432 MHz (C4) |
| `rk3568-rtl8723bu-c5` | tick rtl8xxxu_c5 | C4 plus `NL80211_CMD_CONNECT`: open association, the lwIP netif over `wlan0` (`192.168.99.2/24`), power save off, and 56 B / 1400 B ICMP 20/20 both ways (C5/C6-open) |
| `rk3568-rtl8723bu-c6` | tick rtl8xxxu_c5 rtl8xxxu_c6 | the station plus the C6T table, compared with the same pin's in-tree `rtl8xxxu` on the same bench and firmware (the GT arm selects the 8723BU by MAC/firmware line; both dongles are on the board) - the 2026-11-15 bench run |
| `rk3568-rtl8xxxu-8188eu-ap-tp` | tick rtl8xxxu_ap | the AP role plus the A4 table with the station arm's four direction cells (`tcpdn` board-send 20 s, `tcpup` board-receive 30 s, `udpup` board-receive 10M, `udpdn` board-send 10M), so the two roles are comparable direction by direction; the round swaps the bench (8812AU as the client, MT7921AU as the monitor) - the 2026-11-14 bench run |

```
./make.rtos list
./make.rtos image PROFILE=rk3568-net
./make.rtos rk3568_ci          # clean build every profile, logs in build/ci/
```

The cross toolchain is `aarch64-none-elf-` on `PATH`, or
`AARCH64_CROSS_PATH=<prefix dir>`, or `CROSS_COMPILE=<prefix>`.

## 3. Components own their sources

Every component/module has one `sources.mk` next to its code, in kbuild shape:

```make
obj-$(CONFIG_FOO) += path/relative/to/the/component.c
```

The platform Makefile includes it through `tools/build/sources.mk`:

```make
$(eval $(call component_sources,$(PLATFORM)/board,base,BOARD_BASE))
```

The helper sets `SRC_TAG` (the world this include is for), resolves relative
paths against the component directory, folds the generated `1`/`0` into `y`,
and publishes `<NAME>_SRCS` plus the named lists for the flag-world families
(`_KPI_SRCS`, `_CASE_SRCS`, `_PORT_SRCS`, `_APP_SRCS`, `_SDIO_SRCS`, `_DRV_SRCS`,
`_LWIP_SRCS`).

Line order inside `sources.mk` is link order.

The components in this line:

| Component | `sources.mk` | Groups |
|---|---|---|
| platform/arch | `platform/arch/sources.mk` | base (boot, GICv3, MMU, cache), pcie (ITS/MSI/IRQ controller) |
| platform/board | `platform/board/sources.mk` | base, pcie, nvme |
| platform/os | `platform/os/sources.mk` | base (printf bind, libc stubs, log, tick) |
| liblinux80211 | `components/liblinux80211/sources.mk` | kpi (kernel-API bodies), bus-pci, bus-sdio, port-pci (RK3568 adapter) |

The USB, lwIP, eMMC and PCIe glue are per-world fragments under
`tests/board/rtos/build/*.mk` because they compile in their own flag worlds
(see §5), not because they escape the component rule.

## 4. Cases: one name, three facts

`CONFIG_CASES` is a space-separated list. One name is simultaneously:

1. the translation unit `app/cases/<name>.c`,
2. the `RTOS_CASE_<NAME>` macro the generator derives from the list,
3. the expected entry in the image's `BOARD_TESTS` section and in
   `tools/rtos/judge.py`.

`./make.rtos case-list-check` compares the three and fails when they drift. The
runner walks the list in the declared order, not the linker's.

## 5. Flag worlds

Imported code is not compiled with the application's flags. Each world has its
own C flags set, all derived from `BASE_CFLAGS`/`KCONTRACT_CFLAGS`:

| World | Set | Why it differs |
|---|---|---|
| platform + app | `CFLAGS` | the port's own warnings and includes |
| vendored C (lwIP core, fsl_sdmmc, FreeRTOS, CMSIS) | `VENDORED_CFLAGS` | imported C, `-Wextra` off |
| printf | `VENDORED_CFLAGS + PRINTF_CFLAGS` | the vendored printf has its own knobs |
| CherryUSB | `USB_CFLAGS` | its own include roots and `-std=gnu11` |
| liblinux80211 | `KPI_CFLAGS` | the Linux face needs `gnu11` and its own warning set; the component's sources.mk supplies the groups, `tests/board/rtos/build/kpi.mk` the flags |

A world's flags are part of the flag stamp: changing them rebuilds every object
in that world, so a stale object cannot be certified under a new world.

## 6. Gates

| Gate | What it fails on |
|---|---|
| `check` | a defconfig that does not parse, a generated file that is tracked, a profile that regenerates differently |
| `check-no-d` | a `-D` on a compile or link command |
| `check-config-header` | a Kconfig symbol the generated header does not carry |
| `check-build-contract` | a second `KCONTRACT_CFLAGS`, a flags set that does not derive from it, a world that pins `-O` |
| `case-list-check` | `CONFIG_CASES` vs `BOARD_TESTS` vs `judge.py` drift |
| linker `gates` (in `image`) | `_start` not at `0xa000000`, vector table missing or unaligned, a backend symbol missing, any undefined symbol, the USB storage class table missing |

## 7. Libraries

`./make.rtos libs` archives the object groups the image already built:
`libfw_arch`, `libfw_board_rk3568-itx`, `libfw_osal_cmsis2`, `libfw_support`,
`libfw_usbhost`, `libfw_drivers`, `libfw_app`, `liblinux80211` (when the
component is selected), plus `manifest.json` (members, undefined symbols,
size). The archives are a delivery shape of the image, not a second build.

## 8. Adding a world

The order is the previous line's; keep it, because each step has a gate that
fails early when it is skipped.

1. **Kconfig symbol** in the owning Kconfig file (default off; a profile
   selects it).
2. **defconfig** for the profile that carries it.
3. **`sources.mk` group** in the owning component/module, gated on the symbol.
4. **fragment** only if the world needs its own flag set or link-order slot;
   derive the flags from `BASE_CFLAGS`.
5. **case** if it has anchors: the case file, the `cases.h` declaration, the
   runner row, and the `judge.py` entry.
6. **gates**: add the symbol to `MATRIX_PROFILES` (or the CI list) so
   `check-no-d` exercises it; add a link-time symbol gate if the world has a
   table that must be present.
7. **docs**: this file, `PROVENANCE.md` if anything is imported,
   `DIVERGENCES.md` if an imported file is edited, `LICENSES.md` if the
   license set changes.

## 9. What this repository trimmed

The previous tree's build carried the wireless worlds, the linux-ko line, the
host tier, the SDK packaging and the CMake equivalence entry. None of them is
imported here; their hooks in the Makefile are gone rather than dormant, so a
missing world fails at parse time instead of at link time. The edits are rows
1–13 in `DIVERGENCES.md`.


## 5. S0 and the host bench (2026-11-10)

Two targets sit outside the image, on purpose:

```
./make.rtos s0-compile [S0_RUN=<n>]   # compile the imported stack TUs, no link
./make.rtos host-test                 # x86 bench: green run, then --break red
```

- `s0-compile` writes `bench record` (the
  the project's evidence ledger, beside the repo) and copies the summary to
  `bench record`.  Flags are the kpi world's plus the stack include roots
  and four `-Werror` diagnostics; `tools/build/s0-compile.sh` refuses a flag set
  that reaches into `linux-upstream/include`.  A failed TU is data, not a build
  failure: the target exits 0 and the summary is the product.
- `host-test` builds `tests/host/kpi/` against `rk3568-kpi-mock`'s config header
  and runs the binary twice (normal must be green, `--break` must be red).  It is
  a model of the target's context, not a board test; the limits are listed in
  `tests/host/kpi/README.md`.
- The two new flag worlds (`S0_CFLAGS`, and the host Makefile's own set) do not
  enter the image link; `S0_CFLAGS` derives from `KPI_CFLAGS` so
  `check-build-contract.sh` still sees the kernel's three flags and the profile's
  `-O` token.

## 10. The wireless world (2026-11-10, S1b/C2/C3)

Two Kconfig symbols, one flag-world include, two profiles:

- `CONFIG_LIBLINUX80211_CFG80211` (and `_MAC80211` for the SoftMAC half)
  puts the imported `stack/` TUs in an image;
  `CONFIG_LIBLINUX80211_IWLWIFI` adds the 72 `wireless-drivers/iwlwifi/` TUs and
  the generated firmware unit. `tests/board/rtos/build/wl80211.mk` owns the
  flag worlds (stack: `S0_CFLAGS`; driver: `S1_CFLAGS`; cases: stack flags) and
  the list order that becomes the `.lkpi_init` execution order.
- The component's `sources.mk` stack group is still keyed by
  `CONFIG_LIBLINUX80211` so the S0/S1b measurement targets keep their source
  lists; the image include is what the new symbol gates.
- Profiles: `rk3568-mac80211-c2` (8 MiB FreeRTOS heap),
  `rk3568-iwl7260-mac80211` (16 MiB) and `rk3568-iwl7260-c4` (16 MiB). The
  C4 helper `linuxkpi_nl80211_probe.c` builds the real trigger attributes and
  runs nl80211's `pre_doit`/`doit`/`post_doit`; it is compiled in the stack
  flag world as `WL80211_PROBE_SRCS`, not in the 62/72 TU lists. The driver
  world also sets `LKPI_ALLOC_SKB_MIN=2048` so RX frames stay linear (see
  `docs/DIVERGENCES.md` row 45). The heap is the FreeRTOS one
  (`osMemoryAlloc -> pvPortMalloc`); there is no second heap knob.
- Build-only targets (no image): `s1-driver-compile` (72 TUs, `bench record`)
  and `s1-link` (partial link + classified undefined list,
  `bench record`). `s0-compile` is unchanged.
- The firmware blob is generated, not committed: `tools/rtos/fw-blobs.py`
  writes `components/liblinux80211/fw/iwl7260_fw_blob.c` from the pinned GT
  `iwlwifi-7260-17.ucode` (sha256 checked) and the path is git-ignored.

## STM32F429 wireless profiles (2026-11-15)

The F429 is the MCU platform of this line; its only extra prerequisite is that
the SDRAM is brought up before the kernel (`main.c` calls
`f429_memory_boot()` and parks on failure). Images are flashed as **BIN**
(`mculab` rejects the >1 MB debug ELF):

```
./make.rtos image PROFILE=stm32f429-sdram      # SDRAM bring-up: SDRAM + DMA A/B arm
./make.rtos image PROFILE=stm32f429-wifi-c3    # attach: attach
./make.rtos image PROFILE=stm32f429-wifi-c4    # scan: scan
./make.rtos image PROFILE=stm32f429-wifi-c5    # association: connect
./make.rtos image PROFILE=stm32f429-wifi-c6    # data plane: connect + lwIP + ICMP (delivery, 726,080 B)
./make.rtos image PROFILE=stm32f429-eth       # wired Ethernet: LAN8720A + ST HAL + DHCP + public ICMP (77,164 B)
./make.rtos image PROFILE=stm32f429-wifi-c6t   # data plane + four report-only throughput phases (735,936 B)
mcu flash build/build/rtos-freertos.bin --bin-base 0x08000000 --erase sector \
    --verify --reset --expect "rtos: result"
```

`F429_WIFI_STAGE=3|4|5|6` selects the case stage; `CONFIG_F429_SDRAM` +
`CONFIG_F429_HEAP_SDRAM` put the FreeRTOS heap (4 MiB) in the FMC SDRAM and
`CONFIG_F429_LWIP_SDRAM` puts lwIP's 255 KiB heap in its own `.sdram.lwip`
reserve.  The c6 image links the shared CherryUSB USB adapter through
`ports/usb-cherryusb` and the wireless stack with `NET_LWIP`.

Two diagnostic profiles sit on top of the delivery image and **never ship**:
`stm32f429-wifi-c6h2c` (`CONFIG_RTL8XXXU_TX_CENSUS=y`: the H2C mailbox
read-back and the TX-state register census) and `stm32f429-wifi-c6diag`
(`CONFIG_F429_TICK_PC=y`: the polled-UART tick-hook PC sampler).
`rk3568-rtl8xxxu-8188eu-c5-census` is the same census on the RK3568 control
arm (same dongle, same driver).  All three are default-n in Kconfig, so a
delivery build cannot pick them up by accident.

The size round adds compiler probes beside the images:
`stm32f429-cfg80211-only` is a real `tick` image that proves net/wireless
links without net/mac80211, and `stm32f429-rtl8189fs-compile`,
`-compile-trim` and `-compile-debugoff` are compile-only defconfigs for
`s1-driver-compile` (their `# compile-only` marker is what `ci.sh` skips; they are
not flashable images).


## F429 image size ledger (2026-11-16)

The 1 MiB F429 flash is the binding constraint for the SDIO lines, so the
size round starts with an account, not a flag.  The resource tables (flash,
static SRAM, no-cache/DMA RAM, SDRAM, task stacks) are in
`docs/FOOTPRINTS.md`; this section keeps the size-round decisions.  `tools/rtos/size-ledger.py`
reads the link map and attributes every placed input section to its object
once (an address-interval union; merged `.rodata.str*` blocks are counted
once, and `--gc-sections` discards are a separate row).  It reconciles to
within 16 B of `arm-none-eabi-size`; the remainder is final alignment.

### The delivery image (`stm32f429-wifi-c6`)

| build | text | data | bin | free of 1 MiB |
|---|---:|---:|---:|---:|
| before the round | 722,952 | 3,692 | 726,656 | 321,920 |
| delivery image (this round) | 722,376 | 3,692 | 726,080 | 322,496 |
| delivery + `CONFIG_OPT_LTO=y` | 686,372 | 3,260 | 689,648 | 358,928 |

The delivery image was booted twice from a relay cold start: `PASS SCAN`,
`PASS ASSOC`, 20/20 ICMP at 56 B and 20/20 at 1400 B, `PASS DATA`, and
`rtos: result pass cases=1/1` both times.  `FW_BUILD_TAG` is embedded, so a
sha pins a commit as well as the code; the size and the code are the stable
facts across commits.  The LTO image was booted twice the
same way and passed the same gates; the link gate needed
`-Wl,-u,xPortStartScheduler` (LTO inlines it into its only caller).

Where the 726,080 B goes (placed, `size-ledger.py`):

| group | text | rodata | data | flash |
|---|---:|---:|---:|---:|
| mac80211 | 246,934 | 7,515 | 50 | 254,499 |
| cfg80211 | 137,692 | 10,087 | 918 | 148,697 |
| rtl8xxxu | 69,066 | 42,092 | 2,580 | 113,738 |
| merged strings (`rodata.str*`) | 0 | 92,660 | 0 | 92,660 |
| linuxkpi + bus + skb | 26,112 | 15,238 | 9 | 41,359 |
| lwIP | 35,072 | 0 | 8 | 35,080 |
| CherryUSB | 12,918 | 0 | 0 | 12,918 |
| FreeRTOS | 9,900 | 0 | 0 | 9,900 |
| platform | 5,468 | 62 | 17 | 5,547 |
| app/cases | 4,490 | 0 | 0 | 4,498 |
| other + support + libc | 6,524 | 0 | 80 | 6,604 |
| **total** | **554,176** | **167,654** | **3,662** | **725,500** |

The flash column also carries 8 B of `other` input sections (`.ARM`/fill);
the three named columns sum to 725,492.

`.rodata` is half strings (92,672 B), which is why turning vendor log levels
off is a size lever but also an evidence decision: the C3/C4 attach lines are
part of the judgement.  The delivery face keeps them; only a size-only arm
turns them off.

### What the Kconfig now owns

- `LIBLINUX80211_CFG80211` / `LIBLINUX80211_MAC80211` replace the old
  one-symbol wireless gate.  The split is byte-identical for every image that
  selects both (the c6 `.bin` was compared before/after); a FullMAC/ vendor
  stack can select cfg80211 only.  `stack_config.h` derives the two stack
  macros from these symbols; S0/S1 use `s0_face.h` because the inventory
  measures the whole imported tree.
- `RTL8189FS_{MP,AP,P2P,CONCURRENT,POWER_SAVING,DEBUG,NETIF_SG,EFUSE_FILE,
  LOAD_PHY_FILE}` are the vendor face.  Defaults are the GT answers; an F429
  profile turns them off explicitly.  AP is **not** separable in this vendor
  drop: its fields live behind `CONFIG_AP_MODE` while unguarded code reads
  them (10 of 131 TUs fail with AP off), so AP stays on.
- `LWIP_MEM_SIZE_KB`, `LWIP_PBUF_POOL_SIZE`, `LWIP_TCP_WND`,
  `LWIP_TCP_SND_BUF`, `LWIP_TCP_SND_QUEUELEN`, `LWIP_TCP_SNDLOWAT`,
  `LWIP_MEMP_NUM_TCP_SEG`, `LWIP_MEMP_NUM_TCPIP_MSG_INPKT` and
  `LWIP_UDP_RECVMBOX_SIZE` replace the literals in `lwipopts.h`; the F429
  defaults are 255 KiB / 24 / 16 KiB / 48, the RK defaults are the measured
  gigabit tier.  The linker's `.sdram` lwIP reserve is
  `CONFIG_LWIP_MEM_SIZE_KB + 1 KiB`, passed as `--defsym=LWIP_HEAP_BYTES`,
  not a second 256K literal.
- `CONFIG_OPT_LTO` is the compile/link reserve; `-Oz` and `-fipa-icf` were
  measured at exactly zero and are not offered.
- `CONFIG_LKPI_SKB_DATA_BUF_SZ` and `CONFIG_LKPI_SKB_TOMB_MAX` make the last
  two skb geometry literals visible; the pool counts were already Kconfig.
- One face fix the 8189FS compile needed: `u32`/`s32`/`__u32`/`__s32` are now
  spelled `unsigned int`/`int` (the kernel's own types) rather than
  `int32_t`.  newlib's `int32_t` is `long` on 32-bit ARM, so a vendor
  prototype written as `s32 f(...)` collided with the same function defined
  as `int f(...)`; the width and alignment are unchanged, and the vendor
  driver now compiles 131/131 on the F429 face.

### The 8189FS budget (compile exact, link base exact, SDIO glue estimated)

The F429 8189FS line has no SDIO host adapter yet, so its number is a budget:
the vendor driver is compiled with the F429 face (131/131 TUs), and the base
is the cfg80211-only image's flat `.bin`.  The host glue is the one estimate.

| item | bytes |
|---|---:|
| cfg80211-only base image | 168,036 |
| rtl8189fs, GT face | 742,203 |
| rtl8189fs, delivery face (MP/P2P/CONCURRENT/POWER_SAVING/NETIF_SG/EFUSE_FILE/LOAD_PHY_FILE off; AP+DEBUG on) | 609,933 |
| lwIP (data egress) | 35,080 |
| SDIO engine + fsl_sdmmc + F429 glue (estimate) | 25,000 |
| case + runner (estimate) | 4,000 |
| **pre-trim total** | **974,319 (74,257 B free)** |
| **delivery-face total** | **842,049 (206,527 B free)** |

So the GT face does **not** meet the 100 KiB margin; the delivery face does,
with the vendor attach lines still on.  The DEBUG-off ceiling (414,053 B for
the driver) would leave ~393 KiB free but removes the C3/C4 log evidence, so
it is a size-only arm, not a delivery profile.  The exact per-feature deltas
are in the private round record.

Heap/pool note: the c6 heap low-water was `free=4,157,952 min=4,157,952` of a
4 MiB SDRAM heap, i.e. about 36 KiB ever used on this boot.  The size is
therefore ample and **not** changed in this round; shrinking it is a RAM
footprint choice, not an image-size one.  lwIP's `MEM_ALIGNMENT` stays 8: a
move to 4 is a separate heap A/B, not a free size win.


## AP6181 (BCM43362) profiles

```
./make.rtos image PROFILE=stm32f429-ap6181-attach
./make.rtos image PROFILE=stm32f429-ap6181-scan
./make.rtos image PROFILE=stm32f429-ap6181-join
./make.rtos image PROFILE=stm32f429-ap6181-data
./make.rtos image PROFILE=stm32f429-ap6181-tput
make -C tests/board/rtos/build PROFILE=stm32f429-ap6181-data wiced-image-gate
```

Each carries one case and needs the external WICED tree to materialize
`wireless-drivers/ap6181/vendor/` (`tools/build/wiced-tree.py`, see
`PROVENANCE.md`). The world's switches come from the profile's Kconfig through
`ports/f429/wiced_kconfig.h`, not from `-D` flags: the compile rule filters
`-D` out, so a define that only exists on a command line never reaches the
vendor sources. `wiced-image-gate` fails the build if the image carries a
`linuxkpi`/`mac80211`/`cfg80211`/`brcmfmac` symbol.
