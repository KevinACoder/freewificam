# MIGRATION - what came from the 0.1.0 reference tree, and what did not

This repository was re-shaped from the reference RTOS tree (its last tag,
`0.1.0`). This document is the inventory of that move: every path family in the
reference tree, the decision taken, and the reason. It exists so that "was this
left out on purpose?" is answerable without the reference checkout.

## Method

1. The reference tree was read from its **committed bytes** at commit
   `19c5736`; its working tree was not clean, so a working copy was not used.
   That commit carries 1,231 tracked files; the trees it imports as submodules
   are counted as one file each here and are described by their own pins.
2. Every path family was classified as one of: **imported** (bytes taken, with
   the divergences recorded), **re-shaped** (the content exists here in a
   different layout), **rewritten** (the document or file was written for this
   repository), or **skipped** (with the reason).
3. The result was checked against the repository's own source maps
   (`tests/board/rtos/SOURCE.md`, the `ports/` READMEs, the per-tree
   `provenance` files), which are the authority for what each tree is.

Reference totals by top-level directory: `arch/` 17, `board/` 36,
`components/` 335, `docs/` 10, `os/` 38, `patches/` 37, `ports/` 94,
`tests/` 354, `third-party/` 172, `tools/` 128, top-level files 10.

## Platform and RTOS kernel (`arch/`, `board/`, `os/`)

| Reference path | Files | Decision | Where it is here |
|---|---|---|---|
| `arch/aarch64/` | 17 | imported, re-shaped | `tests/board/rtos/platform/arch/aarch64/`, with the recorded divergences (one interrupt-controller file differs) |
| `board/rk3568-itx/` | 36 | imported, re-shaped | `tests/board/rtos/platform/board/rk3568-itx/`; the USB power/PHY/reset domain, the NVMe and the PCIe units are new here and recorded in their `SOURCE.md` |
| `os/baremetal/` | 7 | imported, re-shaped | `tests/board/rtos/platform/os/baremetal/` (byte-identical) |
| `os/cmsis2/` | 6 | imported, re-shaped | `tests/board/rtos/platform/os/cmsis2/` (one divergence) |
| `os/freertos/` | 10 | imported, re-shaped | `tests/board/rtos/platform/os/freertos/`, plus the board variant, the heap-size knob and the board heap face; all recorded in `SOURCE.md` |
| `os/threadx/` | 15 | imported, re-shaped | `tests/board/rtos/platform/os/threadx/`, plus the event-flag face, the equal-priority time slice and `osThreadGetState()`; all recorded in `SOURCE.md` |

The platform is a **subset** of the reference tree: it carries the drivers the
profiles use (console, MMIO, reset, clocks, eMMC, SDIO, GMAC, PCIe/NVMe, USB),
not every board file the reference tree carried.

## Components and ports (`components/`, `ports/`)

| Reference path | Files | Decision | Where it is here / why not |
|---|---|---|---|
| `components/linuxkpi/` | 208 | imported, re-shaped | `components/linuxkpi/rtos/` (the runtime subset) and `ports/` consumers; the lineage and divergences are in its `PROVENANCE.md` and `DIVERGENCES.md` |
| `components/net80211/` | 64 | imported, re-shaped | `components/net80211/` (core, `port/`, `compat/`, `crypto/`, `ports/`, `patches/`) |
| `components/skbuff/` | 7 | imported, re-shaped | `components/linuxkpi/rtos/skbuff/` (the pool and its lineage) |
| `components/printf/`, `tlsf/`, `ulog/` | 6 / 5 / 6 | imported | `third-party/printf/`, `third-party/tlsf/`, `third-party/ulog/` |
| `components/fsl_sdmmc/` | 13 | imported | `third-party/fsl_sdmmc/` (the NXP stack is the SDMMC/SDIO host) |
| `components/cherrysh/` | 17 | **skipped** | no image here has a shell; the reference suite used it for an interactive smoke step |
| `components/fatfs/` | 9 | **skipped** | no image here carries a filesystem; the storage line is block-level by design |
| `ports/lwip/` | 15 | imported | `third-party/lwip/ports/` |
| `ports/net80211/` | 28 | imported | `components/net80211/ports/` |
| `ports/fsl_sdmmc/` | 12 | imported | `third-party/fsl_sdmmc/ports/` |
| `ports/cherryusb/` | 8 | imported, re-shaped | `ports/cherryusb/`, plus the linuxkpi USB backend and the camera application half |
| `ports/pcie/` | 2 | imported | `ports/pcie/` |
| `ports/wpa_supplicant/` | 15 | **skipped** | this repository carries no supplicant; the RTOS images are open-network only (a known issue of this release) |
| `ports/cherrysh/` | 6 | **skipped** | with `components/cherrysh/` |
| `ports/fatfs/` | 8 | **skipped** | with `components/fatfs/` |

