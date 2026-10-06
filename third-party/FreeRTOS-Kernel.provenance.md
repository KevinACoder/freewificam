# third-party/FreeRTOS-Kernel - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/FreeRTOS/FreeRTOS-Kernel.git |
| Pin | tag `V11.3.1`, commit `3a22924e0a9ddbbc8b0758881c33b3422a5cc20d` (2026-08-21) |
| License | MIT (`LICENSE.md` at the tree root) |
| Vendoring | git submodule; the gitlink is locked to the tag commit above |
| Fetched | 2026-10-06 |
| Local differences | none (zero patch); the host build adapts through configuration and stand-ins under `os/`, never by editing this tree |
| Compiled set | `Source/croutine.c`, `event_groups.c`, `list.c`, `queue.c`, `stream_buffer.c`, `tasks.c`, `timers.c`; `Source/portable/MemMang/heap_4.c`; `Source/portable/ThirdParty/GCC/Posix/port.c` and `.../Posix/utils/wait_for_event.c` |
| Consumers | `os/freertos/` (kernel configuration, hooks, static-allocation donors, lifecycle bridge); `tests/host/freertos-cmsis2-smoke/` (host build); the CMSIS-RTOS2 adapter compiles against these headers |
| Reason | Host-runnable FreeRTOS runtime with the upstream POSIX port, chosen so the RTOS line can be verified on the development host; the pin is the kernel commit recorded by the frozen baseline tree |

Notes:

* `heap_4.c` is used unmodified. The memory subsystem replacement planned for the memory
  milestone goes through the `pvPortMalloc`/`vPortFree` seam rather than a local allocator.
* The POSIX port drives context switching with pthread condition variables and signals
  (`SIGUSR1` for a resume, `SIGALRM` for the tick), so a host build needs no device and no
  SVC: `-pthread` is the whole platform requirement.
