# DIVERGENCES

Imported files are byte-identical by default. Every edit is recorded here with
its reason; the source commit and blob hash of each file are in
`IMPORT-MANIFEST.tsv`.

| # | File | Change | Reason |
|---|---|---|---|
| 1 | `tests/board/rtos/build/Makefile` | rewritten as a trim of the source: FreeRTOS-only backend section, RK3568-only board section, no host tier, no wireless/CMake/SDK worlds | this line carries four bus/network images and one platform; the mechanisms (COMPILE_C, flag stamps, per-world flags, linker gates, libs) are kept byte-for-byte in shape |
| 2 | `tests/board/rtos/Kconfig` | `BACKEND_THREADX/ZEPHYR` and their derived defaults removed; ThreadX-only `TX_*` knobs removed | only the FreeRTOS backend is imported; a backend that the build rejects must not be offered |
| 3 | `tests/board/rtos/platform/board/Kconfig` | firefly board symbols and `UVC_USB_BUS` removed | only the RK3568 board is imported |
| 4 | `tests/board/rtos/app/Kconfig` | WLAN/netio/lwIP-window knobs removed, `CASES` kept | those worlds are not imported; the case list is the one symbol every image needs |
| 5 | `tests/board/rtos/app/cases.h` | case declarations trimmed to tick, cmsis2, net, emmc, pcie, usb_xhci, usb_ehci | same reason as 4; `usb_ehci` was imported on 2026-11-10 when the bench's disk was found on the EHCI side |
| 6 | `tests/board/rtos/app/runner.c` | case table trimmed to the same seven; the wireless big-stack exception removed | same reason as 4 |
| 7 | `tests/board/rtos/config.mk` | default profile `rk3568-tick` | the previous default (`firefly-v2-rtw88-sdio`) is not carried |
| 8 | `tests/board/rtos/platform/build_defs.h` | added the eMMC case's `SDMMC_PORT_SDIO_HOST_TYPE`/`_HOST_ID` | the port header requires an image to name its controller; the eMMC case had no profile in the previous tree, so the case-derived line never existed there |
| 9 | `tools/build/kconfig-gen.py` | `KCONFIG_FILES` trimmed to the carried tree; `CASES` trimmed (seven cases); `legacy_macros` trimmed to the macros the imported sources read | the symbol set must describe this line, not the previous one |
| 10 | `tools/build/rtos.mk` | SDK/CMake/host targets removed; `images` aliased to `image`; default matrix is this line's profiles | those targets' worlds are not imported |
| 11 | `tools/build/check-no-d.sh` | default profile list is this line's | same reason as 10 |
| 12 | `tools/build/ci.sh` | one board; firefly targets and the SDK gate removed | same reason as 10 |
| 13 | `tools/build/dump-vars.py` | added (not in the original import group) | `check-build-contract.sh` calls `make.rtos dump-vars`; without it the contract gate cannot run |
| 14 | `make.rtos` | usage text trimmed | the targets it named no longer exist |
| 15 | `Makefile` | forwards `rk3568_ci` only | same reason as 10 |
| 16 | `tests/board/rtos/app/cases/usb_msc.c` | window follows the disk (`min(32 MiB, blocks - 4 MiB)`, 1 MiB aligned), sweep command count follows the window, and a disk smaller than the old window gets the identity arm (`usbh: msc ident-only`) | the bench's disk is 7.8 MiB, not the 29 GiB one the geometry was sized for; the old geometry is unchanged for a disk that reaches it |
| 17 | `tools/rtos/judge.py` | `CASES` trimmed to the seven carried cases; the USB storage check accepts the `ident-only` arm | the judge must judge this line's images, and an arm the image announces as identity-only is not missing its throughput anchors |

## Rules

- Prefer a patch that upstream could accept over an in-tree edit.
- When an in-tree edit is unavoidable, keep it minimal, mark it with
  `/* FREEWIFICAM: <what and why> */`, and add a row above.
