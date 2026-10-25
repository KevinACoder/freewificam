/*
 * @file
 * @brief Forced-include preamble for the BSD-world translation units.
 *
 * Loaded with -include by the contract build (and by any consumer compiling the
 * imported net80211 sources) before the first source line. It is the single
 * place that carries:
 *   - the NetBSD-world switches the imported sources select their code paths
 *     with (__NetBSD__, the sysctl compile-out),
 *   - the host-coexistence step: the host libc has more headers than the target
 *     the component was built for, so its own <sys/cdefs.h> must win for its
 *     internals while the component's compat copy still provides the NetBSD
 *     vocabulary (the two names both define are dropped first, so the compat
 *     copy defines them exactly once and no redefinition warning is emitted),
 *   - the small NetBSD vocabulary the imported sources take from the kernel
 *     cdefs/systm that the kept compat headers do not carry.
 *
 * @design port-owned shadow preamble: the same mechanism the component's
 * port_config*.h used (a forced include owning the guard claims and the missing
 * vocabulary), re-derived here as the port's own single copy so the component's
 * harness config could retire. One mechanism, one implementation.
 */

#ifndef _WLAN_NET80211_PRELUDE_H_
#define _WLAN_NET80211_PRELUDE_H_

/* Host coexistence, step 1: load the compile-glue header. This resolves to the
 * port's own copy (components/net80211/ports/compat/sys/cdefs.h), which pulls the
 * host/target one through include_next and then adds the NetBSD vocabulary, so
 * the C library's own headers keep the glue they expect. */
#include <sys/cdefs.h>

/* Step 2: load the host's <stddef.h> before the NetBSD switch below. The host
 * compiler's stddef.h has a __NetBSD__ branch (including <machine/ansi.h>, see
 * the port's machine/ansi.h shim); loading it here means the common path is
 * processed once, before the switch. The fixed-width and variadic families come
 * from the same "host owns it" rule: the imported sources use them (PRIu64,
 * va_list) without including the headers themselves. */
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>

/* The receive path keeps one file-scope `struct timeval` interval
 * constant and includes no time header itself; the host libc or the target libc
 * owns the type. Also before the switch, for the same reason as step 2. */
#include <sys/time.h>

/* The sysctl vocabulary: ieee80211_netbsd.h declares entry points naming
 * sysctllog without including the header, and the component's port_config
 * carried that type for every translation unit this prelude keeps that face). */
#include <sys/sysctl.h>

/* The NetBSD code paths of the imported sources. Everything that must see the
 * host's own headers is loaded above. */
#ifndef __NetBSD__
#define __NetBSD__ 1
#endif

/* No sysctl store on a port: the net80211 sysctl configuration tree compiles
 * out (the kept vocabulary header supplies empty stubs either way). */
#ifndef IEEE80211_PORT_NO_SYSCTL
#define IEEE80211_PORT_NO_SYSCTL 1
#endif

/* NetBSD vocabulary the imported sources expect from the kernel headers.
 * Same semantics as the component's port_config*.h, which this prelude
 * replaces. */
void panic(const char *fmt, ...) __printflike(1, 2);

#ifndef __FBSDID
#define __FBSDID(x)
#endif
#ifndef __UNCONST
#define __UNCONST(a) ((void *)(unsigned long)(const void *)(a))
#endif
#ifndef __arraycount
#define __arraycount(x) (sizeof((x)) / sizeof((x)[0]))
#endif
#ifndef __USE
#define __USE(a) ((void) (a))
#endif
#ifndef __CTASSERT
#define __CTASSERT(x) _Static_assert((x), #x)
#endif
#ifndef CTASSERT
#define CTASSERT(x) _Static_assert((x), #x)
#endif
/* The imported code probes invariants with KASSERT; the ports compile the
 * condition out (it must stay unevaluated). */
#ifndef KASSERT
#define KASSERT(cond) ((void) sizeof((cond) ? 1 : 0))
#endif
#ifndef KASSERTMSG
#define KASSERTMSG(cond, fmt, ...) ((void) sizeof((cond) ? 1 : 0))
#endif

#endif /* _WLAN_NET80211_PRELUDE_H_ */
