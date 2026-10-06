# third-party/CMSIS-FreeRTOS - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/ARM-software/CMSIS-FreeRTOS.git |
| Pin | tag `v11.3.0`, commit `4dd0035148cdc106f892877c27cba77d1de717d2` |
| License | Apache-2.0 (`LICENSE` at the tree root); the vendored kernel copy under `Source/` is MIT (`Source/LICENSE.md`) |
| Vendoring | git submodule; the gitlink is locked to the tag commit above |
| Fetched | 2026-10-06 |
| Local differences | none (zero patch) |
| Compiled set | `CMSIS/RTOS2/FreeRTOS/Source/cmsis_os2.c` only |
| Consumers | `os/cmsis2/` (backend facade); `tests/host/freertos-cmsis2-smoke/` (host build) |
| Reason | Official CMSIS adoption of FreeRTOS, i.e. the CMSIS-RTOS2 layer itself. Taking it upstream keeps the layer a thin adaptation instead of a re-implementation, and the API header stays the single one from CMSIS_6 |

Notes:

* `Source/` at the repository root is a plain vendored copy of the FreeRTOS kernel made by
  upstream. This repository neither compiles nor references it; the kernel comes from
  `third-party/FreeRTOS-Kernel`, so only one kernel exists in the build.
* `clib_os.c` (newlib hooks), `handlers.c` (Cortex-M interrupt handlers) and
  `freertos_evr.c` (Event Recorder) are excluded: none of them applies to a host build.
* The adapter is compiled against FreeRTOS-Kernel V11.3.1 while this release targets the
  11.3.0 kernel; the API family matches, and a build failure would be treated as a pin
  problem rather than worked around by editing upstream sources.
* Host portability comes from `os/cmsis2/host-compat/` (a CMSIS Core compiler stand-in and
  a device-header stand-in providing the interrupt-state intrinsics). `SysTick` is not
  defined there, so the adapter's SysTick handler block is not compiled.
