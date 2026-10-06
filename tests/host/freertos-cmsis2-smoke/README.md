# freertos-cmsis2-smoke

Host smoke test for the FreeRTOS kernel behind the CMSIS-RTOS2 API: the FreeRTOS POSIX
port runs one message queue and two threads, and the CMSIS-FreeRTOS adapter (upstream,
unpatched) provides the API surface.

## Build and run

    make -C tests/host/freertos-cmsis2-smoke
    ./tests/host/freertos-cmsis2-smoke/build/freertos-cmsis2-smoke

The run must exit with code 0, print these anchors in order on stdout and finish in well
under ten seconds:

    os2-smoke: kernel running
    os2-smoke: got 1
    os2-smoke: got 2
    os2-smoke: got 3
    os2-smoke: tick advanced n=<ticks>    (ticks >= 5)
    os2-smoke: PASS

## What is compiled

| Source | Role |
|---|---|
| `third-party/FreeRTOS-Kernel` | kernel sources, heap_4, POSIX port and its event utility |
| `third-party/CMSIS-FreeRTOS` | `CMSIS/RTOS2/FreeRTOS/Source/cmsis_os2.c` only; the vendored kernel copy under its `Source/` is not used |
| `third-party/CMSIS_6` | `CMSIS-RTOS2` API header `cmsis_os2.h` (single source) |
| `os/freertos/` | kernel configuration, fail-visible hooks, static-allocation donors, and the FreeRTOS side of the host exit seam |
| `os/cmsis2/` | backend facade (single application include) and the host stand-ins for the CMSIS Core headers |

## Host notes

* `os/cmsis2/host-compat/cmsis_compiler.h` and `cmsis_host_device.h` replace the CMSIS
  Core headers, which are ARM-only. SysTick is not defined, so the adapter's SysTick
  handler block disappears; the tick comes from the POSIX port's timer thread.
* The adapter's system-timer path needs device-side `OS_Tick_*` functions, which do not
  exist on a host. `--gc-sections` keeps it out of the image: the API is absent rather
  than backed by a stub that would return wrong values.
* `cmsis2_host_end_scheduler()` (declared in os/cmsis2/cmsis2.h, implemented by the
  FreeRTOS backend in os/freertos/cmsis2_host.c) ends the scheduler, because CMSIS-RTOS2
  has no kernel-stop call. `main()` then returns and the process exits 0.
