# third-party/CMSIS_6 - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/ARM-software/CMSIS_6.git |
| Pin | tag `v6.3.0`, commit `45dab712ad84f8cbbf2b7bfc089c19088507df6f` |
| License | Apache-2.0 (`LICENSE` at the tree root) |
| Vendoring | **sparse flat import** (not a submodule): only the RTOS2 API header, the Driver API headers and the A-profile core headers are carried, byte-identical to the pin. `third-party/CMSIS_6/patches/MANIFEST.sha256` lists every carried file; verify from `third-party/CMSIS_6/` with `sha256sum -c patches/MANIFEST.sha256`. Not carried: `CMSIS/Core/Include` for Cortex-M/Armv8-M, `CMSIS/RTOS2/Source`, `CMSIS/RTOS2/Template`, the DoxyGen and pack metadata, the test and example trees. |
| Fetched | 2026-10-24, from the pinned checkout recorded by the RTOS reference tree (commit `19c5736`); the pin above was verified at import. |
| Local differences | none (zero patch) |
| Compiled set | none - header supply only: `CMSIS/RTOS2/Include/cmsis_os2.h` (API revision V2.3.0) and `os_tick.h`; `CMSIS/Driver/Include/` for the imported storage stack's driver types; `CMSIS/Core/Include/a-profile/` for the compiler and core intrinsics the adapter and the shadows reach. |
| Consumers | `tests/board/rtos/platform/os/cmsis2/` (backend facade) and every CMSIS-RTOS2 consumer; `tests/board/rtos/platform/os/freertos/` (the CMSIS-FreeRTOS adapter); `third-party/fsl_sdmmc/ports/shadow/` (the storage stack's OS-abstraction shadows). |
| Reason | Single, upstream source of the CMSIS-RTOS2 API header and of the CMSIS Core compiler layer. The release tag is preferred over the development tip found in the frozen baseline tree. |

Notes:

* The host tier uses `tests/board/rtos/platform/os/cmsis2/host-compat/` stand-ins instead of the ARM-only
  core headers, so the host build never reaches this tree's compiler layer.
* The RTOS2 source (the OS-tick drivers for Cortex-M boards) is not carried: this
  project supplies its own tick from the board's generic timer.