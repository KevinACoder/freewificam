# MODULES — component source lists (BUILD-01 M4)

Status: M4 implemented on branch `m4-sources` on top of the BUILD-02 no-`-D`
configuration pair.  This document records the ownership rule, the Kconfig
gates, the query/render interfaces, and the one deliberate divergence between
the RTOS and linux-ko driver sets.

## 1. The rule: one source, one list

Every first-party component owns a `sources.mk` next to its code.  The file is
plain GNU make using the kbuild spelling:

```make
obj-$(CONFIG_NET80211) += net80211/ieee80211.c
```

`tests/board/rtos/build/Makefile` includes the component through
`tools/build/sources.mk`:

```make
$(eval $(call component_sources,$(ROOT)/components/net80211,core,NET80211_CORE))
```

The helper sets `SRC_TAG` to the requested link group, includes the component
file, resolves its paths against the component directory, and publishes
`<NAME>_SRCS`.  Fragments consume those names; they do not repeat the paths.

There is one wrinkle that matters for every future component: the generated
make fragment spells booleans as `1`/`0`, matching the pre-M4 `CONFIG_*`
variables.  `obj-$(CONFIG_X)` therefore produces `obj-1` and `obj-0`, not
`obj-y`; the helper folds `obj-1` into `obj-y` before publishing.  Do not
"fix" this by writing `obj-$(if ...)` in a component: the `1`/`0` spelling is
what keeps `freewificam_config` usable by both make and `#if`.

Line order inside a `sources.mk` is the link order.  The seven representative
profiles were checked against the pre-M4 `ALL_SRCS` sequence and are exactly
equal, including the positions of the net80211 seam, CherryUSB, `rx_ba`, and
the rtw88 SDIO port.

## 2. Component inventory

| component | `sources.mk` | groups (published names) | Kconfig gate |
|---|---|---|---|
| net80211 | `components/net80211/sources.mk` | core (`NET80211_CORE`), port layer (`NET80211_NPORT`: osal, mbuf, ifnet), bridge (`NET80211_BRIDGE`), seam (`NET80211_KPI`), BA receive (`NET80211_RXBA`) | `CONFIG_NET80211`, `CONFIG_LINUXKPI_NET80211`, `CONFIG_WLAN_RX_BA` |
| linuxkpi | `components/linuxkpi/sources.mk` | Linux 7.3 face (`LINUXKPI`: core/compat/80211/cfg80211/netdev/osdep/trace/fw/skbuff + bus face), CherryUSB backend (`LINUXKPI_CHERRY`), 8189FTV vendor face (`LINUXKPI_WLAN`) | `CONFIG_LINUXKPI`, `CONFIG_LINUXKPI_USB/PCI/SDIO`, `CONFIG_LINUXKPI_CENSUS` |
| arch | `tests/board/rtos/platform/arch/sources.mk` | boot/exception core (`ARCH_BASE`), GICv3 ITS/MSI (`ARCH_PCIE`) | board choice; `pcie`/`nvme` case for the ITS group |
| board | `tests/board/rtos/platform/board/sources.mk` | board drivers (`BOARD_BASE`), RK3568 PCIe (`BOARD_PCIE`), RK3568 NVMe (`BOARD_NVME`), firefly NVMe/MSI-X (`BOARD_FNVME`), firefly ECAM (`BOARD_ECAM`) | `CONFIG_BOARD_*`; case string for the bus groups |
| os | `tests/board/rtos/platform/os/sources.mk` | bare-metal runtime (`OS_BASE`: printf bind, libc stubs, log, tick) | unconditional in an image |
| rtw89 | `wireless-drivers/rtw89/sources.mk` | `RTW89_IMPL` (21), `RTW89_BLOB`, `RTW89_APP` (4 cases) | `CONFIG_WLAN_LINE_RTW89` |
| rtl8xxxu | `wireless-drivers/rtl8xxxu/sources.mk` | `RTL8XXXU_IMPL` (9), `RTL8XXXU_BLOB`, `RTL8XXXU_APP` (4 cases) | `CONFIG_WLAN_LINE_RTL8XXXU` |
| rtw88 | `wireless-drivers/rtw88/sources.mk` | `RTW88_IMPL` (19), `RTW88_BLOB`, `RTW88_APP` (2 cases) | `CONFIG_WLAN_LINE_RTW88` |
| iwlwifi | `wireless-drivers/iwlwifi/sources.mk` | `IWLWIFI_IMPL` (72), `IWLWIFI_BLOB`, `IWLWIFI_APP` (1 case) | `CONFIG_WLAN_LINE_IWLWIFI` |
| rtl8189fs | `wireless-drivers/rtl8189fs/sources.mk` | vendor stack (`RTL8189FS_IMPL`, 131), app (`RTL8189FS_APP`), SDIO port (`RTL8189FS_SDIO`), lwIP face (`RTL8189FS_LWIP`), observation (`RTL8189FS_OBS`) | `CONFIG_WLAN_LINE_RTL8189FS`, `CONFIG_WLAN_OBS` |

