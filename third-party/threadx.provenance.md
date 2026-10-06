# third-party/threadx - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/eclipse-threadx/threadx.git |
| Pin | commit `44d7c95c582d415c4ad84527180b29c93c3bf664` (2026-06-30); `git describe --tags` = `v6.5.1.202602a_rel-1-g44d7c95c`, kernel version string `6.5.1.202602a` (`ports/linux/gnu/inc/tx_port.h`) |
| License | MIT (`LICENSE.txt` at the tree root) |
| Vendoring | git submodule; the gitlink is locked to the commit above |
| Fetched | 2026-10-06 |
| Local differences | none (zero patch); the host build adapts through configuration and stand-ins under `os/`, never by editing this tree |
| Compiled set | `ports/linux/gnu/src/*.c` (8 files: the Linux userspace port) and `common/src/*.c` (185 files, the kernel); both sets together are the complete runtime, which is why the build links every object instead of an archive |
| Consumers | `os/threadx/` (kernel configuration, glue, heap seam, CMSIS-RTOS2 backend); `tests/host/threadx-cmsis2-smoke/` (host build) |
| Reason | Host-runnable ThreadX runtime, so the second kernel of the CMSIS-RTOS2 abstraction can be verified on the development host without a board; the pin is the commit the frozen baseline tree and the board work of this project both record |

Notes:

* The Linux userspace port runs each ThreadX thread as a pthread and simulates interrupts
  with pthreads: the scheduler is the process main thread (`src/tx_thread_schedule.c`), the
  system tick is a timer pthread, and suspend/resume is delivered with `SIGUSR1`/`SIGUSR2`.
  `-D_GNU_SOURCE`, `-DTX_LINUX_MULTI_CORE` (pins the process to one core) and `-lpthread -lrt`
  are its build requirements.
* The port reads the recursion depth of its own critical-section mutex through glibc
  internals (`pthread_mutex_t.__data.__count` in `inc/tx_port.h`), so a glibc host is
  required; musl cannot compile this port.
* The port fakes the thread stack pointer at thread build time
  (`src/tx_thread_stack_build.c` points it at the recorded stack top and never updates it,
  because the real stack is the pthread's). ThreadX stack checking can therefore never
  fire on this port and is deliberately left disabled in `os/threadx/tx_user.h`.
* The example build's switches `-DTX_LINUX_DEBUG_ENABLE` and `-DTX_ENABLE_EVENT_TRACE` are
  deliberately not passed: they add logging and trace noise, not behaviour.
* Later lightweight tags exist upstream (`v6.5.1.202602_rel`, `v6.5.2.202603_rel`); the pin
  stays at the recorded baseline commit rather than moving to the newest tag.
