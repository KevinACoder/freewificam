# BUILD-DESIGN — one Kconfig, one generator, one entry (M0+M1)

Status: BUILD-01 M0+M1 executed on 2026-11-02.  This document is the design
record of that migration: what the build is, which symbols exist, how a
defconfig becomes flags, and what was deliberately left for M2–M7.  The run
evidence is in the private project repository,
`runs/build/2026-11-02-M0/README.md`.

## 1. What changed, in one paragraph

The 81 hand-written `tests/board/rtos/configs/*.mk` profiles and the 912-line
`build/Makefile` encoded the same configuration in several places: profile
variables, `CONFIG_LEVERS` strings, per-driver `*_CFLAGS` worlds and a
compile-time case table.  The M1 migration puts the values into a Kconfig tree,
generates one `generated/config.mk` per profile, and changes the Makefile's
only input from `include $(LINE)/config.mk` to the generated fragment.  The
source-list logic, the flag worlds, the linker closure and the case table are
untouched: this is a configuration-source migration, not a redesign.

The old `.mk` files were retired after the equivalence gate; the converter is
kept as the record of how they were read.  `tests/board/rtos/config.mk` is now
only the default-profile selector.

## 2. Invariants and the gate

The build's contract is the compile command and the source list, not the image
hash: `-g3` and `__LINE__` make a hash a weak judge (the handoff says this, and
the plan repeats it).  A1 therefore compares, for every profile:

* the effective `-D` map (name → value, last definition wins),
* the sequence of non-`-D` tokens (`-I`, warning, link flags),
* every `*_CFLAGS` / `*_LDFLAGS` / `*_SRCS` variable,
* `BASE_CFLAGS` and `LDFLAGS`.

