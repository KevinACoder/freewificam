# freertos-cmsis2-smoke - the FreeRTOS host runtime contract

This suite covers the FreeRTOS backend of the host runtime through the CMSIS-RTOS2 API.
Its first slice is the **64-bit handle cases**: the upstream CMSIS-FreeRTOS adapter packed
flag bits into mutex and timer handles through `uint32_t` casts, so on a 64-bit host the
handle lost everything above 4 GiB - `osMutexAcquire` faulted and the timer callback
dispatch dereferenced a truncated pointer. The fix is a patch replayed by the build
(`patches/CMSIS-FreeRTOS/`).

## What runs

| Case | What it does | Why it is here |
|---|---|---|
| `c9-mutex` | recursive mutex: create, acquire twice, release twice | the flag bit lives in the handle itself, so creation and every acquire round-trip the pointer |
| `c9-timer` | periodic timer: create, start, callbacks, stop, delete | the callback record is stored as the timer ID, so the expiry dispatch round-trips a pointer |
| `c9-mempool` | pool over a static array: create, alloc, free, delete | the same 32-bit cast sits in the alignment test; only low bits reached the mask, so this case documents that boundary rather than a fault |
| `c9-precondition-64bit` | the three runs above reported no unmet precondition | the anti-false-green gate: a low-address build (no PIE, 32-bit host) would exercise nothing, so a case whose handle is not above 4 GiB fails instead of passing |

The cases live in `tests/host/freertos-cmsis2-smoke/c9-handle-trunc.c`; one case per
invocation, so a fault is the process's own business. The same objects also build the
runtime smoke scenario's binary.

## The red control

A suite that cannot go red proves nothing, so the same cases can be run against the pinned
bytes:

```
make -C tests/host/freertos-cmsis2-smoke clean
make -C tests/host/freertos-cmsis2-smoke PATCHES=off
sh tools/mock/freertos-cmsis2-smoke/check.sh --expect-red
```

It asserts the exact historical signature: mutex exits 2 (the handle is already truncated
at creation, so the precondition check refuses to pretend the case ran), timer dies with
signal 11 (the callback dispatch faults), mempool passes. The patched direction is the
runtime result of this suite; the pinned direction differs from it in a way that is
visible in the build too: against the pinned bytes the adapter compiles with 23
pointer-cast warnings, with the patch replayed the same build is warning-free.

`PATCHES=off` builds into its own object directory (`build/pinned/`), so a stale object
from one mode can never satisfy the other.

## What is not here yet

The runtime smoke anchors of `tests/host/freertos-cmsis2-smoke` and a same-shape
comparison with the ThreadX host suite are a later slice of this suite.
