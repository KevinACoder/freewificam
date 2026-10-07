/*
 * @file
 * @brief Compiler glue for the NetBSD-imported sources.
 */

#ifndef _COMPAT_SYS_CDEFS_H_
#define _COMPAT_SYS_CDEFS_H_

#define __CONCAT(a, b) a##b
#define __STRING(x) #x
#define __CONCAT3(a, b, c) a##b##c

#define __unused __attribute__((__unused__))
#define __used __attribute__((__used__))
#define __packed __attribute__((__packed__))
#define __aligned(x) __attribute__((__aligned__(x)))
#define __noinline __attribute__((__noinline__))
#define __constfunc __attribute__((__const__))
#define __dead __attribute__((__noreturn__))
#define __pure __attribute__((__pure__))
#define __printflike(fmtarg, firstvararg) \
	__attribute__((__format__(__printf__, fmtarg, firstvararg)))
#define __scanlike(fmtarg, firstvararg) \
	__attribute__((__format__(__scanf__, fmtarg, firstvararg)))

#define __predict_true(exp) __builtin_expect((exp) != 0, 1)
#define __predict_false(exp) __builtin_expect((exp) != 0, 0)

#define __BEGIN_DECLS
#define __END_DECLS

#define __COPYRIGHT(x)
#define __RCSID(x)
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

#define __KERNEL_RCSID(n, x)
#define __SCCSID(x)

/* The imported net80211 crypto modules self-register through the
 * NetBSD link_set mechanism. There is no equivalent on the ports, and
 * the drivers never call the registration functions; the ciphers stay
 * unregistered (scan mode only needs the built-in NONE cipher). */
#ifndef __link_set_add_text
#define __link_set_add_text(set, sym)
#define __link_set_add_rodata(set, sym)
#define __link_set_decl(set, ptype)
#define __link_set_foreach(pvar, set)
#endif

/* bitfield helpers from NetBSD sys/types.h */
#ifndef __BIT
#define __BIT(n) ((uintmax_t)1 << (n))
#define SET(t, f) ((t) |= (f))
#define CLR(t, f) ((t) &= ~(f))
#define ISSET(t, f) ((t) & (f))
#endif

#ifndef __BITS
#define __BITS(hi, lo) ((UINT64_MAX >> (63 - (hi))) & (UINT64_MAX << (lo)))
#endif

#endif /* _COMPAT_SYS_CDEFS_H_ */