All 81 profiles compare equal between the baseline dumps and the post-switch
dumps.  The one textual difference is expected and allowed:
`net-netio-raw-tx256-prof` used to carry `-DCONFIG_GMAC_PROF=1` twice (once
from the profile's make variable, once in `CONFIG_LEVERS`); the new fragment
carries it once, and the effective map is unchanged.

Acceptance A2 is behavioural: two valid boots of each substituted profile,
`rtos: result pass`, judged from the serial log.  A cold-boot flaky boot is
invalid and is retried; there is no boot_id on the bare-metal RTOS, so the
evidence is image sha256 + crc32 + the serial transcript.

## 3. The symbol set: 51, not 53

The handoff counted 53 names; four are double-accounted and settle to 51:

| Name | Why it looked like two |
|---|---|
| `GMAC_PROF` | profile make variable and `-DCONFIG_GMAC_PROF=` in `CONFIG_LEVERS` |
| `WLAN_RX_BA` | make-side source gate and the C-side `-D` |
| `NET_WATCH` | make-side source gate, `-DCONFIG_NET_WATCH=`, and a bare `-DNET_WATCH=` |
| `RTL8XXXU_HB` | make-side driver switch and the fragment's `-DRTL8XXXU_HB` |

The authoritative legacy set is the 51 names below (the machine-readable copy
is `kconfig-gen.py`'s `MODELED`; the split here is for reading).  §7.2 adds 23
resource/toolchain symbols on top; those are not legacy profile keys and are
always emitted, so they are counted separately.

**Image structure (8)**: `BACKEND`, `BOARD`, `CASES`, `HEAP_SIZE`,
`FREERTOS_HEAP_SIZE`, `LEVERS`, `OPT`, `PROFILE`.

**GMAC / wired instruments (12)**: `GMAC_PROF`, `GMAC_RX_BATCH`,
`GMAC_RX_BH`, `GMAC_RX_HWCSUM`, `GMAC_RX_ZC`, `GMAC_STALL_PROBE`,
`GMAC_SWEEP_WIDE`, `GMAC_TCP_PROBE`, `GMAC_TX_CSUM`, `GMAC_TX_RING`,
`GMAC_TX_SWEEP_GUARD`, `GMAC_TX_TAIL_IDLE`.

**USB / class (4)**: `USB_MSC_DIAG`, `USB_MSC_DIAG_FULL`, `LKPI_USB_OBS`,
`UVC_USB_BUS`.

**NETIO instruments (5)**: `NETIO_CPU_KHZ`, `NETIO_RAW_UDP`, `NETIO_RXPATH`,
`NETIO_TCP_PROBE`, `NETIO_TX_STALL_PROBE`.

**Wireless (22)**: `RTL8XXXU_CHIP`, `RTL8XXXU_AP_FREQ`, `RTL8XXXU_AP_SSID`,
`RTL8XXXU_EXPECT_MAC`, `RTL8XXXU_HB`, `RTL8XXXU_USB_BUS`,
`RTW89_USB_BUS`, `NET_WATCH`, `WLAN_RX_BA`, `WLAN_OBS`, `WLAN_PING_COUNT`,
`WLAN_PING_INTERVAL_MS`, `WLAN_PING_TRACE`, `WLAN_PUMP_DRAIN`,
`WLAN_PUMP_PRIO`, `WLAN_SCAN_BATCH`, `WLAN_SCAN_DIAG`, `WLAN_TCP_SINK`,
`WLAN_TCP_SINK_SECONDS`, `WLAN_UDP_PORT`, `WLAN_UDP_SINK`,
`WLAN_UDP_SINK_SECONDS`.

The `tests/board/rtos/` Kconfig tree carries the image structure, the case
and netio knobs; `components/net80211/Kconfig` carries `WLAN_RX_BA`;
`components/linuxkpi/Kconfig` carries `LKPI_USB_OBS`; `wireless-drivers/Kconfig`
carries the wireless group.  The generator refuses an undefined name and
prints the count, so the 51 is checked on every run.

Types: six choices (`BACKEND`, `BOARD`, `OPT`, `DEBUG_LEVEL`, `PROFILE`,
`RTL8XXXU_CHIP`); strings `CASES`, `LEVERS`, `RTL8XXXU_AP_SSID`,
`RTL8XXXU_EXPECT_MAC`, `WLAN_PUMP_PRIO`; integers `HEAP_SIZE` (MB),
`FREERTOS_HEAP_SIZE` (bytes), `RTL8XXXU_AP_FREQ`, `RTL8XXXU_USB_BUS`,
`RTW89_USB_BUS`, `UVC_USB_BUS`, `WLAN_PING_COUNT`, `WLAN_PING_INTERVAL_MS`,
`WLAN_TCP_SINK_SECONDS`, `WLAN_UDP_PORT`, `WLAN_UDP_SINK_SECONDS`,
`GMAC_TX_RING`, `NETIO_CPU_KHZ`; the rest are booleans.

## 4. Explicit-set: the rule that makes a defconfig honest

A defconfig writes only keys the old profile really set, including an explicit
`0`.  The generator emits a make variable or a `-D` only when
`Symbol.user_value is not None`.  Two consequences:

* A profile that never set `WLAN_SCAN_BATCH` keeps the old effective value
  (empty make variable, because the Makefile's unconditional
  `WLAN_SCAN_BATCH := $(CONFIG_WLAN_SCAN_BATCH)` killed the fragment's `?= 1`).
  The Kconfig default of 1 is only the `.config` default; it is not emitted.
* `HEAP_SIZE` and `FREERTOS_HEAP_SIZE` keep the two worlds: MB rendered as
  `<n>M` for the ThreadX `--defsym=HEAP_SIZE=`, bytes rendered as `<n>u` for
  FreeRTOS.  A ThreadX profile that inherited a `FREERTOS_HEAP_SIZE` keeps it
  in the defconfig, unused, because the old profile set it; removing it would
  be a second change hidden in a migration.

The eight structural keys are always emitted (`BACKEND`, `BOARD`, `CASES`,
`OPT`, `PROFILE`, `HEAP_SIZE`, `LEVERS`, `GMAC_PROF`) so the Makefile always
sees a complete image identity.

Defconfigs are written with `Symbol.config_string`, not `write_min_config`, so
an explicit `# CONFIG_X is not set` survives.  No `depends on` or `range` is
used in M1: either would clamp a legacy value and turn the equivalence gate
into a negotiation.

## 5. Three render channels

| Channel | Shape | Members |
|---|---|---|
| A | make variable only; the existing Makefile/fragment makes the `-D` | the structural keys, `RTL8XXXU_*`, `RTW89_USB_BUS`, `UVC_USB_BUS`, `USB_MSC_DIAG*`, `WLAN_*` except `WLAN_RX_BA`, `NET_WATCH`'s make side |
| B | make variable **and** `CFG_DEFS` | `GMAC_PROF`, `WLAN_RX_BA`, `NET_WATCH` |
| C | `CFG_DEFS` only | the 17 lever-only names (`GMAC_*` levers, `LKPI_USB_OBS`, `NETIO_*`, `NET_WATCH`? no — `NET_WATCH` is B) |

`CFG_DEFS` is inserted after `PROFILE_DEFS` and before the residual `LEVERS`:

```
$(CASE_DEFS) $(EXTRA_DEFS) $(PROFILE_DEFS) $(CFG_DEFS) $(LEVERS) ...
```

The residual `CONFIG_LEVERS` keeps what Kconfig does not model:
`-DNET_WATCH=1`, `-DCONFIG_NETIO_CPU_KHZ_LATE=...` and any lab-patch define.
lwIP options are no longer allowed here: they live in Kconfig and are rendered
into `include/generated/lwipopts_kconfig.h`.  The generator refuses a modeled `-DCONFIG_<name>=`
in `LEVERS` (it would be a hidden second value) and refuses `$`, `#` or a
newline in generated make values.  `EXTRA_DEFS` and `BUILD_TAG` stay make
knobs, not Kconfig symbols.

## 6. The Kconfig tree and the generated outputs

```
freewificam/Kconfig
freewificam/tests/board/rtos/Kconfig
freewificam/tests/board/rtos/platform/board/Kconfig
freewificam/tests/board/rtos/app/Kconfig
freewificam/components/linuxkpi/Kconfig
freewificam/components/net80211/Kconfig
freewificam/wireless-drivers/Kconfig
freewificam/third-party/Kconfig        # comment only; no vendor Kconfig in M1
```

`tools/kconfig/` vendors Kconfiglib 14.1.0 from the in-repo Zephyr pin
`0534e997eac` (`kconfiglib.py`, `menuconfig.py`, `LICENSE.txt`, `VENDOR`,
`MANIFEST.sha256`).  `genconfig.py` is not vendored; `tools/build/kconfig-gen.py`
replaces it.  `tools/kconfig` is a build tool, not a compiled third-party unit,
but it is in the digest (`make.rtos digest` runs its `sha256sum -c`).

Per profile the generator writes, under `build/kconfig/<profile>/`:

| File | Consumer |
|---|---|
| `.config` | humans and `--check`; full symbol set |
| `include/generated/autoconf.h` | generated, **not included** in the M1 compile |
| `generated/config.mk` | the only make fragment the Makefile consumes |
| `include/generated/csh_config.h` | the CherrySH core |
| `include/generated/lwipopts_kconfig.h` | lwIP, included by `ports/include/lwipopts.h` |
| `manifest.json` | profile, values, `CFG_DEFS`, input hashes; no timestamps |

Nothing generated is committed; `make.rtos check` fails if `git ls-files --
build/kconfig` is non-empty.  The `.config` and `autoconf.h` are generated so
M2 can start using them without a second format decision; the M1 compile stays
on `-D` flags, which is what makes A1 possible.

## 7. The entry

```
./make.rtos list [--current]
./make.rtos config [PROFILE=<name>]
./make.rtos image|images|host|host-test|digest|check|case-list-check|dump-vars|clean|distclean
./make.rtos rk3568_ci|firefly_v1_ci|firefly_v2_ci
PROFILE=<name> ./make.rtos images
```

The three `*_ci` targets (also `make rk3568_ci` from the repository root) cover
one board at a time: `check` once, then one clean build per matching defconfig,
with failures collected and the target nonzero if any profile failed.  The
board comes from the defconfig; rk3568-itx is the default and is implicit (no
`CONFIG_BOARD_RK3568_ITX` line), while firefly-v1/v2 write
`CONFIG_BOARD_FIREFLY_*`.  Counts today: 78 rk3568-itx, 1 firefly-v1,
3 firefly-v2 (82 defconfigs with `rtos-shell`).  Logs are under `build/ci/<target>/` (git-ignored).

`make.rtos` is POSIX sh; `tools/build/rtos.mk` is the make implementation.
It selects the profile from `PROFILE`, otherwise from the `CONFIG :=` line in
`tests/board/rtos/config.mk`, regenerates the fragment, then runs
`tests/board/rtos/build/Makefile` with `PROFILE=<name>`.

The Makefile changed in only four places:

1. `PROFILE ?= $(CONFIG)` and
   `GEN_CONFIG := $(ROOT)/build/kconfig/$(PROFILE)/generated/config.mk`;
   `-include $(GEN_CONFIG)` (skipped for `make clean`, so clean needs no
   Python) with a remake rule that regenerates and restarts.
2. `CFG_DEFS` in `BASE_CFLAGS` after `PROFILE_DEFS`.
3. The old `-DCONFIG_GMAC_PROF=$(CONFIG_GMAC_PROF)` line removed (channel B
   now owns it); the base/perf make switch renamed `SWITCH_PROFILE` so the
   defconfig name can own `PROFILE`.
4. `build-info` prints `defconfig:` and keeps `profile:` for the base/perf
   switch; `digest` also checks `tools/kconfig/MANIFEST.sha256`.

`tools/rtos/build.sh` is a compatibility shim: it forwards to `make.rtos` and
says so once.  `VENDOR_8189FS`, `JOBS` and `AARCH64_CROSS_PATH` behave as
before.

## 7.1 The CherrySH command layer (M6)

`CONFIG_SHELL` turns the image into a command console instead of the fixed
runner.  The core is vendored under `third-party/cherrysh/` (CherrySH
`8efe539c6e55b71d2f2cd1116cd00c4d64222a67`, 14 files, Apache-2.0, manifest
checked by digest); the board port lives under `components/shell/`:

- `shell_port.c` wires the static `chry_shell_t`, the FSymTab/VSymTab
  boundaries from `baremetal.ld` and a 16 KiB task.
- `shell_console.c` turns `console_sput/sget` into `board_console_write_raw`
  and `uart_early_try_getc`; promptedit is off (`CONFIG_CSH_PROMPTEDIT=n`) so
  the 115200 polled console does not redraw per byte.
- `shell_cmds.c` exports `case list|run <name>|run-all`, the group aliases
  (`wifi`, `net`, `usb`, `sdio`, `pci`, `emmc`, `uvc`), `board
  info|clock|heap`, `log level|mark` and `reboot`; `SHELL_AUTORUN` tokenizes a
  case list for a non-interactive boot.
- The case table is the linker section `BOARD_TESTS`: `board_tests.h` exports
  `BOARD_TEST(name, fn)`, the runner reads
  `__board_tests_start/__board_tests_end`, and the host tier keeps its static
  array.  The linker script already `KEEP`s the section, so `--gc-sections`
  cannot drop a case.

`rtos-shell` is the smoke profile (`CONFIG_SHELL=y`, `CASES="tick"`).  A boot
prints `$ `, and `case list` / `case run tick` reproduce the judge anchors
(`rtos: case tick start`, `rtos: case tick PASS`).  `case-list-check` reads
the linked image's `.board_tests`, the profile's `CONFIG_CASES` and
`tools/rtos/judge.py` and fails when the three disagree; the image may carry
a subset of the judge table, but every case must be judgeable.

The shell is not a replacement for the runner: `CONFIG_SHELL=n` keeps the
plain runner and the old `rtos: result` line.  `SHELL_BATCH` and the
host-side serial session are still M6 follow-ups; the current console is
interactive only, and the polled console's raw echo shows readline redraws
(a terminal erases them with cursor moves).

## 7.2 Resources, toolchain and per-image values

The 51 legacy symbols describe *which* image is built.  Per-image resource and
toolchain values are separate symbols: they have defaults, they are always
emitted by the generator, and a defconfig overrides any of them without a
fragment edit.  They are not legacy profile keys, so they do not change the
51/53 reconciliation.

**Toolchain**: `OPT` is now a six-member choice (`O0`, `O2`, `O3`, `Os`, `Og`);
`DEBUG_LEVEL` is a choice (`g0`..`g3`, default `g3`).  The Makefile renders
them as `$(OPT) -g$(CONFIG_DEBUG_LEVEL)`.

**Stacks and scheduler**: `SHELL_STACK_KB` (16), `RUNNER_STACK_KB` (8),
`RUNNER_BIG_STACK_KB` (16), `CMSIS2_MAX_THREADS` (0 = fragment fallback),
`CMSIS2_MAX_TIMERS` (0), `TX_TIMER_STACK` (0), `TX_TICKS_PER_SECOND` (0).
`HEAP_SIZE` and `FREERTOS_HEAP_SIZE` remain the image heap pair.

**Pools and USB**: `LKPI_SKB_POOL_NUM`, `LKPI_SKB_DATA_POOL_NUM`,
`LKPI_SKB_HEAP_HDR_MAX`, `LKPI_USB_CHERRYUSB_SLOTS` (16),
`USB_STAGE_SIZE` (20480), `USBHOST_MSC_TIMEOUT` (800),
`USB_EHCI_ISO_NUM` (4).  A `0` pool/deadline value means "keep the per-world
fallback"; the fragments carry the fallback (e.g. iwlwifi 256/64/128,
rtl8xxxu/rtw89/rtw88/wlan 128/64/64 and CMSIS2 32/16).

**lwIP, ULOG and printf**: `LWIP_TCPIP_CORE_LOCKING_INPUT` (default y only for
`PROFILE_PERF`), `LWIP_TCP_ACK_DELAY_SEGMENTS` (2; 4 for `PROFILE_PERF`),
`LWIPERF_TCP_MAX_IDLE_SEC` (60) are rendered into
`include/generated/lwipopts_kconfig.h`, which
`third-party/lwip/ports/include/lwipopts.h` includes.  No compile line carries
a `-DLWIP_*`; the only consumer is the header.  `ULOG_ENABLED` (y),
`ULOG_MAX_MESSAGE_LENGTH` (256),
`PRINTF_DISABLE_FLOAT` (y), `PRINTF_DISABLE_EXPONENTIAL` (y).
`NETIO_CPU_KHZ_LATE` is emitted as a raw `-DCONFIG_NETIO_CPU_KHZ_LATE=` token
when set, because `net_mdk_netio.c` reads that macro directly.

**Board behaviour**: `BOARD_AUTO_RESET` (y) renders
`-DRTOS_BOARD_AUTO_RESET=0/1`.

A profile changes them like any other symbol:

```
CONFIG_OPT_O3=y
CONFIG_DEBUG_G2=y
CONFIG_HEAP_SIZE=8
CONFIG_SHELL_STACK_KB=24
CONFIG_RUNNER_STACK_KB=12
CONFIG_LKPI_SKB_POOL_NUM=200
CONFIG_ULOG_MAX_MESSAGE_LENGTH=512
CONFIG_BOARD_AUTO_RESET=n
```

The generated fragment always contains the `CONFIG_*` make variables; only
the defconfig lines are profile-specific.  `make.rtos check` validates all 82
defconfigs; a temporary O3/g2/heap-8/24-KiB-shell-stack profile built to
72,032 bytes, proving the values reached the compiler (it was removed after
the test).  Existing profiles gain the extra `-DCONFIG_*` flags, so image
hashes can change; image hash is not the build gate.

**What stays build-owned.**  These are not per-image tunables: backend
identity (`GUEST`, `EL1`, `CMSIS2_BACKEND_*`), board constants
(`SDMMC_PORT_*`, `USB_BOARD_IRQ_PRIORITY_RAW`, `EQOS_*`), vendor driver ABI
(`CONFIG_RTW89_*`, `CONFIG_RTW88_*`, `CONFIG_IWLWIFI*`, the `LTL8188F`
family), and bridge/firmware names (`WLAN_BRIDGE_*`, `WIFIMAC_PATH`).  They
select a family/board, not a value within one image; changing them changes
which source is built.  `CONFIG_LEVERS` remains the explicit escape hatch for
a one-off experiment, and CI/log review warns when it is used.

## 8. What Kconfig deliberately does not express

These seven classes stay in the Makefile or the fragments; moving them is
M4/M5 work, and pretending Kconfig can express them would have made the
equivalence gate impossible:

1. Per-driver flag worlds (`*_CFLAGS` variables, the order in which
   `rtw89`/`rtl8xxxu`/`rtw88` append `-DCMSIS2_MAX_TIMERS/THREADS`).
2. Conditional source sets (`CASE_SRCS` filter-outs, the world source lists,
   the driver/firmware split).
3. Board linker scripts, the entry address gate, `-Wl,-u` closure flags.
4. Case run order: `CASES` is a set; the runner table's order is code.
5. Filter-out lever surgery (the old `net-pps.mk` idiom) — the residual
   `LEVERS` keeps the result, not the expression.
6. The `FLAG_STAMP` incremental contract.  The migration builds each
   representative profile from clean; M2 must add the generated fragment to
   the stamp before incremental builds are trusted.
7. Identity transforms: `RTL8XXXU_EXPECT_MAC` `aa:bb:...` → `{0xaa,...}` and
   `RTL8XXXU_AP_SSID` quoting.  The generator does them; Kconfig stores the
   canonical string.

Also deliberately not in M1: a unified heap, a USB transport symbol replacing
the magic bus integers (`3` xHCI, `1` both USB2 roots, `4` all four, `0`
first), a `WLAN_LINE` symbol, `AUTORUN`, an autoconf.h include in the compile,
a linux-ko conversion, CMake, `sources.mk` and the static libraries.  They are
M2–M5; the test-layout convergence is M7.  M6 (the cherrysh command layer) is
done in §7.1.

## 9. Profile map and the tier draft

`docs/PROFILE-MAP.md` is generated by `tools/build/profile-map.py` from the 82
defconfigs.  It marks 35 first-class candidates and 47 archive candidates as a
*draft*: every profile is still selectable, nothing is removed from CI, and
the owner confirms the 35 before any matrix changes.  The tier is metadata,
not a build switch.

The two A2 substitutions are recorded there and in the run ledger: the board
has an RTL8723BU (EHCI) and an RTL8851BU (EHCI), no 8188EU and no USB mass
storage, so A2 booted `rtl8xxxu-8723bu-bringup` + `rtw89-8851bu-ehci-bringup`
instead of the plan's 8188 + 8723.  Both baseline (old tree) and new-tree
verdicts are `rtos: result pass cases=2/2`.

## 10. Evidence from the M0+M1 run

* Baseline dumps: `runs/build/2026-11-02-M0/baseline/profiles.json.gz` (81
  profiles, old tree).  Post-switch dumps: `.../after/profiles.json`.
  `tools/build/compare-flags.py` reports `81 profiles equivalent`.
* `make.rtos check`: all 81 defconfigs parse and regenerate identically;
  generated files are not tracked; the converter `--check` is idempotent.
* Representative builds (4 from the plan's list + 2 substitutions):
  `net-netio-raw-tx256` (179544 B, sha256 `c793cbbf…`), `uvc` (116928 B,
  `77248315…`), `rtl8xxxu-8188eu-netio-ar-ht20-ba` (607112 B, `9fbc951a…`),
  `rtl8xxxu-8723bu-netio-ar-ht20-ba` (623960 B, `66085a8b…`),
  `firefly-v2-sdio-mci` (106588 B, `21c83045…`), `rtw89-8851bu-ehci-bringup`
  (2372024 B, `08121f4e…`).  Each is a clean build with the new fragment.
* A2, new tree: `rtl8xxxu-8723bu-bringup` 2/2 valid boots (`rtos: result
  pass cases=2/2 backend=freertos`; sha256 `cdc33e31…`, crc32 `97d725ad`);
  `rtw89-8851bu-ehci-bringup` 2/2 valid boots (sha256 `08121f4e…`, crc32
  `43155329`).  Baseline old-tree boots: 8723-boot1 and rtw89-boot1, both
  pass.
* The first full 81-profile sweep after M1 found six failures (firefly-v1-pcie;
  iwlwifi-7260 and iwlwifi-7260-sjd; wlan-delivery, wlan-obs, wlan-ping) plus
  the two profiles excluded from the M1 gate (pcie-7260 and
  firefly-v2-rtw88-sdio).  The follow-up fixes are: a `case_pcie_run`
  prototype in `cases.h`; the rk3568 PCIe/NVMe include roots for the `pcie`
  case; the `pcie_ecam` + firefly PCIe/NVMe/MSI-X source selection and the
  `firefly-v1-pcie` case list (`pcie_ecam` instead of `pcie`); the rtw88 blob
  symbol `lkpi_fw_rtw8821c_fw_register` in the case and its `-Wl,-u` link
  flag; `-DWLAN_SEAM_NO_USB_OBS=1` for the PCIe iwlwifi bridge; and the
  parse-time `$(shell mkdir -p $(BUILD))` needed before the WLAN stamp's
  `$(file ...)` expands.  A clean rebuild sweep of all 81 defconfigs is now
  81/81 rc=0 (0 failures); the local M1 follow-up ledger records the per-profile
  shas and sizes.  The pcie-7260 image was booted on the RK3568 twice: both
  boots print `pcie: dev 03:00.0 8086:08b1`, `pcie-7260: PASS vectors=1
  pend_irqs=0`, and `rtos: result pass cases=3/3 backend=freertos`.
* Resource/toolchain configurability (the m01699 request): the 23 symbols in
  §7.2 are per-defconfig; a temporary O3/g2/heap-8/24-KiB-shell-stack profile
  built to 72,032 B and was removed; `rtos-shell` rebuilt to 68,144 B.  The
  full three-target CI rebuild after the change is 78 rk3568 + 1 firefly-v1 +
  3 firefly-v2, 0 failures.
* `make.rtos digest` still fails on four `third-party/lwip/ports/` hashes
  (`ports/ethnetif.c`, `ports/ethnetif.h`, `ports/include/lwipopts.h`,
  `ports/platform_hooks.c`); the old tree fails identically, so this is
  manifest drift in the imported tree, not the Kconfiglib addition.  The
  Kconfiglib manifest itself passes (2/2).

## 11. M2–M7, in order

M2 completes the entry (`menuconfig`, `savedefconfig`, `olddefconfig`,
`matrix`, `trimcheck`, `linuxko` with no Python, `build.sh` deprecation done);
M3 adds CMake parity (`cmake/kconfig.cmake`, one build dir per profile,
`compare-parity.py`, A4); M4 replaces the duplicated source lists with
`obj-$(CONFIG_X) += path.o` `sources.mk` and makes linux-ko consume the same
list (A5, `check-closure.sh` in the gate); M5 turns the core into static
libraries with `include/freewificam/` as the only public surface (A9/A10);
M6 (the cherrysh command layer and the three-source case gate, A6/A7) is done;
M7 converges `tests/` and the docs last.  M4 is implemented: every
first-party component owns one `sources.mk`, `tools/build/query.mk` is the one
query point, `tools/build/gen_sources.py` renders the CMake fragment, and
linux-ko consumes the net80211 list and runs `check-closure.sh` in its build.
The component inventory, the order-preservation evidence and the rtw89/rtw88
divergence conclusion are in `docs/MODULES.md`.  The `.mk` retirement stays
done; the converter and `dump-vars.py` are the frozen record of how the 81
profiles were read.

## 12. Open items

* The 35/47 tier is a draft; the owner confirms it before CI changes.
* The M1 follow-up closed the stale profiles: all 81 defconfigs build, and
  `pcie-7260` boots 2/2 on the RK3568.  The firefly images build but were not
  booted here (no firefly station in this round); the wlan images build but
  run long sink loops, so they are compile/boot-anchor only.
* Resource and toolchain values are per-image now (§7.2).  Backend identity,
  board constants and vendor driver ABI macros remain build-owned because
  they select which source is built, not a value within one image; the
  boundary is documented in §7.2.  A1-style `-D` equivalence is not expected
  after the resource flags were added; the contract is the defconfig + the
  generated fragment, not byte-identical images.
* The firmware blobs under `components/linuxkpi/fw/` are generated locally and
  git-ignored; a clean checkout must produce them before building any
  firmware-based profile.  This is deliberate (firmware is not redistributed),
  but it means `make.rtos matrix` is not self-contained until the blob
  generation step is documented.
* The digest's lwIP manifest drift is pre-existing and needs its own
  provenance decision.
* The shell is interactive only so far: `CONFIG_SHELL_BATCH` is generated but
  unused, `CONFIG_SHELL_AUTORUN` is an empty string in `rtos-shell`, and the
  host-side `serial-session.py` is not written.  A6 is the `rtos-shell` boot;
  A7 is `make.rtos case-list-check` on that image.
* Incremental builds across profiles are not trusted until M2 puts the
  generated fragment into `FLAG_STAMP`; the M1 builds are clean builds.

## 13. BUILD-02 — the generated-config pair (2026-11-03)

M0+M1 generated `generated/config.mk` and still let the Makefile and fragments
hand `-D` the configuration.  BUILD-02 removes that second channel:

* `tools/build/kconfig-gen.py` emits one pair for make and C:
  `build/kconfig/<profile>/freewificam_config` (make-facing; booleans `1/0`)
  and `freewificam_config.h` (the only generated C header; true booleans are
  `#define CONFIG_X 1`, false ones are Kconfig comments, integers and strings
  are always emitted).  The full Kconfig `.config` is written alongside them
  for `menuconfig`/`savedefconfig`.
* The platform Makefile includes `freewificam_config`; every C compilation
  force-includes `tests/board/rtos/platform/build_defs.h`, which includes the
  generated header and carries only structural facts (board, backend, ABI
  names, vendor ABI).  `COMPILE_C`/`COMPILE_S` keep a `-D` filter as a guard,
  but the tree no longer produces one: a `grep -D` over the build fragments
  finds only the guard and comments.
* There are no fallback defaults in the build or in C.  `FREERTOS_HEAP_SIZE`,
  `CMSIS2_MAX_THREADS/TIMERS`, `TX_TIMER_STACK`, lwIP knobs and the driver
  geometry all have Kconfig defaults; a missing value is a generator error or
  a compile error, never a silent `$(if ...)` / `#ifndef` default.
* `test/board/rtos/platform/host_*_defs.h` do the same for the host tier;
  `tests/board/linux-ko` keeps its own line until M2/M4 migrate it.

Entry points: `./make.rtos config|menuconfig|olddefconfig|savedefconfig|list|
image|images|matrix|check|check-no-d|check-config-header|trimcheck|libs|sdk|
sdk-check`.
`check-no-d.sh` inspects actual compile/link commands for seven representative
profiles; `check-config-header.py` proves the header covers every Kconfig
symbol; `trimcheck` compares the tick-only and full images by symbols and size.
`libs` archives the existing object groups under `build/lib/<profile>/` and
`gen_lib_manifest.py` writes their member/size/undefined-symbol manifest; `sdk`
adds the generated config header, the public headers and a `.pc`.  The public
`include/freewificam/` API and the consumer link gate are the next checkpoint
(§14).

Evidence: 82/82 profiles build with no hand `-D` (2026-11-03), the seven-profile
matrix is green, `host-test` passes FreeRTOS and ThreadX, and `check` parses all
82 defconfigs.  Behaviour is unchanged by construction for the flag removal
(the removed flags were already filtered); the A1 macro comparison and the
no-D sweep are recorded in the private `runs/build/2026-11-03-BUILD-02` ledger.

## 14. BUILD-02/M5 — the public API and the consumer gate (2026-11-03)

M5 turns the `libs`/`sdk` delivery shape into a consumer-facing checkpoint
without changing the image build or the M4 source lists:

* `include/freewificam/` publishes seven wrappers: `fw_config.h` (the generated
  header, with compile errors for a missing profile/board/backend identity),
  `fw_log.h`, `fw_osal.h`, `fw_wifi.h`, `fw_netif.h`, and two truthful gap
  headers, `fw_bus.h` (`FW_BUS_API_READY 0`) and `fw_core.h`
  (`FW_CORE_API_READY 0`).  The wrappers forward the real component headers;
  the sdk target installs those exact headers under `freewificam/deps/` so a
  prototype cannot drift from the archived object.
* `libs` gained two closure archives that the image used to get for free by
  linking every object: `libfw_support.a` (uLog + printf, needed by the console
  object) and `libfw_usbhost.a` (CherryUSB + the RK3568 cherryusb port, needed
  by a forced Wi-Fi link).  No source list changed; the filters only group
  existing `$(OBJS)`.
* `sdk` now installs `include/freewificam/{*.h,deps/*.h}`, the profile's
  archives, both manifest copies, `PORTING.md`, and a `freewificam.pc` whose
  prefix is the installed tree, whose version comes from `VERSION`, whose
  `Libs` wraps the profile's archives in `--start-group`/`--end-group`, and
  whose `Libs.private` carries `--gc-sections -lc -lgcc -lnosys`.  The previous
  `.pc` named fixed archives (absent on wired profiles) and the old target
  overwrote the public `fw_config.h` with the generated header; both are gone.
* `tests/host/sdk-consumer/check.sh` is the L1 gate: it compiles each header
  alone with only `dist/<profile>/include`, asserts no `-D` and no internal
  path in the pkg-config output, cross-compiles and links a log/config consumer
  with only pkg-config cflags/libs, and requires an empty `nm -u`, the
  `log_level_get` symbol, an AArch64 image and a manifest profile match.
  `./make.rtos sdk-check PROFILE=<name>` runs it.

Measured results: `net-netio-raw-tx256` (5 archives) and
`rtl8xxxu-8723bu-netio-ar-ht20-ba` (9 archives) both pass the gate; the Wi-Fi
probe leaves only the consumer's CMSIS-RTOS2 implementation and the two
baremetal linker-script symbols unresolved.  A plain Wi-Fi call links without
forcing the adapter and would do nothing at run time — that is the link-vs-run
gap recorded in `docs/PORTING.md` §5.

Not claimed: L2 (Zephyr `native_sim`), L3 (Zephyr/RK3568), L4 (RT-Thread) and
L5 (image regression) are not part of this checkpoint.  The clean-checkout
build also needs the generated shell/lwIP headers and the gitignored firmware
blobs; those prerequisites are documented in `docs/PORTING.md` §1.

Evidence: consumer gate output on both profiles, `freewificam.pc` and manifest
contents, and the measured unresolved sets are in `docs/PORTING.md` §5–§6.

## 15. CMake parity path (2026-11-03)

The CMake build is an *equivalence* build for the three representative profiles
(`net-netio-raw-tx256`, `rtl8xxxu-8723bu-netio-ar-ht20-ba`, `rtos-shell`), not a
second build system.  The platform Makefile keeps ownership of the source sets,
the per-driver flag worlds and the link until the per-component `sources.mk`
migration; CMake asks it for the expanded commands instead of copying them:

* `tools/build/cmake-query.py` runs `make -Bn image` (dry-run, read-only) and
  records every compile command (source, language, expanded flags) and the link
  command in `build/cmake/<profile>/build-description.json`.  Each source keeps
  the flags make gives that object, so the last-rule-wins flag worlds need no
  duplicate table.  The query refuses to emit a description that contains a
  `-D` token.
* `cmake/kconfig.cmake` runs `tools/build/kconfig-gen.py --profile <p>` at
  configure time (for the query) and as a custom command (`fw_kconfig`) during
  the build, writing the same pair under `build/kconfig/<p>/` that make reads.
  Every CMake compile also gets `build/kconfig/<p>` and
  `build/kconfig/<p>/include/generated` on the include path.
* `build_defs.h` is force-included for C translation units only.  Assembly must
  not see it: the FreeRTOS vectors select `spsr_el3`/`elr_el3` versus
  `spsr_el1`/`elr_el1` from those definitions, and an unconditional `-include`
  changed the image.
* `cmake/aarch64-none-elf.cmake` takes `AARCH64_CROSS_PATH` (else
  `aarch64-none-elf-*` on PATH), disables CMake build-type flags and leaves
  `OPT` to the profile.  `cmake/check-image.cmake` repeats the make gates
  (entry equals `_start`, vector 2 KiB aligned, backend symbols present, no
  undefined symbols).  One build directory per profile:
  `build/cmake/<profile>/`.

Usage (from the repository root):

```sh
cmake --preset rtos-shell
cmake --build build/cmake/rtos-shell
tools/build/compare-parity.py            # all three profiles, rebuilds both
tools/build/compare-parity.py --profiles rtos-shell --skip-make-build
```

`tools/build/compare-parity.py` builds each side and compares the source set,
the `CONFIG_*` symbol set (a fresh generation from the defconfig versus the
pair both builds used), and the `nm -g --defined-only` / `nm -u` name sets.  It
also scans every Make and CMake compile/link command for `-D`.  Image bytes are
reported for information only and are not a criterion: the three profiles do
happen to be byte-identical when both sides read the same generated header, but
that is a consequence of the shared flags, not a gate.

Known gaps: CMake still queries make for the source lists and flag worlds
rather than consuming `sources.cmake` directly; only the FreeRTOS backend's
gates are exercised; the CMake path does not cover `libs`, `sdk`, host tests,
`matrix`, `trimcheck` or the CI board wrappers; presets exist only for the three
profiles above.
