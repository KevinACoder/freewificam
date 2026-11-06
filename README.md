# freewificam

An open-source WiFi stack for RTOS kernels, and the Linux-side module line that
shares its code.

The project takes the mainline Linux SoftMAC drivers unchanged and supplies the
802.11 stack, the bus engines and the OS surface underneath them, so an ordinary
USB or SDIO WiFi module can be used on an RTOS the same way it is used on
Linux.  The same code is also built as a loadable module for Linux, which is
what makes the port verifiable: the two builds run the same driver bytes against
the same 802.11 stack.

This repository is the release repository.  It carries the final code, the
tests, the tools and the documentation that belong to it.  Measurement records,
reverse-engineering material and the firmware reference notes are kept outside
it.

## Layout

```
components/                     first-party components
  linuxkpi/                     Linux kernel API surface: firmware registry,
                                the PCI/USB engines and the RTOS runtime
  net80211/                     imported 802.11 core: source + ports/ + patches/
wireless-drivers/               pinned wireless drivers (source + ports/ + patches/)
  iwlwifi/ rtl8xxxu/ rtw88/ rtw89/
  rtl8189fs/                    external vendor pin: provenance + ports/ + patches/
firmware/                       the vendor firmware images, compiled in by the build
third-party/                    ported components (source + ports/ + patches/)
  CMSIS_6/ CMSIS-FreeRTOS/ FreeRTOS-Kernel/ threadx/
  cherryusb/ fsl_sdmmc/ lwip/ printf/ tlsf/ ulog/
tests/board/
  linux-ko/                     the Linux kernel-module line: build/entry + ports/
  rtos/                         the RTOS line: app/, build/, platform/, configs/, tests/
tools/                          build entries, board round, judge, gates, board-side scripts
docs/                           commit convention, patch policy, firmware notices,
                                release procedure, release notes, migration record,
                                the reference board (docs/boards/)
known_issues/                   the open issues of each release, tracked per version
```

Every imported tree keeps the same shape: the source, the first-party ports and
a `patches/` directory that records the patch set and the digest of the source
the build compiles (`docs/PATCH-POLICY.md`).  Patches are applied in the tree;
`patches/` is the record, not a build-time replay.

## Lines

| Line | Bus | What runs |
|---|---|---|
| `rtw89` / RTL8851BU | USB2/USB3 | mainline rtw89 over the linuxkpi USB engine, both as a Linux module and in the RTOS images |
| `rtl8xxxu` / RTL8188EUS, RTL8723BU | USB2 | mainline rtl8xxxu, same two builds |
| `rtw88` / RTL8821CU | USB2 | mainline rtw88 over the linuxkpi USB engine, Linux module and the RTOS netio images |
| `rtw88` / RTL8821CS | SDIO | mainline rtw88 on the firefly v2 MCI host (SDIO), in the RTOS images |
| `aic8800` / AIC8800D80 | USB2 | the vendor FullMAC stack (boot, firmware download, WPA2 supplicant, lwIP) over the CherryUSB host stack, firefly v2 |
| `iwlwifi` / Intel AC 7260 | PCIe | mainline iwlwifi/mvm over the linuxkpi PCI engine |
| vendor `rtl8189fs` / RTL8189FTV | SDIO | the vendor driver with its own MLME, over the SDIO host stack |
| USB camera (UVC) | USB2 | the CherryUSB host stack, the video class and an isochronous stream engine |

The `aic8800` line is the one line whose 802.11 is not ours: the chip's own
firmware runs the MLME, so the driver is a message transport and the line is
kept separate from the SoftMAC arms rather than folded into them.  It is the
first vendor FullMAC arm here and it exercises the same host stack (CherryUSB,
the xHCI port) as the SoftMAC arms do.

What a working boot looks like on the console - one verbatim UART transcript per
module, with the anchors that name each step - is in
[`docs/CONSOLE-EVIDENCE.md`](docs/CONSOLE-EVIDENCE.md).  It starts with the
RTL8821CS and the AIC8800D80.

## Board and firmware sources

Two boards are wired out.  The reference board is the RK3568-E4AP5G1-ITX; its
hardware, peripheral map and boot path are in
[`docs/boards/rk3568-itx.md`](docs/boards/rk3568-itx.md).  It carries two EHCI
roots and two xHCI (DWC3) instances.

