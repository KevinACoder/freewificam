# third-party/FreeRTOS-Kernel - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/FreeRTOS/FreeRTOS-Kernel.git |
| Pin | tag `V11.3.1`, commit `3a22924e0a9ddbbc8b0758881c33b3422a5cc20d` |
| License | MIT (`LICENSE.md` at the tree root) |
| Vendoring | **full flat import** (not a submodule) of the upstream working tree at the pin (2026-10-26): every file the pin carries (891 files, including every `portable/` target, the test trees and the project files). `third-party/FreeRTOS-Kernel/patches/MANIFEST.sha256` lists every carried file. The image compiles the kernel sources, the public headers, the two ports this project uses and `heap_4.c`; the rest is present as upstream reference, per `AGENTS.md` §1.3. |
| Fetched | 2026-10-24, from the pinned checkout recorded by the RTOS reference tree (commit `19c5736`); the pin above was verified at import. |
| Local differences | none (zero patch); both builds adapt through configuration under `tests/board/rtos/platform/os/`, never by editing this tree. |
| Compiled set | board image: `tasks.c`, `list.c`, `queue.c`, `portable/MemMang/heap_4.c`, `portable/GCC/ARM_AARCH64_SRE/port.c` and `portASM.S`. Host tier: `tasks.c`, `list.c`, `queue.c`, `event_groups.c`, `stream_buffer.c`, `timers.c`, `croutine.c`, `portable/MemMang/heap_4.c`, `portable/ThirdParty/GCC/Posix/port.c` and `portable/ThirdParty/GCC/Posix/utils/wait_for_event.c`. |
| Consumers | `tests/board/rtos/platform/os/freertos/` (kernel configuration, board glue, vectors, the critical-section extension); the host conformance tier; the CMSIS-FreeRTOS adapter compiles against these headers. |
| Reason | The FreeRTOS side of the two-RTOS proof: one kernel with an AArch64 EL1 port for the board and the upstream POSIX port for the host, so the same application source runs on both tiers. |

Notes:

* `heap_4.c` is used unmodified (64 KiB arena, configured in
  `tests/board/rtos/platform/os/freertos/board/rk3568-itx/FreeRTOSConfig.h`).
* The POSIX port drives context switching with pthread condition variables and
  signals, so the host tier needs no device and no SVC: `-pthread` is the whole
  platform requirement.
* The AArch64 SRE port's exception level is a build-time decision (`-DGUEST`);
  without it the port emits the EL3 variant and the first yield executes SMC.