## Third-party trees (`third-party/`)

| Reference unit | Decision | Here |
|---|---|---|
| `third-party/CMSIS_6`, `CMSIS-FreeRTOS`, `FreeRTOS-Kernel`, `lwip`, `threadx` | imported as **flat full trees** | `third-party/<unit>/` with a `provenance` file and a digest record; the reference tree used submodules |
| `third-party/cherryusb` | imported as a flat full tree, with the patch series | `third-party/cherryusb/` with eight patches applied in the tree |
| `third-party/fsl_sdmmc` | imported | `third-party/fsl_sdmmc/` |
| `third-party/FreeRTOS-Kernel-tests` | **skipped** | a test tree for a component's own upstream; the component's conformance here is the host tier, not upstream's suites |

The vendor wireless drivers are larger here than in the reference tree:
`wireless-drivers/{rtw89,rtw88,rtl8xxxu,iwlwifi,rtl8189fs}` are the driver
trees, each with the same source + `ports/` + `patches/` shape.

## Patches (`patches/`)

| Reference path | Files | Decision | Where it is here |
|---|---|---|---|
| `patches/cherryusb/` | 7 | imported, re-shaped | `third-party/cherryusb/patches/` (0001-0008) |
| `patches/lwip/` | 6 | imported, re-shaped | `third-party/lwip/patches/` |
| `patches/CMSIS-FreeRTOS/` | 2 | imported, re-shaped | `third-party/CMSIS-FreeRTOS/patches/` |
| `patches/net80211/`, `patches/tlsf/`, `patches/cherrysh/` | 1 / 1 / 1 | imported or skipped with its unit | `components/net80211/patches/` and `third-party/tlsf/patches/` carry the digest records; the shell patch is skipped with the shell |
| `patches/rtl8189fs/` | 8 | imported, re-shaped | `wireless-drivers/rtl8189fs/patches/` |
| `patches/{iwlwifi,rtl8xxxu,rtw88,rtw89}/` | 2 each | re-shaped | these are digest records, not changes; each driver tree here carries its own `patches/MANIFEST.sha256` and README at the same pins |
| `patches/wpa_supplicant/` | 2 | **skipped** | no supplicant |
| `patches/README.md` | 1 | rewritten | `docs/PATCH-POLICY.md` and the per-tree `patches/README.md` |

The reference tree replayed its patches at build time; this repository applies
them in the tree and keeps `patches/` as the record (`docs/PATCH-POLICY.md`).

## Tests (`tests/`)

