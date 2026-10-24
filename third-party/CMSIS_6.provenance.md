# third-party/CMSIS_6 - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/ARM-software/CMSIS_6.git |
| Pin | tag `v6.3.0`, commit `45dab712ad84f8cbbf2b7bfc089c19088507df6f` |
| License | Apache-2.0 (`LICENSE` at the tree root) |
| Vendoring | **sparse flat import** (not a submodule): only the RTOS2 API header, the Driver API headers and the A-profile core headers are carried, byte-identical to the pin. `patches/CMSIS_6/MANIFEST.sha256` lists every carried file; verify from this directory with `sha256sum -c ../../patches/CMSIS_6/MANIFEST.sha256`. Not carried: `CMSIS/Core/Include` for Cortex-M/Armv8-M, `CMSIS/RTOS2/Source`, `CMSIS/RTOS2/Template`, the DoxyGen and pack metadata, the test and example trees. |
| Fetched | 2026-10-24, from the pinned submodule checkout recorded by the RTOS reference tree (commit `19c5736`); the submodule HEAD was verified to be the pin above at import. |
| Local differences | none (zero patch) |
| Compiled set | none - header supply only: `CMSIS/RTOS2/Include/cmsis_os2.h` (API revision V2.3.0) and `os_tick.h`; `CMSIS/Driver/Include/` for the imported storage stack's driver types; `CMSIS/Core/Include/a-profile/` for the compiler and core intrinsics the adapter and the shadows reach. |
| Consumers | `rtos/os/cmsis2/` (backend facade) and every CMSIS-RTOS2 consumer; `rtos/os/freertos/` (the CMSIS-FreeRTOS adapter); `rtos/ports/fsl_sdmmc/shadow/` (the storage stack's OS-abstraction shadows). |
| Reason | Single, upstream source of the CMSIS-RTOS2 API header and of the CMSIS Core compiler layer. The release tag is preferred over the development tip found in the frozen baseline tree. |

Notes:

* The host tier uses `rtos/os/cmsis2/host-compat/` stand-ins instead of the ARM-only
  core headers, so the host build never reaches this tree's compiler layer.
* The RTOS2 source (the OS-tick drivers for Cortex-M boards) is not carried: this
  project supplies its own tick from the board's generic timer.