The `WLAN_LINE` choice in `wireless-drivers/Kconfig` is the single selector:
`none` -> no wireless line, rtl8xxxu, rtw89, rtw88, iwlwifi, rtl8189fs.  Its visible
derived symbols (`NET80211`, `LINUXKPI`, `LINUXKPI_NET80211/USB/PCI/SDIO`,
`LINUXKPI_CENSUS`) feed the components; no source list reads `CONFIG_CASES` to
decide whether a wireless stack exists.

Profiles retain their historical case strings and gained exactly one line,
`CONFIG_WLAN_LINE_*=y`, next to `CONFIG_CASES=`.  That line was generated from
the existing case prefix, not from a second driver list.

## 3. Consumption points

### 3.1 RTOS image

`tests/board/rtos/build/{rtw89,rtl8xxxu,rtw88_sdio,iwlwifi,wlan}.mk` now
compose named groups (`RTW89_NET80211_SRCS`, `RTW89_LKPI_SRCS`, ...) from the
component outputs.  The duplicate net80211 list, the repeated nport/bridge
lists, the 131-line rtl8189fs list and its second lwIP list are gone.  The
`$(filter ...)` on `CONFIG_WLAN_RX_BA` moved into the net80211 component and is
now `obj-$(CONFIG_WLAN_RX_BA)`; the seam's Kconfig gate is likewise in the
component.

### 3.2 Query interface

`tools/build/query.mk` prints a module's list, one repository-relative path per
line:

```sh
make -s -f tools/build/query.mk PROFILE=net-netio-raw-tx256 MODULE=image SRCS
make -s -f tools/build/query.mk MODULE=net80211-ko SRCS
```

Modules: `image`, `image-wlan`, `net80211`, `net80211-core`, `net80211-nport`,
`net80211-bridge`, `net80211-kpi`, `net80211-rxba`, `net80211-ko`,
`linuxkpi`, `linuxkpi-cherry`, `linuxkpi-wlan`, `rtw89`, `rtl8xxxu`, `rtw88`,
`iwlwifi`, `rtl8189fs`.  `PROFILE` selects the Kconfig world; without it the
query runs in native mode and exposes only `net80211-ko`, which is the compile
set the kernel module needs.  That mode is deliberately Python-free so
`tests/board/linux-ko/build.sh` stays a pure native-make consumer.

### 3.3 CMake fragment

`tools/build/gen_sources.py` renders the query result:

```sh
tools/build/gen_sources.py --profile net-netio-raw-tx256
tools/build/gen_sources.py --profile net-netio-raw-tx256 --check
```

The default output is `build/kconfig/<profile>/sources.cmake`:

```cmake
set(CFG_SRCS
    "tests/board/rtos/platform/arch/aarch64/start.S"
    "components/net80211/net80211/ieee80211.c"
    ...
)
```

The script fails on an empty list, on a duplicate path, and (with `--check`)
on a stale file.  `image` is the complete link list: `ALL_SRCS` plus the
8189FS vendor group when that line is selected, so a WLAN profile renders its
driver too.  M3 can therefore `include()` this file and add the executable
without re-deriving conditions; CMake support itself is not part of M4.
For `net-netio-raw-tx256` the 82 rendered paths are identical to the make
`ALL_SRCS` sequence; for `wlan-ping` the 224 paths are identical to
`ALL_SRCS + WLAN_ALL_SRCS`.

### 3.4 linux-ko

`tests/board/linux-ko/build.sh` now obtains `STACK_SRCS` from
`MODULE=net80211-ko`.  `query.mk`'s native mode composes that list from the
same `components/net80211/sources.mk` groups, omitting
`osal_memory_target.c` (the kernel OSAL owns memory) and keeping the module's
historical `mbuf, ifnet, osal_cmsis2` order.  The 21 paths are exactly the
previous inline list.

The build now runs the closure gate itself:

```sh
NM="${CROSS}nm" sh tests/board/linux-ko/check-closure.sh "$MOD/wlan_ko.ko"
```

`check-closure.sh` uses `${NM:-nm}`, so the host build can still invoke it
directly.  A full module build was not run in this worktree because no
prepared `KSRC`/`KOBJ` tree is present; the source list, the shell syntax and
the closure script were exercised independently.

## 4. What deliberately did not move

These lists stay in their existing fragments because their flag world is part
of the list, not just the paths:

