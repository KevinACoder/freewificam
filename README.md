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
| `rtw88` / RTL8821CU | USB2 | mainline rtw88, Linux module line |
| `iwlwifi` / Intel AC 7260 | PCIe | mainline iwlwifi/mvm over the linuxkpi PCI engine |
| vendor `rtl8189fs` / RTL8189FTV | SDIO | the vendor driver with its own MLME, over the SDIO host stack |
| USB camera (UVC) | USB2 | the CherryUSB host stack, the video class and an isochronous stream engine |

## Board and firmware sources

The reference board is the RK3568-E4AP5G1-ITX; its hardware, peripheral map and
boot path are in [`docs/boards/rk3568-itx.md`](docs/boards/rk3568-itx.md).

Both firmware trees live on GitHub and are not vendored here:

- Boot firmware: <https://github.com/KevinACoder/u-boot-rockchip>, branch
  `next-dev`, board defconfig `rk3568-e4ap5g1-itx_defconfig`, board DTS
  `arch/arm/dts/rk3568-e4ap5g1-itx.dts`.
- Reference Linux kernel: <https://github.com/KevinACoder/linux>, branch
  `v7.3-rc2-firefly`; the measured reference image is commit `e71d9a28760f`
  (`7.3.0-rc2-ge71d9a28760f`).

## Building the Linux kernel module

`DRIVER` selects the one driver the module carries; `rtw89` is the default,
`rtw88`, `rtl8xxxu` and `iwlwifi` are the other byte-faithful arms.

```sh
BUILD=/path/to/linux-ko KSRC=/path/to/kernel/src KOBJ=/path/to/kernel/obj \
  KREL=7.3.0-rc2 DRIVER=rtw89 FW_EMBED=0 tests/board/linux-ko/build.sh
```

## Building the RTOS images

The image is a function of one profile file, not of the command line.
`tests/board/rtos/config.mk` names the active profile from
`tests/board/rtos/configs/`; `tools/rtos/build.sh images` builds both kernels of
it (ThreadX and FreeRTOS behind one CMSIS-RTOS2 abstraction).

```sh
AARCH64_CROSS_PATH=/path/to/aarch64-none-elf \
  tools/rtos/build.sh images
```

The profiles carried today, by group:

| Group | Profiles | Carries |
|---|---|---|
| camera | `uvc` (active) | one USB2 root, the video class, the isochronous engine and the four camera cases |
| USB host | `usb-xhci`, `usb-xhci-diag`, `usb-xhci-diag-full`, `usb-ehci` | the host stack and the mass-storage class on one controller |
| RTL8851BU | `rtw89-8851bu`, `rtw89-8851bu-bringup`, `rtw89-8851bu-tx`, `rtw89-8851bu-ehci`, `rtw89-8851bu-ehci-bringup`, `rtw89-8851bu-ehci-scan`, `rtw89-8851bu-ehci-join`, `rtw89-8851bu-tx-ehci` | rtw89 over the linuxkpi USB engine, on an xHCI or EHCI root |
| RTL8188EUS | `rtl8xxxu-8188eu-{bringup,bringup-hb,bringup-all,scan,join,data,net}` | rtl8xxxu over CherryUSB, the bring-up-to-network ladder |
| RTL8723BU | `rtl8xxxu-8723bu-{bringup,bringup-all,scan,join,data,net}` | the same driver on an EHCI root, including the composite-interface case |
| AC 7260 | `iwlwifi-7260`, `iwlwifi-7260-sjd` | iwlwifi/mvm over the linuxkpi PCI engine, attach to network |
| RTL8189FTV | `wlan-delivery`, `wlan-obs`, `wlan-ping` | the vendor SDIO stack; needs the external tree (below) |
| PCIe | `pcie-nvme`, `pcie-bench`, `pcie-7260` | the PCIe root complex, NVMe and the 7260 endpoint |

The RTL8189FTV profiles additionally need the external vendor tree:

