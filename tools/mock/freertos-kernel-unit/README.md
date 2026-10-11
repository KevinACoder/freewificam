# freertos-kernel-unit - Gate1 suite

The official upstream unit tests for the FreeRTOS kernel primitives, run on the
development host: 43 test binaries over the queue (including semaphore, mutex and
queue-set suites), event group, list, stream buffer and message buffer modules.
Each binary is a self-contained CMock/Unity test suite that links the real kernel
sources against generated mocks of the port, so the kernel logic is exercised
without the POSIX port and without the CMSIS-RTOS2 adapter.

## Layout

| Path | What it is |
|---|---|
| `third-party/FreeRTOS-Kernel-tests/` | the upstream test tree, copied verbatim (see its provenance file) |
| `third-party/FreeRTOS-Kernel-tests/CMock` | ThrowTheSwitch/CMock submodule, pinned; brings Unity and CException |
| `third-party/FreeRTOS-Kernel` | the kernel under test (the suite's `KERNEL_DIR`) |
| `tools/mock/freertos-kernel-unit/build.sh` | builds every R1 binary |
| `tools/mock/freertos-kernel-unit/run-tests.sh` | runs and judges every built binary |
| `tools/mock/freertos-kernel-unit/modules.sh` | the one place that declares the R1 modules and the make overrides |

## How the adaptation works

The upstream build derives `FREERTOS_DIR`/`KERNEL_DIR` from its own position inside
the FreeRTOS main repository and defaults `CC` to `/usr/local/bin/gcc`. This suite
passes `KERNEL_DIR`, `CC` and `BUILD_DIR` as make command-line variables, which
override those assignments and travel down to every sub-make. No upstream file is
edited, and nothing is written inside either upstream tree: `BUILD_DIR` points at
`tools/mock/build/freertos-kernel-unit/out` and `GCOV_PREFIX` redirects the coverage
counters a run writes, so both trees stay byte-clean.

The module list is the R1/R2 boundary: R1 covers the modules whose pipeline needs no
`unifdef` (queue, event_groups, list, stream_buffer, message_buffer). The timers,
tasks and smp modules expand `FreeRTOSConfig.def` through `unifdef` and are the R2
slice.

## Running it

```
make -C freewificam mock freertos-kernel-unit      # through the container
sh tools/mock/freertos-kernel-unit/build.sh       # build only
sh tools/mock/freertos-kernel-unit/run-tests.sh   # judge what was built
```

## What a pass means

`run-tests.sh` accepts a binary only when all three hold: it exits 0, it prints the
Unity summary `<N> Tests 0 Failures 0 Ignored`, and it ends in `OK`. Unity's exit
code counts failures only, so the ignored count is asserted separately - a suite may
not mask a case by skipping it. Per-binary logs and a `summary.tsv` land under
`tools/mock/build/freertos-kernel-unit/logs/`.

The expectations are upstream's own test bytes. If a binary goes red, the suite is
reporting a real disagreement between this kernel pin and the upstream tests; the
fix is never to edit the upstream test or skip it.

## Prerequisites

`ruby` (CMock mocks and Unity runners are generated from the kernel headers at build
time) and `gcc`/`make`. `unifdef` is only needed by the R2 modules.
