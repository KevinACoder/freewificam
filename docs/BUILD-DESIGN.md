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

The authoritative set is the 51 names below (the machine-readable copy is
`kconfig-gen.py`'s `MODELED`; the split here is for reading):

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

Types: five choices (`BACKEND`, `BOARD`, `OPT`, `PROFILE`,
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

The residual `CONFIG_LEVERS` keeps what Kconfig does not model: `-DLWIP_*`,
`-DNET_WATCH=1`, `-DCONFIG_NETIO_CPU_KHZ_LATE=...`, `-DLWIPERF_TCP_MAX_IDLE_SEC`
and any lab-patch define.  The generator refuses a modeled `-DCONFIG_<name>=`
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
| `generated/config.mk` | the only file the Makefile consumes |
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
M7 converges `tests/` and the docs last.  The `.mk` retirement stays done;
the converter and `dump-vars.py` are the frozen record of how the 81 profiles
were read.

## 12. Open items

* The 35/47 tier is a draft; the owner confirms it before CI changes.
* The M1 follow-up closed the stale profiles: all 81 defconfigs build, and
  `pcie-7260` boots 2/2 on the RK3568.  The firefly images build but were not
  booted here (no firefly station in this round); the wlan images build but
  run long sink loops, so they are compile/boot-anchor only.
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