The second board is the firefly v2, an aarch64 carrier with two xHCI instances
and no EHCI - the board the SDIO and vendor-FullMAC lines above run on.  Its
image links at `0x90000000` and is loaded the same way (`tftpboot 0x90000000`,
then `go 0x90000000`); the DDR base and the controller register apertures are
in `tests/board/rtos/platform/board/firefly/v2/`.

Both firmware trees live on GitHub and are not vendored here:

- Boot firmware: <https://github.com/KevinACoder/u-boot-rockchip>, branch
  `next-dev`, board defconfig `rk3568-e4ap5g1-itx_defconfig`, board DTS
  `arch/arm/dts/rk3568-e4ap5g1-itx.dts`.
- Reference Linux kernel (RK3568): <https://github.com/KevinACoder/linux>,
  branch `v7.3-rc2-firefly`; the measured reference image is commit
  `e71d9a28760f` (`7.3.0-rc2-ge71d9a28760f`).

Wi-Fi firmware is in the repository, under
[`firmware/`](firmware/README.md): an image has no filesystem, so each driver's
firmware is compiled in as a C array generated from those files by
`tools/rtos/fw-blobs.py`, which the build runs before it names a source.  The
digests, the upstream each file came from and the redistribution terms are in
[`docs/FIRMWARE-NOTICES.md`](docs/FIRMWARE-NOTICES.md); the arrays themselves
are build products and stay out of git.

## Building the Linux kernel module

`DRIVER` selects the one driver the module carries; `rtw89` is the default,
`rtw88`, `rtl8xxxu` and `iwlwifi` are the other byte-faithful arms.

```sh
BUILD=/path/to/linux-ko KSRC=/path/to/kernel/src KOBJ=/path/to/kernel/obj \
  KREL=7.3.0-rc2 DRIVER=rtw89 FW_EMBED=0 tests/board/linux-ko/build.sh
```

## Building the RTOS images

The image is a function of one defconfig under `tests/board/rtos/configs/`,
not of the command line.  `tests/board/rtos/config.mk` only names the default;
select another profile with `PROFILE=<name>`.  `./make.rtos images` builds both
kernels of it (ThreadX and FreeRTOS behind one CMSIS-RTOS2 abstraction).

The profile is a Kconfig defconfig; the generator writes the make-facing
`freewificam_config` and the only C config header `freewificam_config.h`.  The
build does not hand `-D` configuration macros: structural facts live in
`tests/board/rtos/platform/build_defs.h`, configuration values in the generated
header, and a missing value fails the build instead of falling back silently.

```sh
./make.rtos list                      # every defconfig
./make.rtos config PROFILE=<name>     # write freewificam_config{,.h,.config}
./make.rtos menuconfig PROFILE=<name> # edit .config, then save the defconfig
./make.rtos image PROFILE=<name>      # one image, the profile's backend
./make.rtos check                     # all defconfigs parse identically
./make.rtos check-no-d                # no compile/link command carries -D
./make.rtos matrix                    # seven representative profiles
./make.rtos images                    # both kernels
PROFILE=rtos-shell ./make.rtos image  # the CherrySH console image
PROFILE=rtos-shell ./make.rtos case-list-check  # image vs defconfig vs judge
AARCH64_CROSS_PATH=/path/to/aarch64-none-elf ./make.rtos images
# per-image resource/toolchain values live in the defconfig: CONFIG_OPT_O3,
# CONFIG_DEBUG_G2, CONFIG_HEAP_SIZE, CONFIG_SHELL_STACK_KB, ...
# see docs/BUILD-DESIGN.md section 7.2

make rk3568_ci                        # every rk3568-itx profile, clean build each
make firefly_v1_ci firefly_v2_ci      # every firefly-v1/v2 profile
```

`./make.rtos config` regenerates `build/kconfig/<profile>/generated/config.mk`
from the Kconfig tree; the old `tools/rtos/build.sh` forwards to this entry.

The profiles carried today, by group:

| Group | Profiles | Carries |
|---|---|---|
| camera | `uvc` (active) | one USB2 root, the video class, the isochronous engine and the four camera cases |
| USB host | `usb-xhci`, `usb-xhci-diag`, `usb-xhci-diag-full`, `usb-ehci` | the host stack and the mass-storage class on one controller |
| USB host (firefly v2) | `firefly-v2-usb-xhci` | the same storage case on the board's `usb3_0` xHCI root |
| RTL8851BU | `rtw89-8851bu`, `rtw89-8851bu-bringup`, `rtw89-8851bu-tx`, `rtw89-8851bu-ehci`, `rtw89-8851bu-ehci-bringup`, `rtw89-8851bu-ehci-scan`, `rtw89-8851bu-ehci-join`, `rtw89-8851bu-tx-ehci` | rtw89 over the linuxkpi USB engine, on an xHCI or EHCI root |
| RTL8188EUS | `rtl8xxxu-8188eu-{bringup,bringup-hb,bringup-all,scan,join,data,net,netio-ar-g,netio-ar-ht20,netio-ar-ht20-ba}` | rtl8xxxu over CherryUSB, the bring-up-to-network ladder and the netio arms |
| RTL8723BU | `rtl8xxxu-8723bu-{bringup,bringup-all,scan,join,data,net,netio-ar-ht20,netio-ar-ht20-ba}` | the same driver on an EHCI root, including the composite-interface case and the netio arms |
| AC 7260 | `iwlwifi-7260`, `iwlwifi-7260-sjd` | iwlwifi/mvm over the linuxkpi PCI engine, attach to network |
| RTL8189FTV | `wlan-delivery`, `wlan-obs`, `wlan-ping` | the vendor SDIO stack; needs the external tree (below) |
| PCIe | `pcie-nvme`, `pcie-bench`, `pcie-7260` | the PCIe root complex, NVMe and the 7260 endpoint |
| Shell | `rtos-shell` | the CherrySH console: `case list|run`, the `wifi`/`net`/`usb`/... groups and `board`/`log`/`reboot`; one boot drives every case by name |
| firefly v2 | `firefly-v1-pcie`, `firefly-v2-{rtw88-sdio,rtw88-join,rtw88-net,sdio-mci,xmac,usb-xhci,aic8800-usb}` | the second board: PCIe/ECAM, the rtw88 SDIO line (8821CS), the MCI host alone, the on-board MAC, the storage case and the AIC8800 vendor FullMAC line |

The RTL8189FTV profiles additionally need the external vendor tree:

```sh
VENDOR_8189FS=/path/to/rtl8189fs ./make.rtos images
```

## Performance

One board, one bench, one client.  The tables are split by module - platform -
bus, so a new module adds a table instead of a wider row.  Case names are fixed
(registry below); a table reports the cases it measured, and a new case is
added to the registry first.

### Cases

| case | traffic | condition | parameters |
|---|---|---|---|
| `E-UDP-RX` | UDP 1440 B, peer to board (board receives) | wired 1 GbE | netio `-u -b 1440`, 6 s cell |
| `E-UDP-TX` | UDP 1440 B, board to peer | wired 1 GbE | same cell, server phase |
| `E-TCP-TX` | TCP 1440 B, board to peer | wired 1 GbE | netio `-t -b 1440` |
| `E-TCP-RX` | TCP 1440 B, peer to board | wired 1 GbE | netio `-t -b 1440` |
| `E-PPS64` | 64 B UDP zero-loss pps (`T0`) | wired 1 GbE | ppsgen `-T 2 -k 0 -B 256` + kernel sink |
| `E-MSC` | USB mass storage read/write | USB2/USB3 root | 32 MiB window, fio reference |
| `W-UDP-D/11g` | UDP 1440 B downlink, 802.11g 54M | 2.4 GHz ch5 | netio `-u -b 1440`, 6 s cell |
| `W-UDP-U/11g` | UDP 1440 B uplink, 802.11g 54M | 2.4 GHz ch5 | same cell, server phase |
| `W-TCP-D/11g` | TCP downlink, 802.11g 54M | 2.4 GHz ch5 | netio `-t`, 75 s window |
| `W-UDP-D/HT20` | UDP 1440 B downlink, HT20 72.2 MCS7 SGI | 2.4 GHz ch5 | netio `-u -b 1440`, 6 s cell |
| `W-UDP-U/HT20` | UDP 1440 B uplink, HT20 72.2 MCS7 SGI | 2.4 GHz ch5 | same cell, server phase |
| `W-UDP64-U/HT20` | UDP 64 B uplink, HT20 72.2 MCS7 SGI | 2.4 GHz ch5 | netio `-u -b 64`; the server's transmit window is 30 s, the reference's is its own 6 s interval |
| `W-TCP-D/HT20` | TCP downlink, HT20 72.2 MCS7 SGI | 2.4 GHz ch5 | netio `-t`, 75 s window |
| `W-UDP-D/HT40` | UDP 1440 B downlink, HT40 150 MCS7 SGI | 2.4 GHz ch5 | frozen: protocol upgrade |