```sh
VENDOR_8189FS=/path/to/rtl8189fs tools/rtos/build.sh
```

## Performance

Zero loss in every row. Three arms on one board: `ko` is the linux-ko shim
(net80211 replacing mac80211, in-tree driver, kernel USB), `rtos` is the bare
metal stack, `Linux` is the same board running the in-tree driver on mac80211.
A dash means that arm was not measured on this bench.  Methods and counters:
[docs/NET-TESTING.md](docs/NET-TESTING.md) for the wired port,
[docs/WIFI-BENCH.md](docs/WIFI-BENCH.md) for 2.4 GHz Wi-Fi.

| Link | Test | freewificam (ko) | freewificam (rtos) | Linux reference |
|---|---|---|---|---|
| Wired 1 GbE | UDP 1440 B receive | - | 114.00 MiB/s (956 Mbit/s) | 113.13 MiB/s |
| Wired 1 GbE | UDP 1440 B send | - | 108.09 MiB/s (907 Mbit/s) | 113.61 MiB/s |
| Wired 1 GbE | 64 B zero-loss pps (T0), ~1.69 GHz | - | 350k with RX coalescing (260k without) | 195k (230k+ with RPS) |
| Wired 1 GbE | 64 B T0, 816 MHz | - | 197.5k | 100k (reference only) |
| Wired 1 GbE | TCP 1440 B, board to peer | - | 33.51 MB/s | 94.21 MB/s |
| Wired 1 GbE | TCP 1440 B, peer to board | - | 0.03 MB/s (stalled) | 111.91 MB/s |
| Wi-Fi 8188EU, 802.11g legacy (54 Mbit/s) | UDP 1440 B downlink | 18.62 / 18.72 / 18.98 Mbit/s | - | 18.05 / 18.06 / 18.19 Mbit/s |
| Wi-Fi 8188EU, 802.11g legacy (54 Mbit/s) | TCP downlink | 19.64 / 19.83 Mbit/s | - | 17.48 / 17.48 Mbit/s |
| Wi-Fi 8188EU, HT20 (72.2 Mbit/s MCS7 SGI) | UDP 1440 B downlink | 21.50 / 20.03 Mbit/s | - | 49.83 / 51.67 / 51.92 Mbit/s |
| Wi-Fi 8188EU, HT20 (72.2 Mbit/s MCS7 SGI) | TCP downlink | 18.81 / 19.39 Mbit/s | - | 38.12 / 37.87 Mbit/s |

TCP rows are first measurements, one session each: TCP is not part of the
accepted set in [docs/NET-TESTING.md](docs/NET-TESTING.md) yet, and the wired
RTOS arm's peer-to-board direction stalled at 33 KB/s while the board-to-peer
direction ran at 33.5 MB/s - that asymmetry is the next thing to look at, not a
throughput number.

The two Wi-Fi modes tell different stories.  At the lowest protocol the shim
and mac80211 are equal: per frame data 238 us against 239 us and control 28 us
against 28 us, so the net80211 shim carries no per-frame tax, and TCP lands
within 13% of the reference (19.6 against 17.5 Mbit/s).  At HT the reference
negotiates block-ack and sends A-MPDUs (3,523 BlockAcks, 37,620 frames with
radiotap A-MPDU marks), while the shim has no block-ack session at all (0
BlockAcks, one plain ACK per frame), and that is the whole gap.

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

Firmware blobs are not committed; `docs/FIRMWARE-NOTICES.md` carries their
digests, the source files and the redistribution terms.

## The reference board

Every hardware result is from one RK3568 ITX board (RK3568-E4AP5G1-ITX). The
console, the 1 GbE management link, the SDIO socket, the USB roots, the PCIe
slots, the boot window and the reset lines are described in
[docs/boards/rk3568-itx.md](docs/boards/rk3568-itx.md), together with which
connector each line uses and which code owns it.

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
