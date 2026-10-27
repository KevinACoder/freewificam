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

## Gates and tools

```sh
tools/rtos/build.sh host-test       # the conformance case on the host, both kernels
tools/rtos/build.sh digest          # every imported tree's digest record
tools/rtos/board-round.py --help    # one board boot: stage, boot, capture, judge
tools/rtos/judge.py <serial.log>    # re-judge a capture
tools/check-commit-msg.sh <msg>     # the commit format
tools/check-release-docs.sh         # reader-facing docs carry no private references
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
