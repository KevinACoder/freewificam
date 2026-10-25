# components/net80211/ports - divergences

Deliberate differences of this port layer from the component's own shells (which
it replaces) and from the imported pin. The component's vocabulary headers stay
byte-identical; every behaviour difference of this port is listed here.

| # | File | Change | Reason | Round |
|---|---|---|---|---|
| D-1 | `net/mbuf.c` | One cluster per mbuf over the port allocation seam; the driver DMA padding of the component's embox shell is not carried over (the cluster is only 16-byte aligned) | The port has no DMA consumer yet; the driver round adds the padding its bus needs and records it here | M0-8b |
| D-2 | `net/mbuf.c` | `max_linkhdr` starts at 16 (the component's value) and is raised by the stack itself once it knows the frame header it needs | Same observable behaviour as the component shell; kept explicit so the value has one owner | M0-8b |
| D-3 | `net/ifnet.c` | `ifmedia_baudrate()` reports a fixed 54 Mbit/s; it is not derived from the media word | The component shell does the same; the state machine only compares media words, and the driver round can derive the real rate | M0-8b |
| D-4 | `net/ifnet.c` | `ieee80211_ioctl()` is an ENOTTY stub, because the ioctl translation unit is not in the compile set | Nothing in the compile or link set calls it; the stub keeps the interface path linked instead of relying on dead-stripping | M0-8b |
| D-5 | `osal/cmsis2/osal_cmsis2.c` | `tsleep`/`wakeup` use a bounded table of 8 identified waits; `softint_establish`/`softint_schedule` are inert | No consumer in the current compile set (drivers are the consumers); the driver round replaces both with the port worker and records it | M0-8b |
| D-6 | `osal/cmsis2/osal_cmsis2.c` | `cprng_fast()` is a software generator seeded with a constant state | No entropy source on the host; the target round switches to the platform source | M0-8b |
| D-7 | `osal/cmsis2/osal_cmsis2.c` | `panic()` prints and aborts (host semantics) | The imported code expects the kernel to stop; a target build replaces this with a reset policy | M0-8b |
| D-8 | `compat/prelude.h` | Replaces the component's `port_config*.h` as the forced include (NetBSD-world switches, missing vocabulary, host coexistence) | One mechanism, one copy: the component's harness config served the embox/board builds and required a private host preemption hack in this repository | M0-8b |
| D-9 | `compat/sys/*.h` | Machinery headers replaced by thin ones of the same names (the component's copies are removed by the trim) | The port owns the OS service face so a single CMSIS-RTOS2 implementation can serve every backend | M0-8b |
| D-10 | `compat/sys/cdefs.h`, `compat/sys/errno.h` | Port-owned forwarding shims over the host/target header (`include_next`), replacing the component's copies | The component's copies shadowed the C library's own headers; on a host that costs the library its compiler glue (errno.h needs `__THROW`) and on a newlib target it hides the real errno values. The port keeps the NetBSD vocabulary on top | M0-8b |
| D-11 | `compat/machine/ansi.h`, `compat/sys/null.h` | Empty/tiny shims for the target headers GCC's `<stddef.h>` and the component's `sys/queue.h` ask for once `__NetBSD__` is defined | Those headers are target-only; the shims let the compiler and the libc keep owning the type definitions | M0-8b |
| D-12 | `compat/prelude.h` | Loads `<inttypes.h>`, `<stdarg.h>`, `<stddef.h>`, `<sys/time.h>` and `<sys/sysctl.h>` before the NetBSD switch, and declares `panic()` for every unit | The imported sources use PRIu64, va_list, `struct timeval` and `sysctllog` without including the headers; the component's `port_config*.h` carried the same face | M0-8b |
| D-13 | `compat/sys/cdefs.h` | `__link_set_foreach` expands to a loop that never runs (the component's `cdefs.h` left it empty, `port_config.h` used the same loop) | Keeps the loop variable initialised and the registration body out of the object: the net80211 crypto registration is retired on the ports | M0-8b |
| D-14 | `compat/sys/once.h` | `ONCE_DECL` zero-initialises the control | The single-context flag must start unset; an uninitialised file-scope flag is also a compile warning | M0-8b |

Rules: the port stays English and dependency-light (CMSIS-RTOS2, the C library
for the host backend, the project's own headers). A change that other consumers
of the component could want belongs upstream as a patch
(`components/net80211/patches/`), not here.
