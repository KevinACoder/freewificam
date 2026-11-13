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

## 2. Profiles

| Profile | Cases | What it accepts |
|---|---|---|
| `rk3568-tick` | tick | console, MMU, GICv3, FreeRTOS port, CMSIS-RTOS2 facade, runner |
| `rk3568-net` | tick cmsis2 net | lwIP + GMAC: link, address, ping, TCP both ways, UDP probes, census |
| `rk3568-pcie-nvme` | tick pcie | DWC root complex, ITS/MSI, NVMe namespace, one real I/O completion |
| `rk3568-emmc` | tick cmsis2 emmc | fsl_sdmmc + DW SDHCI: card ID, block round trip, error path, sweep |
| `rk3568-usb-msc` | tick usb_ehci | CherryUSB EHCI: enumerate the U-disk, block zero, round trip (identity arm on a small disk) |
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
| `rk3568-iwl7260-ap5g` | tick pcie iwl7260_ap5g | the AP on the 5 GHz band (`CONFIG_AP_FREQ=5180`, `CONFIG_AP_HW_MODE="a"`, docs/HANDOFF-5GHZ.md P1): the case reports the wiphy's 5 GHz band and every candidate channel's flags/beacon verdict, then starts hostapd with `hw_mode=a` on the frequency it found beaconable; `PASS AP5` |
| `rk3568-iwl7260-ap5g-wpa2` | tick pcie iwl7260_ap5g | the same with `CONFIG_AP_WPA2=y`: passphrase, 4-way, `ptk=1 gtk=1 fail=0`, `PASS AP5W` (P2) |
| `rk3568-iwl7260-sta5g` | tick pcie iwl7260_sta5g | the station on the 5 GHz band (`CONFIG_STA_FREQ=5180`, empty passphrase = open): the imported supplicant associates, the case reads the association's frequency back out of cfg80211 and fails unless it is 5 GHz, then 56/1400 B ICMP 20/20; `PASS STA5` (P3) |
| `rk3568-iwl7260-sta5g-wpa2` | tick pcie iwl7260_sta5g | the same with `CONFIG_STA_PSK` set: EAPOL both ways, `ptk=1 gtk=1 fail=0`, `PASS STA5W` (P4) |
| `rk3568-iwl7260-mon` | tick pcie iwl7260_mon | the AC 7260 as an RX-only monitor on `CONFIG_MON_FREQ` (2432) for `CONFIG_MON_WINDOW_MS` (15 s); the role change is `SET_INTERFACE(MONITOR)` with the device down, then `dev_open` **then** `SET_WIPHY(freq)` (cfg80211 refuses the channel set while no monitor is running), and the frames go to the component's monitor sink, never to lwIP; `PASS MON` requires >=8 beacons/s, `bad=0` radiotap, an unchanged lwIP `rx_frames` and `tx_packets=0` |
| `rk3568-kpi-mock` | tick kpi_mock | the same engines over a synthetic host/card and the null egress |
| `rk3568-rtl8189fs-sdio` | tick rtl8189fs_sdio | the board's SDIO 8189FTV attaches through the imported fsl_sdmmc stack to the vendored vendor stack: card `024c:f179`, embedded firmware `FW_NIC 20306`, MAC, wiphy + netdev, `dev_open` (C3) |
| `rk3568-rtl8189fs-sta` | tick rtl8189fs_sta | C3 plus the real `NL80211_CMD_TRIGGER_SCAN`/`CONNECT` streams through nl80211, dispatched to the vendor driver's cfg80211 ops: scan, open association, lwIP netif, 56/1400 B ICMP both ways (C4–C6 open) |
| `rk3568-rtl8189fs-c6t` | tick rtl8189fs_sta rtl8189fs_c6t | the same join, then the throughput phase table: 30 s sampled TCP downlink, 35 Mbit/s UDP downlink sink, TCP/UDP uplink, board-local loopback; each phase carries the adapter/stack/pool and the SDIO claim/pump + vendor queue gauges |
| `rk3568-rtl8189fs-udpdn` | tick rtl8189fs_sta rtl8189fs_c6t | the C6T diagnostic ordering `CONFIG_C6T_UDP_FIRST`: the receive-only downlink is offered before any TCP phase, so one boot separates an RX-path stall from a transmit/receive concurrency stall |
| `rk3568-rtw89-8851bu-c3` | tick rtw89_c3 | the RTL8851BU (`0bda:b851`, USB) attaches through the linuxkpi USB engine and CherryUSB: the engine's `.lkpi_module` walk registers the driver, the board's USB domain starts the EHCI roots, the driver's probe downloads `rtw8851b_fw-1.bin` from the compiled-in registry, `ieee80211_register_hw` creates `wlan0`, and `dev_open` reaches start (C3) |
| `rk3568-rtw89-8851bu-c4` | tick rtw89_c4 | C3 plus the real `NL80211_CMD_TRIGGER_SCAN` stream through nl80211: mac80211 -> rtw89 -> the USB bulk endpoints, the BSS table carries the bench AP (C4) |
| `rk3568-rtw89-8851bu-c5` | tick rtw89_c5 | C4 plus `NL80211_CMD_CONNECT`: open association, the lwIP netif over `wlan0`, 56/1400 B ICMP 20/20 (C5/C6-open) |

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

- `s0-compile` writes `runs/S0/run-<n>/{compile/*.log,obj/,summary.tsv}` (the
  project-root `runs/`, beside the repo) and copies the summary to
  `runs/S0/summary.tsv`.  Flags are the kpi world's plus the stack include roots
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

- `CONFIG_LIBLINUX80211_WIRELESS` puts the imported `stack/` TUs in an image;
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
  (`osMemoryAlloc -> pvPortMalloc`), not `CONFIG_HEAP_SIZE`.
- Build-only targets (no image): `s1-driver-compile` (72 TUs, `runs/S1/driver/`)
  and `s1-link` (partial link + classified undefined list,
  `runs/S1/link/`). `s0-compile` is unchanged.
- The firmware blob is generated, not committed: `tools/rtos/fw-blobs.py`
  writes `components/liblinux80211/fw/iwl7260_fw_blob.c` from the pinned GT
  `iwlwifi-7260-17.ucode` (sha256 checked) and the path is git-ignored.