Zero loss in every row unless stated; a Wi-Fi cell that drops more than 1% is
void.  Wi-Fi numbers come from the same bench AP (AR9271, ch5) and the same
pinned netio 1.34 client; the Linux column is the same board running the
in-tree driver on mac80211, the `ko` column is the linux-ko shim (net80211
replacing mac80211, in-tree driver, kernel USB) and `rtos` is the bare-metal
stack (CherryUSB + linuxkpi USB).  Methods: [docs/NET-TESTING.md]
(docs/NET-TESTING.md) for the wired port, [docs/WIFI-BENCH.md]
(docs/WIFI-BENCH.md) for 2.4 GHz Wi-Fi.

### RK3568 / GMAC (wired 1 GbE)

| case | freewificam (rtos) | Linux reference |
|---|---|---|
| `E-UDP-RX` | 114.00 MiB/s (956 Mbit/s) | 113.13 MiB/s |
| `E-UDP-TX` | 108.09 MiB/s (907 Mbit/s) | 113.61 MiB/s |
| `E-PPS64`, ~1.69 GHz | 350k with RX coalescing (260k without) | 195k (230k+ with RPS) |
| `E-PPS64`, 816 MHz | 197.5k | 100k (reference only) |
| `E-TCP-TX` | 49.8 / 49.9 MB/s (2/2) | 94.21 MB/s |
| `E-TCP-RX` | 43.4 / 45.0 MB/s (2/2) | 111.91 MB/s |
| `E-MSC` | blocked: no disk on the bench | - |

### RK3568 / RTL8188EUS / USB2 xHCI

| case | rtos, pre-BA | rtos, RX-BA | ko | Linux reference |
|---|---|---|---|---|
| `W-UDP-D/HT20` | 20.0-20.5 Mbit/s | **50.50 / 50.20 / 50.67** (boot 1), **50.75 / 50.87 / 51.12** (boot 2) | 52.07 / 52.34 / 53.01 | 49.83 / 51.67 / 51.92 |
| `W-UDP-U/HT20` | 31.3-32.5 Mbit/s | 31.3 / 30.9 / 31.0 (boot 1), 31.5 / 31.6 / 31.4 (boot 2) | 34.36 / 34.76 / 34.81 | - |
| `W-TCP-D/HT20` | 15.96 Mbit/s | - | 18.81 / 19.39 | 38.12 / 37.87 |
| `W-UDP-D/11g` | 19.03 / 19.29 / 19.16 Mbit/s | - | 18.62 / 18.72 / 18.98 | 18.05 / 18.06 / 18.19 |
| `W-TCP-D/11g` | 15.24 / 15.46 / 15.44 Mbit/s | - | 19.64 / 19.83 | 17.48 / 17.48 |
| `W-UDP-D/HT40` | frozen | frozen | frozen | frozen |

### RK3568 / RTL8723BU / USB2 EHCI

| case | rtos, no BA | rtos, RX-BA | ko | Linux reference |
|---|---|---|---|---|
| `W-UDP-D/HT20` | 10.51 / 10.05 Mbit/s | **30.6 / 24.1** (boot 1), **30.5 / 21.7** (boot 2) | 32.67 / 31.25 | 30.60 / 30.90 |
| `W-UDP-U/HT20` | 26.8 / 26.9 Mbit/s | 26.0 / 25.8 (boot 1), 26.9 (boot 2) | 24.81 / 25.13 | 36.39 / 37.53 |

### RK3568 / RTL8821CU / USB2 xHCI

Two rounds on this chip.  The 1440 B rows are the three-arm run of 2026-11-03
(same AP, same client, same cells as the tables above); the 64 B uplink row is
the sustained-window run of 2026-11-06, which held one transmit window open for
its whole length instead of the six seconds the earlier cells lasted.

