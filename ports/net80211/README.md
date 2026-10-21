# ports/net80211 - the net80211 port layer

The first-party adaptation of the imported net80211 component
(`components/net80211/`) onto this project's stack. Three layers, one
implementation each:

| Directory | What it is |
|---|---|
| `osal/cmsis2/` | The single CMSIS-RTOS2 implementation of the kernel services the imported sources call (mutexes, condition variables, the serializer, time, sleeps, allocation seam). One implementation serves ThreadX, FreeRTOS and the host; the backend is chosen at link time. |
| `net/` | The thin mbuf clusters and the ifnet/ifmedia shells, including the single receive seam and the transmit serialization. |
| `compat/` | The thin kernel-service headers the imported sources include (`sys/mutex.h` and friends), plus `prelude.h`, the forced include that carries the NetBSD-world switches and the host-coexistence step. |

`seam.h` is the driver-side contract: a plain-C scalar vtable for the world
beyond net80211 (a Linux-family driver later). It shares no structure layout
with either world.

## How the pieces fit

* The imported sources keep their upstream include names. The build orders the
  search path as `ports/net80211/compat` (this port's machinery) before
  `components/net80211/compat/netbsd` (the imported vocabulary headers) before
  the component root and the toolchain, and force-includes
  `compat/prelude.h`. The vocabulary headers are byte-identical to the pin; the
  machinery headers here are ours (same names, our bytes).
* `port/port.h` of the component stays the library's stable interface (firmware
  lookup, presentation hooks, chip-driver registry, lifecycle, serializer
  entry points). This port implements the parts a host run needs, and its
  `net/` layer feeds the presentation hooks. The driver-side `seam.h` is a
  different face (what the state machine asks a driver to do); the two are not
  merged and there is no second driver registry.
* `osal/cmsis2/` allocates through one seam (`wlan_osal_alloc/free`). The host
  build backs the seam with the C library; a target build replaces
  `osal/cmsis2/osal_memory.c` (the memory subsystem owns that decision).

## Interrupt context

Interrupts only defer: an ISR may release a semaphore or queue a work item, and
must never enter net80211 or call a blocking service of this port. The
imported `spl*()` family is therefore a scheduling barrier in name only - real
exclusion comes from the port serializer, which every driver entry takes
(`wlan_port_serializer_lock`, or `if_start_lock` on the transmit path).

## Deviations

Every deliberate difference from the component's own shells and from the pin is
recorded in `DIVERGENCES.md`, with the file, the reason and the round that
needed it. The component itself stays byte-identical (its `DIVERGENCES.md`
records the trim).
