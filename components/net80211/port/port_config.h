/*
 * @file
 * @brief Forced-include compatibility preamble for the imported code.
 *
 * The embox build puts its own include tree ahead of the compat shadow
 * paths, so everything this project needs from <sys/cdefs.h> and
 * <sys/types.h> is defined here instead; the file is force-included
 * (-include) for every translation unit of the library. All additions
 * are guarded so they compose with whatever the host already defines.
 */

#ifndef _NET80211_PORT_CONFIG_H_
#define _NET80211_PORT_CONFIG_H_

#include <stdint.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdbool.h>

/* ---- NetBSD typedefs (identical repeated typedefs are legal C11) -- */
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;

typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;

#ifndef ticks_t
typedef unsigned long ticks_t;
#endif

/* ---- compiler attributes ------------------------------------------ */
#ifndef __CONCAT
#define __CONCAT(a, b) a##b
#endif
#define __STRING(x) #x
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
#define __noinline __attribute__((__noinline__))
#ifndef __dead
#define __dead __attribute__((__noreturn__))
#endif
#define __constfunc __attribute__((__const__))
#define __pure __attribute__((__pure__))
#ifndef __printflike
#define __printflike(fmtarg, firstvararg) \
	__attribute__((__format__(__printf__, fmtarg, firstvararg)))
#endif
#define __scanlike(fmtarg, firstvararg) \
	__attribute__((__format__(__scanf__, fmtarg, firstvararg)))
#ifndef __predict_true
#define __predict_true(exp) __builtin_expect((exp) != 0, 1)
#endif
#ifndef __predict_false
#define __predict_false(exp) __builtin_expect((exp) != 0, 0)
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

#define __COPYRIGHT(x)
#define __RCSID(x)
#define __KERNEL_RCSID(n, x)
#define __SCCSID(x)
#define __FBSDID(x)

#ifndef __UNCONST
#define __UNCONST(a) ((void *)(unsigned long)(const void *)(a))
#endif
#ifndef __CASTV
#define __CASTV(a) ((void *)(unsigned long)(const void *)(a))
#endif

#ifndef __arraycount
#define __arraycount(x) (sizeof((x)) / sizeof((x)[0]))
#ifndef __USE
#define __USE(a) ((void) (a))
#endif
#endif

/* time structs come from the host: the embox posix compat headers
 * define timespec/timeval for the imported USB headers */
#if defined(__EMBOX__)
#include <time.h>
#include <sys/time.h>
#endif

#ifndef __CTASSERT
#define __CTASSERT(x) _Static_assert((x), #x)
#endif
#ifndef CTASSERT
#define CTASSERT(x) _Static_assert((x), #x)
#endif
#ifndef KASSERT
#define KASSERT(cond) ((void) sizeof((cond) ? 1 : 0))
#endif
#ifndef KASSERTMSG
#define KASSERTMSG(cond, fmt, ...) ((void) sizeof((cond) ? 1 : 0))
#endif
#define __LINKER_ARRAY_SET(x)

/* The imported net80211 crypto modules self-register through the
 * NetBSD link_set mechanism; there is no equivalent here and the
 * drivers never call the registration functions (scan mode only needs
 * the built-in NONE cipher). */
#define __link_set_add_text(set, sym)
#define __link_set_add_rodata(set, sym)
#define __link_set_decl(set, ptype)
#define __link_set_foreach(pvar, set) for ((pvar) = NULL; (pvar) != NULL; )

#endif /* _NET80211_PORT_CONFIG_H_ */
