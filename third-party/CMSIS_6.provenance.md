# third-party/CMSIS_6 - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/ARM-software/CMSIS_6.git |
| Pin | tag `v6.3.0`, commit `45dab712ad84f8cbbf2b7bfc089c19088507df6f` |
| License | Apache-2.0 (`LICENSE` at the tree root) |
| Vendoring | git submodule; the gitlink is locked to the tag commit above |
| Fetched | 2026-10-06 |
| Local differences | none (zero patch) |
| Compiled set | none - header supply only: `CMSIS/RTOS2/Include/cmsis_os2.h` (API revision V2.3.0) and `CMSIS/RTOS2/Include/os_tick.h` |
| Consumers | `os/cmsis2/` (backend facade) and every CMSIS-RTOS2 consumer, including the CMSIS-FreeRTOS adapter |
| Reason | Single, upstream source of the CMSIS-RTOS2 API header. The release tag is preferred over the development tip found in the frozen baseline tree; the two copies of `cmsis_os2.h` are byte-identical, so nothing is lost by pinning the release |

Notes:

* `CMSIS/RTOS2/Source/` (OS tick drivers for Cortex-M boards) and `CMSIS/Core/Include`
  (ARM-only compiler and core headers) are not used by the host build; the host supplies
  stand-ins under `os/cmsis2/host-compat/`.