| case | rtos, no BA | rtos, RX-BA | ko | Linux reference |
|---|---|---|---|---|
| `W-UDP-D/HT20` | 11.67 / 23.10 Mbit/s | **54.50 / 54.79** (2 boots) | 54.48 (1 cell) | 54.92-55.39 (2 boots) |
| `W-UDP-D/11g` | 21.10 / 21.57 Mbit/s | - | 21.44 | 21.44 |
| `W-UDP-U/11g` | 8.26 / 8.94 Mbit/s | - | - | 18.27-19.58 |
| `W-UDP-U/HT20` | invalid | invalid | invalid | 10.04-10.84 |
| `W-UDP64-U/HT20` | **4.32-4.66 Mbit/s, 8.4-9.1k frames/s** (8 boots, four images) | - | - | 9.92 Mbit/s, 19.4k frames/s |

The HT20 downlink row is the block-ack story again: 11.7-23.1 without
`CONFIG_WLAN_RX_BA`, 54.5 with it, while the same image's legacy downlink stays
at 21.1-21.6.

The uplink cells of that 2026-11-03 run were not measurable, and the reason was
in this repository, not in the air.  On the ko and bare-metal arms the netdev
transmit path let the board's netio server queue 440k frames while the air
carried ~6k, because the driver's refusal was counted as a bridge output error
and dropped after `sendto` had already returned success; the reference kernel's
qdisc is what paces the server there.  The seam now refuses the frame instead,
the bridge holds it and re-offers it on the next transmit completion, so the
refusal reaches `sendto` and the server paces itself - `refusals=` on the
cell's done line is that backpressure, not loss.

The 64 B row is the first uplink that runs to the end of its window: 312k-326k
frames handed to the seam per boot, 0.02% of them missing at the client, zero
`wlan_txerr`, zero interface-queue drops, the access point's own receive count
agreeing to 0.02%, and eight clean boots across four images of the same fix
(the last one on the tree rebuilt from the commit).  One caveat on the window
itself: it is armed for 30 s, and both clocks - the board's counter and the
client's own byte count over its own rate - put the burst at 36.6 s, so the
timer fires about 6.6 s late under this load.  The rates are computed over the
interval that actually elapsed, so they are not inflated by the label; the late
fire is an open question, not a settled 30 s.

What is left is rate: 4.3-4.7 Mbit/s against the reference's 9.9 on the same
cell, and the wall is the device, not the feed.  The transfer census of the
preceding round puts a bulk-out at about 250 us of service plus 80 us of gap
for three frames, against about 140 us for the same three frames on the
reference, while the transmit queues hold a dozen frames - the engine never
runs dry.  Three things travel with that rate and are unexplained here: the
transmit block-ack ledger ends the window at 7 started, 2 operating and 9
stopped, which is not a one-to-one session count and has not been read yet (the
image is built with `CONFIG_WLAN_RX_BA=n`, so the receive reorder path, which
still locks up under a burst, is out of it); the access point reports the
station's uplink at MCS4 39 Mbit/s where the reference arm of the same cell sat
at MCS5 52, a rate-control difference nobody has attributed; and the 1440 B
uplink cells have not been re-run since the transmit path was fixed.

One residue is known and bounded: after a window closes, the driver's report
queue still holds about 64 frames (one `skbq` line at the engine slot address);
the next cell starts with empty transmit queues and is unaffected.  One or two
register accesses per boot also return `-110` (reads of `0x770`, a write of
`0xaa`), which has not stopped a window.  The earlier, much larger failure of
this line - a machine that stopped printing a second into the window - was a
queue-membership defect in the shared socket-buffer code, fixed in this tree.

### firefly v2 / USB mass storage (USB2 xHCI, `usb3_0`)

The same storage case as the RK3568 rows, moved to the second board.  It runs
inside the disk's last 32 MiB, does a 64 KiB write-then-read round trip first,
then a size sweep and a fixed-command arm at each size; the Linux column is the
same board, port and stick under the in-tree driver.

| case | freewificam (rtos) | Linux reference |
|---|---|---|
| `E-MSC` read | 21.50 MiB/s (1 MiB cmds, 46.5 us/cmd), 19.54 (64 KiB) | 20.6 MiB/s |
| `E-MSC` write | 18.69 / 18.83 MiB/s (1 MiB cmds, 2 boots), 15.0 (64 KiB) | 17.1 MiB/s |

