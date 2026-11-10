# PROVENANCE

Files in this repository are either **written here** or **imported**. An
imported file gets a row in the table below and one line in
`IMPORT-MANIFEST.tsv` (source commit, path, blob hash).

## The import

| | |
|---|---|
| Source repository | the previous line's `freewificam`, branch `master` |
| Source commit | `fdceccb95dc85c70229709973ffcd150e643c7cd` |
| Method | per-file `cp` from a clean working tree at that commit; every file's blob hash equals the source commit's blob (`IMPORT-MANIFEST.tsv`) |
| Direction | one way (see `LICENSES.md`) |

The build files are the previous tree's as a blueprint, then trimmed to the
worlds this line carries; every edit they received is in `DIVERGENCES.md`.

## What was imported

| Destination | Source path | Files | License | Byte-identical |
|---|---|---|---|---|
| `tests/board/rtos/platform/arch/aarch64/` | same | 17 | BSD-3-Clause | yes |
| `tests/board/rtos/platform/board/rk3568-itx/` | same | 43 | BSD-3-Clause | yes |
| `tests/board/rtos/platform/os/{freertos,baremetal,cmsis2}/` | same | 23 | BSD-3-Clause | yes |
| `tests/board/rtos/platform/{build_defs.h,*/sources.mk,board/Kconfig}` | same | 6 | BSD-3-Clause | `build_defs.h`, `board/Kconfig` edited |
| `tests/board/rtos/app/` | same, case subset | 22 | BSD-3-Clause | `cases.h`, `runner.c`, `Kconfig` edited |
| `tests/board/rtos/{Kconfig,config.mk}` | same | 2 | BSD-3-Clause | both edited |
| `tests/board/rtos/build/` | same, minus `wlan.mk`, `iwlwifi.mk`, `rtw88*.mk`, `rtl8xxxu.mk`, `rtw89.mk`, `aic8800.mk`, `uvc.mk`, `sdio_mci.mk` | 3 | BSD-3-Clause | `Makefile` rewritten (trim), `usb.mk`/`fw.mk` byte-identical |
| `tools/build/` | same, minus the CMake/parity/SDK tools | 11 | BSD-3-Clause | 8 edited or added |
| `tools/kconfig/` | same | 5 | ISC | yes |
| `tools/rtos/judge.py` | same | 1 | BSD-3-Clause | yes |
| `third-party/FreeRTOS-Kernel/` | same, compiled closure | 33 | MIT | yes |
| `third-party/CMSIS_6/` | same, RTOS2/Core/Driver headers | 8 | Apache-2.0 | yes |
| `third-party/CMSIS-FreeRTOS/` | same, `CMSIS/RTOS2/FreeRTOS` | 5 | Apache-2.0 AND MIT | yes |
| `third-party/lwip/` | same, `src/` + `ports/` + `patches/` | 327 | BSD-3-Clause style | yes (the tree carries its patches applied) |
| `third-party/cherryusb/` | same, `core/ class/{hub,msc}/ port/{xhci,ehci}/ common/ osal/ ports/osal/cmsis2/ patches/` | 66 | Apache-2.0 | yes (patches applied) |
| `third-party/fsl_sdmmc/` | same, whole tree | 27 | BSD-3-Clause | yes |
| `third-party/{printf,tlsf,ulog}/` | same | 17 | MIT / BSD-3 / MIT | yes |
| `LICENSE` | same | 1 | BSD-3-Clause | yes |
| `make.rtos`, `Makefile` | same | 2 | BSD-3-Clause | both edited |

`third-party/*.provenance.md` travel with their trees and carry the upstream
pin, the patch records and the license; they are the authority for the trees,
this table is the summary.

## What was deliberately not imported

| Not imported | Why |
|---|---|
| `components/net80211/` | this line replaces the stack with mac80211; the net80211 line stays in the previous repository as the control arm |
| `components/linuxkpi/` | its mac80211/cfg80211 substitutes (`linuxkpi_80211*.c`, `linuxkpi_txq.c`, `linuxkpi_wiphy_work.c`, `linuxkpi_cfg80211.c`) would collide with the real stack. The driver-facing part lands in `components/liblinux80211/` with the mac80211 work |
| `wireless-drivers/*` | no driver in this round's images; they land with `liblinux80211` |
| `firmware/` | no driver to feed; the firmware blobs land with their drivers |
| `components/shell/`, `third-party/cherrysh/` | the console shell is not part of the four acceptance images |
| firefly/ThreadX/Zephyr platform directories | this line targets RK3568 + FreeRTOS; the CMSIS-RTOS2 facade stays so another backend is additive |
| `tests/board/linux-ko/`, `dist/`, the CMake entry | not part of this round (see `STATUS.md`) |

## Rules

1. An import records source, commit, path, license and whether the bytes match.
2. Byte-identical files do not appear in `DIVERGENCES.md`. Any edit does.
3. Imports move in one direction only; see `LICENSES.md`.
