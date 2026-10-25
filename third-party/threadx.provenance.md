# third-party/threadx - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/eclipse-threadx/threadx.git |
| Pin | commit `44d7c95c582d415c4ad84527180b29c93c3bf664`; `git describe --tags` = `v6.5.1.202602a_rel-1-g44d7c95c`, kernel version string `6.5.1.202602a` |
| License | MIT (`LICENSE.txt` at the tree root) |
| Vendoring | **sparse flat import** (not a submodule): the kernel sources and headers, the Cortex-A55 board port and the Linux host port, byte-identical to the pin. `third-party/threadx/patches/MANIFEST.sha256` lists every carried file. Not carried: every other `ports/` target, the sample and test trees, the CMake project files, the Azure RTOS documentation. |
| Fetched | 2026-10-24, from the pinned checkout recorded by the RTOS reference tree (commit `19c5736`); the pin above was verified at import. |
| Local differences | none (zero patch); both builds adapt through configuration under `tests/board/rtos/platform/os/threadx/`, never by editing this tree. |
| Compiled set | board image: `common/src/*.c` (185 units, the whole kernel), `ports/cortex_a55/gnu/src/*.S` (the nine assembly units) and the board port's `tx_port.h`. Host tier: `common/src/*.c` and `ports/linux/gnu/src/*.c` (the eight Linux userspace port units). The two ports share flat `tx_*.c` basenames, so object names in the build mirror the path below the repository root. |
| Consumers | `tests/board/rtos/platform/os/threadx/` (kernel configuration, glue, heap seam, vectors, the CMSIS-RTOS2 backend); the host conformance tier. |
| Reason | The ThreadX side of the two-RTOS proof: the kernel the webcam line's carrier runs, with a Cortex-A55 EL1 port for the board and a Linux userspace port for the host. |

Notes:

* The Linux userspace port runs each ThreadX thread as a pthread and simulates
  interrupts with pthreads: the scheduler is the process main thread, the system tick
  is a timer pthread, and suspend/resume is delivered with `SIGUSR1`/`SIGUSR2`.
  `-D_GNU_SOURCE`, `-DTX_LINUX_MULTI_CORE` and `-lpthread -lrt` are its build
  requirements.
* The port reads the recursion depth of its own critical-section mutex through glibc
  internals, so the host tier requires a glibc host.
* ThreadX has no integrated heap: the board image binds `tlsf` over the linker's heap
  window (`tests/board/rtos/platform/os/threadx/board/rk3568-itx/heap.c`), the host binds the C library
  allocator.
* ThreadX has no thread-join primitive, so the CMSIS-RTOS2 backend exposes none; the
  conformance suite's thread case carries a terminal semaphore instead.