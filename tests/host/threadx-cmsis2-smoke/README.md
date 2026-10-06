# threadx-cmsis2-smoke

Host smoke test of the ThreadX kernel behind the CMSIS-RTOS2 API. The kernel runs as an
ordinary Linux process through its own userspace port, so the RTOS side of the project can
be exercised without a board.

## Build and run

```
make -C tests/host/threadx-cmsis2-smoke
./tests/host/threadx-cmsis2-smoke/build/threadx-cmsis2-smoke
```

`make -j` is supported; the build compiles the kernel and the port, so a first build takes
noticeably longer than the sibling FreeRTOS test.

## What it checks

Two threads at normal priority share one message queue. The producer publishes 1, 2, 3 with
zero timeout, delays five ticks and verifies that the kernel tick advanced; the consumer
blocks on the queue and drains it.

The run is accepted when it exits with code 0, finishes well under ten seconds, and prints
these lines on stdout in this order:

```
os2-smoke: kernel running
os2-smoke: got 1
os2-smoke: got 2
os2-smoke: got 3
os2-smoke: tick advanced n=<ticks>      (ticks >= 5)
os2-smoke: PASS
```

Diagnostics - the CMSIS priority map and any failure report - go to stderr, so stdout stays
a clean result channel. `os2-smoke: FAIL <what>` on stderr means the run aborted.

## What is compiled

| Source | Role |
|---|---|
| `third-party/threadx/ports/linux/gnu/src/*.c` | the kernel's Linux userspace port: scheduler, thread build, context save/restore, simulated timer interrupt |
| `third-party/threadx/common/src/*.c` | the kernel itself |
| `third-party/CMSIS_6/CMSIS/RTOS2/Include/cmsis_os2.h` | the CMSIS-RTOS2 API header; never a local copy |
| `os/threadx/cmsis2_threadx.c` | the CMSIS-RTOS2 backend over this kernel |
| `os/threadx/tx_glue.c`, `heap.{c,h}`, `tx_user.h` | kernel configuration, the application entry point the kernel asks for, the allocation seam |
| `os/cmsis2/cmsis2.h`, `os/cmsis2/host-compat/` | backend selection, and the host stand-ins for the ARM-only CMSIS Core headers |

## Host notes

* **Nothing reaches the kernel before the kernel is entered.** The port's critical section
  is a recursive mutex that only becomes recursive while the kernel starts, so an object
  creation attempted before that deadlocks instead of failing. The backend therefore
  records message queues, semaphores, mutexes and threads asked for before the kernel start
  call and creates them from `tx_application_define()`. The object ids are valid from the
  moment they are returned; until the kernel runs, calls that use them are refused.
* The port needs glibc: it reads the recursion depth of its own critical-section mutex
  through a glibc internal field.
* The consumer returns from its thread function instead of calling `osThreadExit()`: this
  backend does not implement that call, and a returned thread is parked by the backend.
* The kernel is started by `osKernelStart()` and never comes back - the port's scheduler is
  the process main thread. The run ends through `cmsis2_host_end_scheduler()`, which
  flushes and exits, so the process exit code is the test result.
* ThreadX stack checking is deliberately disabled here: the port points the recorded stack
  pointer at a fixed location and never updates it, so the kernel's stack checks could not
  fire even if they were enabled.
