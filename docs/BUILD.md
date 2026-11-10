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
| `rk3568-usb-msc` | tick usb_xhci | CherryUSB xHCI: enumerate the U-disk, block round trip, sweep |

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
`libfw_usbhost`, `libfw_drivers`, `libfw_app`, plus `manifest.json`
(members, undefined symbols, size). The archives are a delivery shape of the
image, not a second build. `components/liblinux80211` joins this list as
`liblinux80211.a` when it lands.

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
