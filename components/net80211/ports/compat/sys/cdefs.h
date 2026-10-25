/*
 * @file
 * @brief Compile-glue face for the imported sources, on top of the host's.
 *
 * The imported NetBSD sources select their kernel paths with __NetBSD__ and use
 * the NetBSD spelling of attributes and helpers. This header loads the
 * compiler/libc <sys/cdefs.h> first (through include_next, so the host or target
 * copy keeps owning __THROW, __BEGIN_DECLS, the fortified declarations and
 * everything else its own headers expect) and then adds the NetBSD vocabulary
 * on top with guards, so nothing is defined twice.
 *
 * It is the replacement for the component's compat copy of the same name: the
 * component's file defined its macros unguarded and had no include_next, which on
 * a host makes the C library's own headers (errno.h and friends) lose their
 * compiler glue. The vocabulary below is the component's list, re-derived.
 *
 * @design port-owned shadow header: same mechanism the component's compat header
 * used (claim the name, supply the NetBSD macros), with the host/target header
 * kept alive under it. The component's copy retires with the trim (see the
 * component's DIVERGENCES ledger).
 */

#ifndef _WLAN_SYS_CDEFS_H_
#define _WLAN_SYS_CDEFS_H_

#include_next <sys/cdefs.h>

#ifndef __CONCAT
#define __CONCAT(a, b) a##b
#endif
#ifndef __STRING
#define __STRING(x) #x
#endif
#ifndef __CONCAT3
#define __CONCAT3(a, b, c) a##b##c
#endif

#ifndef __unused
#define __unused __attribute__((__unused__))
#endif
#ifndef __used
#define __used __attribute__((__used__))
#endif
#ifndef __packed
#define __packed __attribute__((__packed__))
#endif
#ifndef __aligned
#define __aligned(x) __attribute__((__aligned__(x)))
#endif
#ifndef __noinline
#define __noinline __attribute__((__noinline__))
#endif
#ifndef __constfunc
#define __constfunc __attribute__((__const__))
#endif
#ifndef __dead
#define __dead __attribute__((__noreturn__))
#endif
#ifndef __pure
#define __pure __attribute__((__pure__))
#endif
#ifndef __printflike
#define __printflike(fmtarg, firstvararg) \
	__attribute__((__format__(__printf__, fmtarg, firstvararg)))
#endif
#ifndef __scanlike
#define __scanlike(fmtarg, firstvararg) \
	__attribute__((__format__(__scanf__, fmtarg, firstvararg)))
#endif
#ifndef __predict_true
#define __predict_true(exp) __builtin_expect((exp) != 0, 1)
#endif
#ifndef __predict_false
#define __predict_false(exp) __builtin_expect((exp) != 0, 0)
#endif

#ifndef __COPYRIGHT
#define __COPYRIGHT(x)
#endif
#ifndef __RCSID
#define __RCSID(x)
#endif
#ifndef __KERNEL_RCSID
#define __KERNEL_RCSID(n, x)
#endif
#ifndef __SCCSID
#define __SCCSID(x)
#endif
#ifndef __FBSDID
#define __FBSDID(x)
#endif

#ifndef __SHIFTIN
#define __SHIFTIN(v, mask) (((uintmax_t)(v) << __builtin_ctzll(mask)) & (mask))
#endif
#ifndef __SHIFTOUT
#define __SHIFTOUT(v, mask) (((uintmax_t)(v) & (mask)) >> __builtin_ctzll(mask))
#endif
#ifndef __GNUC_PREREQ__
#define __GNUC_PREREQ__(maj, min) \
	((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#endif

/* The imported net80211 crypto modules self-register through the NetBSD
 * link_set mechanism. There is no equivalent on the ports and no driver calls
 * the registration functions (scan mode needs the built-in NONE cipher), so the
 * mechanism compiles out here: the declaring macro expands to a loop that never
 * runs, which keeps the loop variable initialised and the body out of the
 * object. */
#ifndef __link_set_add_text
#define __link_set_add_text(set, sym)
#define __link_set_add_rodata(set, sym)
#define __link_set_decl(set, ptype)
#define __link_set_foreach(pvar, set) \
	for ((pvar) = NULL; (pvar) != NULL; )
#endif

#ifndef __BIT
#define __BIT(n) ((uintmax_t)1 << (n))
#endif
#ifndef __BITS
#define __BITS(hi, lo) ((UINT64_MAX >> (63 - (hi))) & (UINT64_MAX << (lo)))
#endif
#ifndef SET
#define SET(t, f) ((t) |= (f))
#define CLR(t, f) ((t) &= ~(f))
#define ISSET(t, f) ((t) & (f))
#endif

#endif /* _WLAN_SYS_CDEFS_H_ */
