# third-party/CMSIS-FreeRTOS - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/ARM-software/CMSIS-FreeRTOS.git |
| Pin | tag `v11.3.0`, commit `4dd0035148cdc106f892877c27cba77d1de717d2` |
| License | Apache-2.0 (`LICENSE` at the tree root) |
| Vendoring | **sparse flat import** (not a submodule): the adapter source and its include directory, the pin plus the applied `0001` patch. `third-party/CMSIS-FreeRTOS/patches/MANIFEST.sha256` lists every carried file. Not carried: `Source/` (upstream's own copy of the kernel - this project compiles the kernel from `third-party/FreeRTOS-Kernel/` only), `clib_os.c` (newlib hooks), `handlers.c` (Cortex-M interrupt handlers), `freertos_evr.c` (Event Recorder), the projects and the CMSIS-Pack metadata. |
| Fetched | 2026-10-24, from the pinned checkout recorded by the RTOS reference tree (commit `19c5736`); the pin above was verified at import. |
| Local differences | **one patch**, applied in the tree (see `third-party/CMSIS-FreeRTOS/patches/README.md`): `0001-cmsis_os2-do-not-launder-handles-through-32-bit-integers.patch` - the adapter round-tripped mutex and timer handles through `uint32_t`, which truncates them above 4 GiB, so on any 64-bit target `osMutexAcquire` faulted and the timer callback dispatch dereferenced a truncated pointer. The tree carries the applied patch; `patches/` is the record. |
| Compiled set | `CMSIS/RTOS2/FreeRTOS/Source/cmsis_os2.c`, once for the board FreeRTOS image and once for the host tier. |
| Consumers | `tests/board/rtos/platform/os/cmsis2/` (backend facade); `tests/board/rtos/platform/os/freertos/` (kernel configuration and the board's critical-section extension); the host conformance tier. |
| Reason | Official CMSIS adoption of FreeRTOS, i.e. the CMSIS-RTOS2 layer itself. Taking it upstream keeps the layer a thin adaptation instead of a re-implementation, and the API header stays the single one from `third-party/CMSIS_6/`. |

Notes:

* The adapter is compiled against FreeRTOS-Kernel V11.3.1 while this release targets
  the 11.3.0 kernel; the API family matches, and a build failure would be treated as
  a pin problem rather than worked around by editing upstream sources.
* The `osThreadJoinable` attribute is rejected by this adapter and no `osThreadJoin`
  exists: the conformance suite's thread case carries a terminal semaphore instead
  (recorded in the platform ADR).