| Reference path | Files | Decision | Why |
|---|---|---|---|
| `tests/board/common/` | 25 | re-shaped | the shared case/suite framework became this repository's case interface (`tests/board/rtos/app/`) and the judge |
| `tests/board/suites.mk`, `README.md` | 2 | rewritten | the profile files (`tests/board/rtos/configs/`) and `tests/board/rtos/README`-grade documentation replace the suite registry |
| `tests/board/threadx-rk3568-{smoke,regression,emmc,sdio,pcie,iwlwifi,rtl8189,rtw88,rtw89,wlan}/` | 132 | re-shaped | each became a profile and its cases; the coverage is the same surface, one profile at a time |
| `tests/board/freertos-rk3568-{smoke,regression}/` | 9 | re-shaped | the same profiles are built for the FreeRTOS kernel |
| `tests/board/cherrysh-rk3568-smoke/` | 4 | **skipped** | shell image |
| `tests/board/fixtures/` | 18 | **skipped** | mock fixtures |
| **`tests/board/threadx-rk3568-uvc/`** | **10** | **imported and re-shaped** | the camera line: the two patches and the five application units are imported (see below); the suite frame, the shell steps and the image's own makefile are not |
| `tests/host/` | 154 | **skipped** | the host mock tier. It tests mock drivers, not the code this repository ships |

The camera suite's ten files map as follows:

| Reference file | Here |
|---|---|
| `tests/uvc_app.c` | `ports/cherryusb/class/uvc_app.c/.h` (bring-up and open policy added) |
| `tests/uvc_detect.c` | `tests/board/rtos/app/cases/uvc_detect.c` |
| `tests/uvc_open.c` | `tests/board/rtos/app/cases/uvc_open.c` |
| `tests/uvc_iso_smoke.c` | `tests/board/rtos/app/cases/uvc_iso_smoke.c` |
| `tests/uvc_stream.c` | `tests/board/rtos/app/cases/uvc_stream.c` |
| `main.c`, `Makefile`, `suite.mk`, `steps.tsv`, `judge.py` | not imported: this repository's case runner, profile file and judge cover the same ground |

## Tools (`tools/`)

| Reference path | Files | Decision | Why |
|---|---|---|---|
| `tools/mock/` | 119 | **skipped** | the mock tier |
| `tools/gates/` | 5 | re-shaped | this repository has `tools/check-commit-msg.sh` and `tools/check-release-docs.sh`; the reference scripts refer to checkouts and paths that do not exist here |
| `tools/prober/` | 2 | **skipped** | measurement tooling; records live outside this repository |
| `tools/patches/` | 1 | **skipped** | this repository applies patches in the tree, so there is no replay step |
| `tools/insight/README.md` | 1 | **skipped** | a pointer document for a private measurements tree |

## Documents and release metadata

| Reference path | Decision | Here |
|---|---|---|
| `docs/RELEASE-NOTES-0.1.md`, `docs/known-limitations.md`, `docs/README.md`, `docs/net80211.md`, `docs/bus-and-threading.md`, `docs/memory-model.md` | rewritten | `docs/RELEASE-NOTES-0.0.2.md`, `known_issues/`, and the source maps under `tests/board/rtos/` and the component `PROVENANCE` files |
| `docs/boards/rk3568-e4ap5g1-itx*` (including three images) | **skipped** | vendor manual material with no recorded redistribution grant |
| `README.md`, `README_zh.md`, `CONTRIBUTING.md`, `Makefile`, `.gitmodules` | rewritten or dropped | `README.md` written for this repository; no submodules and no top-level makefile here |
| `CHANGELOG.md`, `LICENSE`, `LICENSES.md`, `VERSION` | rewritten | this release's versions of them |

## Caveats

- The reference tree's working copy was dirty at the time of the move; nothing
  here was taken from it except where a component's `PROVENANCE.md` says so
  explicitly (the wireless import records one such working-tree edit).
- File counts are the reference commit's tracked files; a submodule counts as
  one entry in the inventory and its contents are described by its own pin.
- "Imported" always means the bytes were taken and are anchored by a
  `patches/MANIFEST.sha256` or a `provenance` record. Where the bytes differ,
  the difference is listed in the importing tree's `SOURCE.md` or
  `DIVERGENCES.md`, not hidden.