Two boots, the disk's last 32 MiB window, a 64 KiB round trip before the
measured arms.  Both boots also print the size sweep, which is where the
per-command cost is read off: read goes 10.41 MiB/s at 8 KiB to 20.72 at
128 KiB, write 8.06 to 17.85, so the fixed cost per command is what the small
arms pay (0.75 ms against 6.0 ms for a 16x larger command).  Each boot reports
`csw bStatus 1` / "Device not ready" once, on the capacity command: that is the
stick's own answer right after the bus resets, and the class retries it - the
run is clean from the round trip onwards.

### firefly v2 / RTL8821CS / SDIO

The card runs mainline rtw88 on the board's MCI host.  What is measured today
is reachability - it associates to the WPA2 bench AP 2/2 boots and passes a
first packet 2/2 (`ping` 10/10, DHCP lease in `10.227.127.0/24`).  **Throughput
is not measured yet**; the rows below appear when the 1440 B UDP cells have
run.

| case | freewificam (rtos) | Linux reference |
|---|---|---|
| `W-JOIN` (WPA2 assoc + DHCP + ping) | 2/2 boots | 2/2 |
| `W-UDP-D` / `W-UDP-U` | not measured | not measured |

### firefly v2 / AIC8800D80 / USB2 xHCI

A vendor FullMAC chip: the firmware runs the MLME and the driver is a message
transport over the same CherryUSB host stack.  The bring-up ladder is the
module's own, on one boot: the `1111:1111` ZeroCD mode switch, the four-stage
firmware download, the `a69c:8d81` runtime composite, the scan table (24 APs
seen), the WPA2 four-way handshake, DHCP and the stack's own gateway ping.

The handshake is reported per message: M1 received, M2 sent, M3 received with a
valid MIC, M4 sent - so the PTK is derived on both sides, not merely attempted.

| case | freewificam (rtos) | Linux reference |
|---|---|---|
| `W-JOIN` (WPA2 4-way + DHCP + ping) | 2/2 boots (`connected` 3000 ms, ping 4/4, 6-65 ms) | 2/2 |
| `W-UDP-D` / `W-UDP-U` | not measured | not measured |

The Linux column is the same dongle under the in-tree `aic8800` driver on the
same board and port; that arm is what separated the host stack from the device
when this line was being brought up.

### Notes

The wired TCP rows are the 2026-11-02 default (TCP and UDP cells 2/2 each,
`tcp_retrans=0`, zero loss).  The peer-to-board direction used to crawl at
40 kB/s: input ran inside the driver's receive thread, so the socket reader
could not drain and lwIP re-offered refused segments only from its 250 ms fast
timer.  Input now runs on the tcpip thread; a second, independent defect in the
zero-copy receive slot table (a lost half of a counter pair handed a live slot
out again) surfaced at the new rate and is fixed with masked-IRQ critical
sections.  The UDP cells did not move (113.99/107.4 MB/s, 0% loss both
directions), and three lifetime counters stay at zero; see
[docs/NET-TESTING.md](docs/NET-TESTING.md) section 8.

The Wi-Fi modes tell different stories.  At the lowest protocol the shim and
mac80211 are equal: per frame data 238 us against 239 us and control 28 us
against 28 us, so the net80211 shim carries no per-frame tax, and TCP lands
within 13% of the reference (19.6 against 17.5 Mbit/s).  At HT20 the shim
matches the reference after adding block-ack and A-MPDU support: 52.1-53.0
against 49.8-51.9 Mbit/s, with BlockAcks, radiotap A-MPDU marks, control
2.7-2.8 us per frame and gap 0.  The old gap was a missing feature, not a slow
stack: before this work the reference negotiated block-ack (3,523 BlockAcks,
37,620 A-MPDU-marked frames) while the shim had no session at all (0 BlockAcks,
0 marks, one plain ACK per frame).  The bare-metal arm was in that same
pre-block-ack state until 2026-11-02: the imported net80211 dropped the AP's
ADDBA Requests (five retries, then DELBA), so every downlink MPDU paid a plain
ACK and 327 us of inter-frame gap out of 618 us per frame - it looked like a
host per-frame cost but was the unaggregated medium.  Answering ADDBA and
reordering the receive side (no driver change; the chip itself emits the
BlockAcks) moved that arm from 19.9 to 50.2-51.1 Mbit/s at 229-234 us/frame,
gap 0-3.6 us and control 2.1 us per frame: 0.97-0.98x the ko arm on the same
bench, 2/2 boots, zero loss.  Its uplink is 0.89-0.91x ko (the transmit path
was not touched, unattributed) and its legacy band is 18.4-18.8 Mbit/s today
against 19.0-19.4 in the previous round, in an image the BA code is not linked
into.