* `build/usb.mk`: CherryUSB stack, board glue and MSC class lists (`USB_CFLAGS`).
* `build/uvc.mk`: UVC class/app/case list (`UVC_CFLAGS`).
* `build/sdio_mci.mk`: the SDIO-MCI image's port and vendor lists.
* `build/rtw88_sdio.mk`: `RTW88_SDIO_PORT_SRCS` (FSL SDMMC port world).
* `Makefile`'s MMC/eMMC groups (`MMC_PORT_SRCS`, `MMC_VENDOR_SRCS`).
* Backend vendor lists (FreeRTOS kernel/port, ThreadX common/port), the shell
  and CherrySH lists, and the lwIP stack list (`LWIP_STACK_SRCS`); each is a
  third-party set with its own flags and is not a first-party component.

The firefly ThreadX `libc_bindings.c` also stays in `BOARD_TX_EXTRA_SRCS`: it
is backend+board link glue and its position in the backend group is
load-bearing.

## 5. The rtw89 +7 / rtw88 +3 divergence

`tests/board/linux-ko/build.sh` compiles more driver units than the RTOS image:

* rtw89: `mac_be.c phy_be.c efuse_be.c wow.c debug.c led.c led_mc.c`;
* rtw88: `debug.c wow.c led.c`.

These are now checked by build, not by assumption:

* **rtw89, all seven added**: the build fails in `wow.c` (`struct
  cfg80211_wowlan`/`cfg80211_pkt_pattern` are not modelled by the RTOS
  linuxkpi) and in `led.c`/`led_mc.c` (their `rtw89_led_init`/`deinit` collide
  with the static-inline stubs `led.h` selects when `CONFIG_RTW89_LEDS` is
  off).
* **rtw89, only the three `*_be.c` units added**: rc=0, identical image size
  (2,371,448 B), identical defined-symbol set.  The 8851B build never reaches
  the Wi-Fi 7 BE paths and `--gc-sections` drops them.
* **rtw88, all three added**: the build fails in `wow.c` (same missing cfg80211
  wowlan types) and `led.c` (`rtw_led_init`/`deinit` redefined against the
  no-LED stubs).
* **rtw88, only `debug.c` added**: rc=0 and an identical symbol set; the unit
  is dead for this image (the +8 B bin delta is data/layout placement, not a
  symbol or section-size change).

Conclusion: the divergence is **not a missing-unit bug**.  `wow.c`/`debug.c`/
`led.c`/`led_mc.c` are kernel feature units gated by `CONFIG_PM`,
`CONFIG_RTW89_DEBUG`, `CONFIG_RTW89_LEDS` and `CONFIG_RTW88_DEBUGFS`, none of
which the RTOS images implement; the linuxkpi headers deliberately provide
the no-feature stubs.  The `*_be.c` units are unconditional in the Linux
module but unreachable on 8851B, and adding them changes nothing after
garbage collection.  Revisit this conclusion if the RTOS gains suspend/LED/
debugfs support or a BE chip line.

## 6. Verification and known deltas

* `./make.rtos check`: passed (all defconfigs parse and are stable; no
  generated files tracked).
* Seven representative profiles built rc=0 with
  `AARCH64_CROSS_PATH=/home/zhugy/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1`:
  netio raw, rtl8xxxu 8723BU BA, rtos-shell, rtw89 8851BU EHCI, firefly-v2
  rtw88 SDIO, pcie-7260, uvc.
* For all seven, the full `ALL_SRCS` sequence is identical to the pre-M4
  baseline and the `nm -g --defined-only` set is identical.
* With equal-length build tags (the pre-commit comparison, where both sides
  carried a `-dirty` suffix), all seven flat binaries had exactly the baseline
  size, including firefly-v2 rtw88 at 570,208 B.  A clean HEAD has a five
  character shorter `git describe` tag, so six images are 8 B smaller (16 B
  where the banner string is linked twice); `.rodata` differs only in
  `FW_BUILD_TAG`, and rtw89's firmware array masks the shift.  Symbols and
  source sequences are identical in both cases.  Image hashes were never the
  judge.
* No board run was performed for M4.  Runtime behavior is unchanged by
  construction (same objects, same flags, same link order); the evidence here
  is build-level, as the handoff's M4 acceptance requires.
* Pre-existing quirk kept on purpose: the 8723BU profile still links the
  8188EU firmware blob.  `rtl8xxxu.mk` selects on the make variable
  `CONFIG_RTL8XXXU_CHIP`, but the generator emits only the choice booleans
  (`CONFIG_RTL8XXXU_CHIP_8723BU=1`), so the variable is empty and the default
  is 8188eu.  The component list reproduces that selection exactly; fixing the
  chip choice would change behavior and is a separate change.