The 8723BU is the same driver on the other USB controller (EHCI, behind the
hub).  Both kernel arms run it with block-ack: 1,366-1,448 BlockAcks per cell,
A-MPDU marks, control 2.2-2.3 us/frame, gap 0, MCS6/7, 30.6-32.7 Mbit/s.
The bare-metal arm used to see almost none of those aggregates: the receive
side answered ADDBA and the chip acked (1,097 BlockAcks, near-full bitmaps),
yet 16,383 arrivals reached the bridge's receive entry and only 3,713 were
delivered.  The drops were not the USB path (one 1,498-byte MPDU per
completion, no errors, no full buffers, no interface-layer drops) and not the
reorder algorithm: they were the reorder window.  With 32 slots against an AP
block-ack buffer of 64, AP retransmissions parked the reorder head on a missing
sequence number, so 4,268 frames ahead of the window and 3,117 frames from an
older wrap of the 12-bit sequence space were discarded as duplicates (10 of the
latter were genuine duplicates).  Pacing the same client to 1,000 pps delivered
6,016 of 6,016 frames, which separated congestion from an algorithm fault.

The window is now 128 slots, one full AP buffer with slack; nothing else
changed (no driver, no USB engine).  Clean image, two boots, two cells each:
30.6 / 24.1 and 30.5 / 21.7 Mbit/s downlink, zero loss in three of four cells,
uplink 25.8-26.9 Mbit/s.  The fourth cell dropped 11% (21.7 Mbit/s), the
remaining variance this row does not explain; the xHCI 8188EU arm re-measured
at 51.7 Mbit/s with zero loss after the change, so the healthy path is not
taxed.  The next ceiling is the EHCI completion rate itself: about 2,700 bulk
completions per second (the port hands a transfer back from the
interrupt-on-async-advance path, ~370 us each) against 4,430/s for the same
one-transfer-in-flight engine on xHCI.  The aggregation-free arm is not
re-measured, since it does not link the reorder module.

HT40 is frozen with the protocol upgrade.  The historical numbers are not
carried here: the shim's so-called HT40 arm associated at 20 MHz (assoc HT
Capabilities 0x0020), and the reference's downlink at 40 MHz is 84.8-94.0
Mbit/s against that arm's 46.2, so neither side is a like-for-like row yet.
The row will be rewritten when the protocol work reopens it.

## What the console shows

The two paths are distinguishable from the first seconds of a boot, without a
debugger: the bare-metal image brings up its own USB controllers and asks
linuxkpi to claim the interface, while the linux-ko image is an ordinary module
on the kernel's usbcore.  These are verbatim excerpts from the bench logs (one
line may be elided where a calibration warning sat in between; the numbers and
the MAC are the ones that boot printed).

Bare-metal RTOS, RTL8188EUS on the xHCI root:

```
[   3.079] rtl8xxxu: usb buses=3,2
[   3.309] rtl8xxxu: usb bus=3 hcd=xhci base=0xfcc00000 started
[   4.466] rtl8xxxu: claim usbh: lkpi claim attempt 0bda:8179 bus=3 hub=2 port=4 intf=0 spd=3
[   5.735] wlan: bridge attached 20:f4:1b:52:8f:01
[   5.739] wlan: seam tx wake registered
[   5.749] rtl8xxxu: claim usbkpi: 'rtl8xxxu' claimed 0bda:8179
[   9.969] wlan: ba addba req tid=0 ssn=1 buf=64
[   9.973] wlan: ba addba rsp tid=0 buf=64 dialog=1
[  28.901] net: netio ready port=18767 aux=18768 buf=16384
```

linux-ko, the same module on the same controller:

```
[   12.573520] usb 4-1.4: RTL8188EU rev K (TSMC) romver 0, 1T1R, TX queues 2, WiFi=1, BT=0, GPS=0, HI PA=0
[   12.611851] usb 4-1.4: RTL8188EU MAC: 20:f4:1b:52:8f:01
[   12.623567] usb 4-1.4: rtl8xxxu: Loading firmware rtlwifi/rtl8188eufw.bin
[   13.607354] wlan: bridge attached 20:f4:1b:52:8f:01
[   13.620009] wlan: seam tx wake registered
[   13.635653] wlan: wext face registered on wlan0
[   13.657222] rtl8xxxu 4-1.4:1.0 wlan0: kernelmod line data plane up, addr 20:f4:1b:52:8f:01
```

Read the two blocks against each other.  In the bare-metal image the controller
is the stack's own (`hcd=xhci base=0xfcc00000`), the chip is identified only by
vendor:product (`0bda:8179`) in the claim line, and the driver then prints its
own transfer counters (`stage_size`, `slots`, `rx_err`); in the linux-ko image
the kernel's usbcore enumerates the device and prints the chip's revision and
MAC, the driver loads the firmware, and the shim only adds the bridge and the
wext face on top.  The bus/port path differs because each stack numbers the
controllers itself.  Both blocks end at the same two anchors - the bridge
attached to the MAC, and the netio server listening - which is what makes the
same case runnable on either arm.

## Wireless bench

A wireless number is only as good as the access point under it.  Before taking
any, prove the bench with `tools/bench/stability.sh` (fixed-rate stream, four
PASS/FAIL criteria on the AP's own counters) and rule the channel out with
`tools/bench/channel-busy.sh` (airtime-based occupancy per channel).  Method,
role assignment and the failure modes seen in the lab:
[docs/WIFI-BENCH.md](docs/WIFI-BENCH.md).

## Gates and tools

```sh
tools/bench/stability.sh --ap-if wlan2 --sta-ip 192.168.99.2 --secs 240
tools/bench/channel-busy.sh --mon-if wlan1 --channels "1 3 6 9 11 13"
tools/rtos/build.sh host-test       # the conformance case on the host, both kernels
tools/rtos/build.sh digest          # every imported tree's digest record
tools/rtos/board-round.py --help    # one board boot: stage, boot, capture, judge
tools/rtos/judge.py <serial.log>    # re-judge a capture
tools/check-commit-msg.sh <msg>     # the commit format
tools/check-release-docs.sh         # reader-facing docs carry no private references
tools/netio/build.sh                # the pinned NETIO peer (the Linux host / Pi client)
```

The firmware images are committed under `firmware/`; the C arrays the images
link are generated from them by `tools/rtos/fw-blobs.py` and are not.
`docs/FIRMWARE-NOTICES.md` carries the digests and the redistribution terms.

## The reference boards

The hardware results come from two boards.  The RK3568 ITX board
(RK3568-E4AP5G1-ITX) carries the wired, PCIe, SDIO and USB lines: its console,
1 GbE management link, USB roots, PCIe slots, boot window and reset lines are
described in [docs/boards/rk3568-itx.md](docs/boards/rk3568-itx.md), together
with which connector each line uses and which code owns it.

The firefly v2 carries the SDIO, vendor-FullMAC and storage rows added on this
page.  It has two xHCI roots and no EHCI, so the RK3568 board is the only place
the EHCI port runs; the two boards share the host stack and the class code, and
differ in the board directory and the controller facts it selects.

## Version, releases and known issues

`VERSION` carries the release number; `CHANGELOG.md` the per-release list;
`docs/RELEASE.md` the version rules, the branch model and how to cut a release;
`docs/RELEASE-NOTES-0.0.2.md` the state of this release, with the measured
numbers; `known_issues/` the issues each release is known to carry.

## Rules

- Do not merge or rebase the previous development line. Take a file explicitly
  with `git show <sha>:<path>` and record the source commit in the importing
  commit message.
- Every commit builds; documentation and tooling commits prove it with the
  commit-message checker or the release-doc checker.
- ASCII in the repository; working notes written in another language do not land
  here.
- No machine-local paths, no private repository names, no internal round
  identifiers: what lands here has to read correctly to someone who has never
  seen the development machine.
- Commit format: `docs/COMMIT-CONVENTION.md`; gate: `tools/check-commit-msg.sh`.
