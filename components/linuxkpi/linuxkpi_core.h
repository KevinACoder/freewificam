/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * linuxkpi core (driver-facing) - the Linux kernel API surface the
 * verbatim drivers compile against, and NOTHING ELSE: this header is
 * deliberately self-contained (no port headers, no kernel shadows) so a
 * driver translation unit sees only the Linux world.
 *
 * Every stateful operation is a plain function call into the linuxkpi
 * implementation (linuxkpi_core.c, which compiles in the port's OSAL
 * world); the lock/timer/completion types are fixed-size opaque blobs so
 * the drivers can embed them by value without seeing the OSAL.
 *
 * The contents are first-party (project-authored), shaped on the Linux
 * semantics; the mapping decisions that came out of the board-proven
 * bring-up rounds are carried over unchanged (in particular: the driver
 * code only runs at passive level - spinlocks are passive mutexes and
 * every USB completion is deferred to the lkpi workqueue before it can
 * touch one; GFP flags only select wait-or-fail; allocations carry a
 * size header because the heap free path wants the size).
 */

#ifndef LINUXKPI_CORE_H
#define LINUXKPI_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

/*
 * The console level vocabulary is the vendored engine's (components/ulog). The
 * component borrows only the type and the constants, so the line sink can carry
 * a severity without the project growing a second set of level names.
 */
#include "ulog.h"

/* -std=c11 has no typeof keyword; the GNU spelling always exists. */
#ifndef typeof
#define	typeof(x)	__typeof__(x)
/* [LNX] include/linux/random.h:39 */
void get_random_bytes(void *buf, size_t len);

#endif

/* IS_ENABLED/IS_REACHABLE over the kbuild convention: a CONFIG_ symbol is
 * "defined" iff the build passed -D for it (kernel.h's placeholder trick) */
#define	___is_defined(val)	____is_defined(__ARG_PLACEHOLDER_##val)
#define	__is_defined(val)	___is_defined(val)
#define	__ARG_PLACEHOLDER_1	0,
#define	____is_defined(arg1_or_junk)	__take_second_arg(arg1_or_junk 1, 0)
#define	__take_second_arg(first, second, ...)	second
#ifndef IS_ENABLED
#define	IS_ENABLED(option)	__is_defined(option)
#endif
#ifndef IS_REACHABLE
#define	IS_REACHABLE(option)	__is_defined(option)
#endif
#ifndef IS_BUILTIN
#define	IS_BUILTIN(option)	__is_defined(option)
#endif

/* the drivers are one module here; KBUILD_MODNAME shows up in ids */
#ifndef KBUILD_MODNAME
#define	KBUILD_MODNAME	"rtw88"
#endif
typedef unsigned long	kernel_ulong_t;

/* the image's own libc: printf lives in minilibc (routed through the
 * locked console sink by the BSD world), vsnprintf in the vendored
 * mpaland/printf.  Declared here so the driver world never pulls the
 * host <stdio.h>. */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...);

/* ------------------------------------------------------------------ */
/* basic types                                                         */
/* ------------------------------------------------------------------ */

/*
 * The interface's widths, and specifically its 64-bit spelling: [LNX]
 * include/uapi/asm-generic/int-ll64.h:33-34 makes __s64/__u64 `long long` /
 * `unsigned long long` on every architecture (the file's own comment: "int-ll64 is
 * used everywhere now"), and arm64 reaches it through
 * include/uapi/asm-generic/types.h. It is not interchangeable with `long` even
 * where both are 64 bits: a driver's format strings are written against the
 * interface's spelling, so `%lld`/`%llu`/`%llx` match here only if the type is the
 * same one - and with `long` the gate reports the driver's own correct format
 * strings as mismatches while hiding the ones the interface would reject.
 */
typedef uint8_t			u8;
typedef uint16_t		u16;
typedef uint32_t		u32;
typedef unsigned long long	u64;
typedef int8_t			s8;
typedef int16_t			s16;
typedef int32_t			s32;
typedef long long		s64;

typedef uint8_t			__u8;
typedef uint16_t		__u16;
typedef uint32_t		__u32;
typedef unsigned long long	__u64;
typedef int8_t			__s8;
typedef int16_t			__s16;
typedef int32_t			__s32;
typedef long long		__s64;

typedef uint16_t		__le16;
typedef uint32_t		__le32;
typedef unsigned long long	__le64;
typedef uint16_t		__be16;
typedef uint32_t		__be32;
typedef unsigned long long	__be64;
/* the checksum types the wire-header layouts name (uapi/linux/types.h);
 * __bitwise is a sparse annotation and a no-op for the compiler */
typedef uint16_t		__sum16;
typedef uint32_t		__wsum;

typedef unsigned long		ulong;
typedef unsigned int		uint;
typedef int64_t			loff_t;
typedef int64_t			ssize_t;	/* sysfs show/store signatures */
typedef int64_t			time64_t;	/* the second count of a timespec64 */
typedef uint64_t		dma_addr_t;
typedef unsigned int		gfp_t;

#define	__user
#define	__iomem
#define	__force
/* the sparse bitwise-endianness marker: a no-op for the compiler, but it sits
 * between the base type and the declarator (`typedef unsigned int __bitwise
 * foo_t;`), so without the define the declaration fails to parse */
#define	__bitwise
/* sparse address-space markers: no-ops for the compiler, but rtw89's
 * rcu-protected pointers (`struct foo __rcu *`) put the annotation in
 * the middle of declarations - without the define the whole struct
 * fails to parse (GCC error-recovery then poisons every member access) */
#define	__rcu
#define	__iftd
#define	__percpu
#define	__must_check
#define	__init
#define	__exit
#define	__acquire(x)
#define	__release(x)
#define	__acquires(x)
#define	__releases(x)
#define	__printf(a, b)		__attribute__((__format__(printf, a, b)))
/* count bound annotation (Linux 6.5+): compile-time documentation only */
#define	__counted_by(member)
#ifndef __always_inline
#define	__always_inline		inline __attribute__((__always_inline__))
#endif
#ifndef __noinline
#define	__noinline		__attribute__((__noinline__))
#endif
#ifndef __weak
#define	__weak			__attribute__((__weak__))
#endif
#ifndef __visible
#define	__visible		__attribute__((__visibility__("default")))
#endif
#ifndef __aligned
#define	__aligned(x)		__attribute__((__aligned__(x)))
#endif
/* [LNX] include/linux/cache.h: the cacheline-alignment markers. This port runs a
 * single CPU, so the SMP-specific marker is the interface's own empty shape. */
#ifndef ____cacheline_aligned
#define	____cacheline_aligned		__attribute__((__aligned__(64)))
#endif
#ifndef ____cacheline_aligned_in_smp
#define	____cacheline_aligned_in_smp
#endif
#ifndef __cacheline_aligned
#define	__cacheline_aligned		____cacheline_aligned
#endif
/* [LNX] asm-generic/cache.h:11: the cacheline size (aarch64's L1_CACHE_SHIFT is 6). */
#ifndef SMP_CACHE_BYTES
#define	SMP_CACHE_BYTES			(1 << 6)
#endif
#ifndef __packed
#define	__packed		__attribute__((__packed__))
#endif
#ifndef __section
#define	__section(x)		__attribute__((__section__(x)))
#endif
#ifndef __read_mostly
#define	__read_mostly
#endif
#ifndef __used
#define	__used			__attribute__((__used__))
#endif
#ifndef __unused
#define	__unused		__attribute__((__unused__))
#endif
/* [LNX] include/linux/compiler_attributes.h:214: the fall-through marker a switch case
 * states; the attribute arm is the one this face's compiler class supports (the
 * do-while arm is for compilers without the statement attribute and would leave
 * -Wimplicit-fallthrough firing) */
#ifndef fallthrough
#define	fallthrough		__attribute__((__fallthrough__))
#endif

#define	U8_MAX			0xffU
#define	U16_MAX			0xffffU
#define	U32_MAX			0xffffffffU
#define	U64_MAX			0xffffffffffffffffULL
#define	UINT_MAX		((unsigned int)~0U)
#define	INT_MAX			0x7fffffff
#define	ULONG_MAX		((unsigned long)~0UL)
#define	LONG_MAX		((long)(~0UL >> 1))
#define	S8_MAX			0x7f
#define	S8_MIN			(-0x80)
#define	S16_MAX			0x7fff
#define	S16_MIN			(-0x8000)
#define	S32_MIN			(-0x80000000)
#define	S64_MIN			(-0x7fffffffffffffffLL)
#define	U8_MIN			0
#define	U16_MIN			0
#define	U32_MIN			0

/* Linux's private ENOTSUPP: the value is the kernel's own (upstream
 * include/linux/errno.h:27 states 524), not a number of this project's
 * choosing - a driver's -ENOTSUPP must compare equal to the kernel's. */
#define	ENOTSUPP		524

/* byte-order convenience names (host order is LE here) */
#define	htons(x)		cpu_to_be16(x)
#define	ntohs(x)		be16_to_cpu(x)
#define	htonl(x)		cpu_to_be32(x)
#define	ntohl(x)		be32_to_cpu(x)

/* the 32-bit PRNG spelling; random.h re-defines it for TUs that take
 * the include, this is the always-visible fallback */
#ifndef get_random_u32
unsigned int lkpi_random_u32(void);
#define	get_random_u32()	lkpi_random_u32()
#endif

/*
 * [LNX] include/linux/random.h:56-59 (the bounded draw) and 110-115 (the inclusive
 * form). The interface's bounded draw returns a value in [0, ceil) with uniform
 * distribution; this port's source is the core's xorshift stream, so the bounded
 * draw is the interface's rejection rule (discard a draw in the top partial band,
 * then take the remainder) and the inclusive form is the interface's own
 * `floor + get_random_u32_below(ceil - floor + 1)`. The interface's
 * constant-folding fast path is an optimisation of the same answer and is not
 * carried.
 */
static __always_inline __unused u32
get_random_u32_below(u32 ceil)
{
	u32 reject, r;

	if (ceil <= 1)
		return 0;
	/* (2^32 mod ceil) values at the bottom of the range would bias the
	 * remainder; they are the ones rejected here. */
	reject = (u32)(0u - ceil) % ceil;
	do {
		r = (u32)get_random_u32();
	} while (r < reject);
	return r % ceil;
}

static __always_inline __unused u32
get_random_u32_inclusive(u32 floor, u32 ceil)
{
	return floor + get_random_u32_below(ceil - floor + 1);
}

#ifndef static_assert
#define	static_assert		_Static_assert
#endif

#if __has_attribute(__nonstring__)
#define	__nonstring		__attribute__((__nonstring__))
#else
#define	__nonstring
#endif

#ifndef likely
#define	likely(x)		__builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define	unlikely(x)		__builtin_expect(!!(x), 0)
#endif

/* ------------------------------------------------------------------ */
/* struct device (the Linux shape: a name carrier + driver data)        */
/* ------------------------------------------------------------------ */

struct attribute_group;

/* [LNX] include/linux/pm.h: struct dev_pm_info. The interface's record is wide (it carries
 * the whole runtime-PM state machine); this face states the one field the compiled set
 * reads, `is_prepared`, which a cfg80211 consumer asks before it suspends a device. The
 * rest arrives with a consumer that names it. */
struct dev_pm_info {
	bool		is_prepared;
};

struct bus_type;

/* [LNX] include/linux/kobject.h: the sysfs object a device embeds. The compiled
 * set takes its address and hands it to the uevent call; nothing here walks it. */
struct kobject {
	const char	*name;
};

struct device {
	const char	*name;
	void		*driver_data;
	void		*platform_data;
	struct device	*parent;
	/* [LNX] include/linux/device.h: the sysfs object the device embeds. */
	struct kobject	kobj;
	/* rtw89's usb.c assigns the sysfs attribute groups here (the
	 * groups are never walked on this image; see linux/sysfs.h) */
	const struct attribute_group **groups;
	/* the runtime-PM state a cfg80211 consumer reads; see dev_pm_info above */
	struct dev_pm_info power;
	/* [LNX] include/linux/device.h: the bus this device sits on. The compiled
	 * set reads the bus name off a device (a firmware-dump line); nothing on
	 * this port assigns the record, so the pointer is carried for the read. */
	struct bus_type	*bus;
};

#define	dev_set_drvdata(dev, data)	do { (dev)->driver_data = (data); } while (0)
#define	dev_get_drvdata(dev)		((dev)->driver_data)

/* ------------------------------------------------------------------ */
/* constants and small helpers                                         */
/* ------------------------------------------------------------------ */

#define	BITS_PER_LONG		(8 * sizeof(unsigned long))
#define	BITS_PER_BYTE		8
#ifndef PAGE_SIZE
/* [LNX] arch/arm64/include/asm/page-def.h: `#define PAGE_SIZE (_AC(1, UL) <<
 * PAGE_SHIFT)` - an unsigned long, not a bare int. The width shows through
 * arithmetic a driver logs with %lu/%lx (pages * PAGE_SIZE), which the
 * interface's own build accepts. */
#define	PAGE_SIZE		(1UL << 12)
#endif
#ifndef PAGE_SHIFT
#define	PAGE_SHIFT		12
#endif
#ifndef PAGE_MASK
#define	PAGE_MASK		(~(PAGE_SIZE - 1))
#endif

#ifndef BIT
#define	BIT(n)			(1UL << (n))
#endif
#define	BIT_ULL(n)		(1ULL << (n))
#define	min3(a, b, c)		min(min((a), (b)), (c))
#define	max3(a, b, c)		max(max((a), (b)), (c))
#define	offsetofend(t, m)	(offsetof(t, m) + sizeof(((t *)0)->m))
#ifndef GENMASK
#define	GENMASK(h, l)		(((~0UL) << (l)) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#endif
#define	GENMASK_ULL(h, l)	(((~0ULL) << (l)) & (~0ULL >> (64 - 1 - (h))))

#define	ARRAY_SIZE(a)		(sizeof(a) / sizeof((a)[0]))
#define	DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#define	DIV_ROUND_UP_ULL(n, d)	DIV_ROUND_UP(n, d)
#define	DIV_ROUND_DOWN_ULL(ll, d)	((ll) / (d))
#define	DIV_U64_ROUND_UP(ll, d)	DIV_ROUND_UP_ULL((u64)(ll), (u32)(d))
static __always_inline __unused u64
roundup_u64(u64 x, u32 y)
{
	return DIV_U64_ROUND_UP(x, y) * y;
}
static __always_inline __unused u64
div_u64_rem(u64 dividend, u32 divisor, u32 *remainder)
{
	*remainder = (u32)(dividend % divisor);
	return dividend / divisor;
}
static __always_inline __unused u64
div_u64(u64 dividend, u32 divisor)
{
	u32 rem;

	return div_u64_rem(dividend, divisor, &rem);
}

/*
 * The 64-bit-by-64-bit divide family. The interface states these as inlines in
 * linux/math64.h (include/linux/math64.h:81-84 and 159-163); they are carried in
 * the core's divide family, next to div_u64()/div_u64_rem() above, because the
 * compiled set reaches the divide vocabulary through the core contract rather than
 * through linux/math64.h (the interface's own BITS_PER_LONG == 64 arm, which is the
 * arm this port compiles).
 */
/* [LNX] include/linux/math64.h:81-84 */
static __always_inline __unused u64
div64_u64(u64 dividend, u64 divisor)
{
	return dividend / divisor;
}

/* [LNX] include/linux/math64.h:159-163: the interface computes the quotient
 * through div_s64_rem() and discards the remainder, so the result is the plain
 * signed divide; the divisor is the interface's 32-bit s32. */
static __always_inline __unused s64
div_s64(s64 dividend, s32 divisor)
{
	return dividend / divisor;
}

/* Linux's overflow-checked size arithmetic, without the checked parts
 * (the counts here are chip-table lengths, never near overflow); the
 * member expressions ride sizeof, so they are never evaluated - which
 * is what lets struct_size(p, ...) in a p-assignment statement work */
#define	struct_size(p, member, count)	\
	(sizeof(*(p)) + (count) * sizeof((p)->member[0]))
#define	struct_size_t(type, member, count)	\
	(sizeof(type) + (count) * sizeof(((type *)0)->member[0]))
#define	flex_array_size(p, member, count)	\
	((count) * sizeof((p)->member[0]))
#define	DIV_ROUND_CLOSEST(x, y)	((((x) < 0) ^ ((y) < 0)) ? \
				  (((x) - ((y) / 2)) / (y)) : \
				  (((x) + ((y) / 2)) / (y)))
#define	round_up(x, y)		((((x) - 1) | ((y) - 1)) + 1)
#define	round_down(x, y)	((x) & ~((y) - 1))
#define	PAGE_ALIGN(x)		round_up(x, PAGE_SIZE)
#ifndef ALIGN
#define	ALIGN(x, a)		round_up(x, a)
#endif
#define	IS_ALIGNED(x, a)	(((x) & ((a) - 1)) == 0)
#define	FIELD_SIZEOF(t, f)	(sizeof(((t *)0)->f))
/* [LNX] stddef.h's modern FIELD_SIZEOF; rtw89's pci.h BUILD_BUG_ON uses
 * it - invisible while BUILD_BUG_ON was a no-op (the argument was never
 * even macro-expanded), exposed the day the trap went real */
#define	sizeof_field(type, member)	(sizeof(((type *)0)->member))

#define	min(a, b)		((a) < (b) ? (a) : (b))
#define	max(a, b)		((a) > (b) ? (a) : (b))
#define	MIN(a, b)		min((a), (b))
#define	MAX(a, b)		max((a), (b))
#define	min_t(t, a, b)		((t)min((a), (b)))
#define	max_t(t, a, b)		((t)max((a), (b)))
#define	clamp_val(v, lo, hi)	max(min((v), (hi)), (lo))
#define	clamp_t(t, v, lo, hi)	((t)clamp_val((v), (lo), (hi)))
#define	clamp(v, lo, hi)	clamp_val((v), (lo), (hi))
#define	umin(x, y)		min((x) + 0u, (y) + 0u)
#define	swap(a, b)		do { typeof(a) __t = (a); (a) = (b); (b) = __t; } while (0)
#define	abs_diff(a, b)		((a) > (b) ? (a) - (b) : (b) - (a))

/* Linux's container_of; the drivers' version includes array members. */
#define	container_of(ptr, type, member)	\
	((type *)(void *)((char *)(void *)(ptr) - offsetof(type, member)))
#define	container_of_const(ptr, type, member)				\
	_Generic(ptr,							\
		const typeof(*(ptr)) *: ((const type *)container_of(ptr, type, member)), \
		default: ((type *)container_of(ptr, type, member)))

#define	VERIFY_OCTAL_PERMISSIONS(x)	(x)

/* linux/typecheck.h: compile-time pointer-type assertion, value 1 */
#define	typecheck(type, x)				\
({							\
	type __dummy_tc;				\
	typeof(x) __dummy2_tc;				\
	(void)(&__dummy_tc == &__dummy2_tc);		\
	1;						\
})
#define	typecheck_fn(type, function)			\
({							\
	typeof(type) __tmp = function;			\
	(void)__tmp;					\
})

/* Linux's fls() is the 1-based position of the highest set bit. */
static __always_inline __unused int
lkpi_fls32(uint32_t x)
{

	return x == 0 ? 0 : 32 - __builtin_clz(x);
}

/* Linux's ffs() is the 1-based position of the lowest set bit. */
static __always_inline __unused int
lkpi_ffs32(uint32_t x)
{

	return x == 0 ? 0 : __builtin_ctz(x) + 1;
}

static __always_inline __unused s32
sign_extend32(u32 value, int index)
{
	u8 shift = 31 - index;

	return (s32)(value << shift) >> shift;
}

static __always_inline __unused s64
sign_extend64(u64 value, int index)
{
	u8 shift = 63 - index;

	return (s64)(value << shift) >> shift;
}

#define	lower_32_bits(n)	((u32)((n) & 0xffffffff))
#define	upper_32_bits(n)	((u32)(((n) >> 16) >> 16))
#ifndef ALIGN_DOWN
#define	ALIGN_DOWN(x, a)	((x) & ~((__typeof(x))((a) - 1)))
#endif

#define	fls(x)			lkpi_fls32((uint32_t)(x))
#define	ffs(x)			lkpi_ffs32((uint32_t)(x))

static __always_inline __unused unsigned int
lkpi_hweight8(u8 w)
{
	unsigned int res = w - ((w >> 1) & 0x55);

	res = (res & 0x33) + ((res >> 2) & 0x33);
	return (res + (res >> 4)) & 0x0f;
}

static __always_inline __unused unsigned int
lkpi_hweight16(u16 w)
{
	unsigned int res = w - ((w >> 1) & 0x5555);

	res = (res & 0x3333) + ((res >> 2) & 0x3333);
	res = (res + (res >> 4)) & 0x0f0f;
	return (res + (res >> 8)) & 0x00ff;
}

static __always_inline __unused unsigned int
lkpi_hweight32(u32 w)
{
	/* SWAR: plain armv8-a has no POPCNT, and __builtin_popcount
	 * would emit a __popcountdi2 libgcc call on this freestanding
	 * build */
	w = w - ((w >> 1) & 0x55555555);
	w = (w & 0x33333333) + ((w >> 2) & 0x33333333);
	w = (w + (w >> 4)) & 0x0f0f0f0f;
	return (w * 0x01010101) >> 24;
}

static __always_inline __unused unsigned int
lkpi_hweight64(u64 w)
{
	return lkpi_hweight32((u32)w) + lkpi_hweight32((u32)(w >> 32));
}

/* hweight as the Linux macro shape: __builtin_constant_p lets a
 * constant argument fold to an ICE, which rtw89's static_assert over
 * hweight32(GENMASK(...)) requires */
/* the asm-generic/bitops/const_hweight.h form: pure boolean arithmetic,
 * an exact integer constant expression for static_assert (a helper
 * function - even __always_inline - is not) */
#define	__const_hweight8(w)		\
	((unsigned int)			\
	 ((!!((w) & (1ULL << 0))) +	\
	  (!!((w) & (1ULL << 1))) +	\
	  (!!((w) & (1ULL << 2))) +	\
	  (!!((w) & (1ULL << 3))) +	\
	  (!!((w) & (1ULL << 4))) +	\
	  (!!((w) & (1ULL << 5))) +	\
	  (!!((w) & (1ULL << 6))) +	\
	  (!!((w) & (1ULL << 7)))))
#define	__const_hweight16(w)	(__const_hweight8(w) + __const_hweight8((w) >> 8))
#define	__const_hweight32(w)	(__const_hweight16(w) + __const_hweight16((w) >> 16))
#define	__const_hweight64(w)	(__const_hweight32(w) + __const_hweight32((u64)(w) >> 32))
#define	hweight8(w)	(__builtin_constant_p(w) ? __const_hweight8(w) : lkpi_hweight8(w))
#define	hweight16(w)	(__builtin_constant_p(w) ? __const_hweight16(w) : lkpi_hweight16(w))
#define	hweight32(w)	(__builtin_constant_p(w) ? __const_hweight32(w) : lkpi_hweight32(w))
#define	hweight64(w)	(__builtin_constant_p(w) ? __const_hweight64(w) : lkpi_hweight64(w))
/* [LNX] include/linux/bitops.h: the upper-case spelling of the same family. The
 * interface carries both (HWEIGHTn is the "hardware popcount" name a driver uses
 * when it wants the count itself rather than the mask test); they fold to the same
 * helper here, so there is one mechanism under two names. */
#define	HWEIGHT8(w)	(__builtin_constant_p(w) ? __const_hweight8(w) : lkpi_hweight8(w))
#define	HWEIGHT16(w)	(__builtin_constant_p(w) ? __const_hweight16(w) : lkpi_hweight16(w))
#define	HWEIGHT32(w)	(__builtin_constant_p(w) ? __const_hweight32(w) : lkpi_hweight32(w))
#define	HWEIGHT64(w)	(__builtin_constant_p(w) ? __const_hweight64(w) : lkpi_hweight64(w))

/* [LNX] include/linux/bitops.h:94-97: `static __always_inline unsigned long
 * hweight_long(unsigned long w)` picks the width from sizeof(w); on this port
 * unsigned long is 64 bits, so it reaches the hweight64 body above. */
#define	hweight_long(w)							\
	((unsigned long)(sizeof(w) == 4 ? hweight32(w) : hweight64((u64)(w))))

static __always_inline __unused unsigned long
__ffs(unsigned long word)
{

	return word == 0 ? 0 : (unsigned long)__builtin_ctzl(word);
}

static __always_inline __unused unsigned long
__fls(unsigned long word)
{

	return word == 0 ? 0 : (unsigned long)(63 - __builtin_clzll(word));
}

/* [LNX] include/asm-generic/bitops/ffz.h:11: `#define ffz(x) __ffs(~(x))` - the
 * index of the first zero bit, which is the first set bit of the complement. */
#define	ffz(x)			__ffs(~(unsigned long)(x))

#define	do_div(n, base)		({ unsigned long __r = (n) % (base);	\
				   (n) = (n) / (base); __r; })

/* [LNX] include/linux/stringify.h:9-10: the two-level indirection, so an argument
 * that is itself a macro is expanded before it is stringified. */
#define	__stringify_1(x...)	#x
#define	__stringify(x...)	__stringify_1(x)

/* [LNX] include/linux/log2.h:193: `#define ilog2(n) __ilog2_u64(n)` with
 * `fls64(n) - 1` - the 0-based index of the HIGHEST set bit. The previous form
 * counted trailing zeros (__builtin_ctzll), which agrees with the floor log
 * only on pure powers of two - and get_order() feeds it `size - 1` on purpose
 * (the generic getorder.h arithmetic), so a 4 KiB receive buffer came back as
 * order -11: the board named it as `alloc_pages(order=4294967285)` and the
 * iwlwifi receive path's every buffer allocation was refused, which in turn
 * starved the firmware-ALIVE handshake. Exact for constants, as the interface
 * is, because both builtins fold. */
#define	ilog2(n)	((unsigned long)(63 - __builtin_clzll((unsigned long long)(n))))

/* [LNX] include/linux/math.h: the round-up-to-a-power-of-two helper. It is a
 * macro over the exact-for-constants ilog2 above, because a compiled consumer
 * names it inside an alignment attribute, where only a constant expression is
 * accepted. */
#define	__roundup_pow_of_two(n)	(1UL << (ilog2((n) - 1) + 1))
#define	roundup_pow_of_two(n)	__roundup_pow_of_two(n)

/*
 * [LNX] include/linux/log2.h:45-48: `static __always_inline bool
 * is_power_of_2(unsigned long n)` with the interface's own body `n - 1 < (n ^
 * (n - 1))`. Zero is not a power of two (n-1 wraps to ULONG_MAX, which is not
 * less than 1), which is the interface's contract.
 */
static __always_inline __unused bool
is_power_of_2(unsigned long n)
{
	return n - 1 < (n ^ (n - 1));
}

/* [LNX] include/linux/log2.h:65-69 and 191-195: the interface's round-down, whose
 * runtime arm is `1UL << (fls_long(n) - 1)` - the 0-based highest-set-bit index,
 * which is this port's __fls() above (fls_long(n) - 1 == __fls(n)). The constant
 * arm folds through the exact-for-constants ilog2 the round-up helper already
 * uses. */
#define	__rounddown_pow_of_two(n)	(1UL << __fls((unsigned long)(n)))
#define	rounddown_pow_of_two(n)						\
	(__builtin_constant_p(n) ? (1UL << ilog2(n)) : __rounddown_pow_of_two(n))

#ifndef READ_ONCE
#define	READ_ONCE(x)		(*(volatile typeof(x) *)&(x))
#endif
#ifndef WRITE_ONCE
#define	WRITE_ONCE(x, v)	do { *(volatile typeof(x) *)&(x) = (v); } while (0)
#endif

/*
 * [LNX] include/linux/compiler.h:85: `#define barrier() __asm__ __volatile__("": :
 * :"memory")` - the compiler-only barrier (no CPU instruction, but the compiler
 * may not reorder or merge accesses across it). It is the interface's own body,
 * carried as the interface's macro rather than as the extern declaration an earlier
 * revision of this header had, because the interface spells it as a macro and a
 * driver may name it in a context an out-of-line call would not survive.
 */
#ifndef barrier
#define	barrier()		__asm__ __volatile__("": : :"memory")
#endif
/* [LNX] include/asm-generic/barrier.h:113: the !CONFIG_SMP arm of smp_mb(), which
 * is `barrier()` - this port runs one CPU, so that is the reachable arm. */
#ifndef smp_mb
#define	smp_mb()		barrier()
#endif

/* ------------------------------------------------------------------ */
/* error numbers (the Linux set the drivers use)                       */
/* ------------------------------------------------------------------ */

#define	EPERM			1
#define	ENOENT			2
#define	ESRCH			3
#define	EINTR			4
#define	EIO			5
#define	ENXIO			6
#define	E2BIG			7
#define	ENOEXEC			8
#define	EBADF			9
#define	EAGAIN			11
#define	ENOMEM			12
#define	EACCES			13
#define	EFAULT			14
#define	EBUSY			16
#define	EEXIST			17
#define	EXDEV			18
#define	ENODEV			19
#define	ENOTDIR			20
#define	EISDIR			21
#define	EINVAL			22
#define	ENFILE			23
#define	EMFILE			24
#define	EFBIG			27
#define	ENOSPC			28
#define	ESPIPE			29
#define	EROFS			30
#define	EMLINK			31
#define	EDOM			33
#define	ERANGE			34
#define	EDEADLK			35
#define	ENOSYS			38
#define	ELOOP			40
#define	ENOTEMPTY		41
#define	ENOTCONN		53
#define	ETIMEDOUT		110
#define	ECONNREFUSED		111
#define	EALREADY		114
#define	EINPROGRESS		115
#define	ESHUTDOWN		108
#define	EPROTO			71
#define	EILSEQ			84
#define	ETIME			62
#define	ECOMM			70
#define	EOVERFLOW		75
#define	ECANCELED		125
#define	ENODATA			61
#define	ENOLINK			67
#define	EOPNOTSUPP		95
#define	EPIPE			32
/* [LNX] uapi/asm-generic/errno.h:121 */
#define	ERFKILL			132
/* [LNX] uapi/asm-generic/errno.h:89,96 */
#define	ENOBUFS			105
#define	EHOSTDOWN		112
/* the urb-cancellation code usb_kill_urb delivers on Linux (hub.c's
 * stop set consumes it); the kpi kill surfaces -ENOENT/-ENODEV, the
 * driver's stop set lists both spellings */
#define	ECONNRESET		104

/* ------------------------------------------------------------------ */
/* error pointers (linux/err.h)                                        */
/* ------------------------------------------------------------------ */

#define	MAX_ERRNO	4095
#define	IS_ERR_VALUE(x)	((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)
static __always_inline __unused void *ERR_PTR(long error) { return (void *)error; }
static __always_inline __unused long PTR_ERR(const void *ptr) { return (long)ptr; }
static __always_inline __unused bool IS_ERR(const void *ptr) { return IS_ERR_VALUE((unsigned long)ptr); }
static __always_inline __unused bool IS_ERR_OR_NULL(const void *ptr)
{
	return ptr == NULL || IS_ERR(ptr);
}
static __always_inline __unused void *ERR_CAST(const void *ptr) { return (void *)ptr; }

/* ------------------------------------------------------------------ */
/* bitmap API over unsigned long[]                                     */
/* ------------------------------------------------------------------ */

#define	DECLARE_BITMAP(name, bits)	\
	unsigned long name[BITS_TO_LONGS(bits)]
#define	BITS_TO_LONGS(nr)	DIV_ROUND_UP(nr, BITS_PER_LONG)

static __always_inline __unused void
set_bit(unsigned int nr, volatile unsigned long *addr)
{
	addr[nr / BITS_PER_LONG] |= BIT(nr % BITS_PER_LONG);
}

static __always_inline __unused void
clear_bit(unsigned int nr, volatile unsigned long *addr)
{
	addr[nr / BITS_PER_LONG] &= ~BIT(nr % BITS_PER_LONG);
}

static __always_inline __unused int
test_bit(unsigned int nr, const volatile unsigned long *addr)
{
	return (int)((addr[nr / BITS_PER_LONG] >> (nr % BITS_PER_LONG)) & 1);
}

static __always_inline __unused int
test_and_set_bit(unsigned int nr, volatile unsigned long *addr)
{
	int old = test_bit(nr, addr);

	set_bit(nr, addr);
	return old;
}

static __always_inline __unused int
test_and_clear_bit(unsigned int nr, volatile unsigned long *addr)
{
	int old = test_bit(nr, addr);

	clear_bit(nr, addr);
	return old;
}

/* the __-prefixed form is the non-atomic-bitops name; same impl */
#define	__test_and_set_bit(nr, addr)	test_and_set_bit((nr), (addr))
#define	__test_and_clear_bit(nr, addr)	test_and_clear_bit((nr), (addr))
/* [LNX] include/linux/bitops.h:53-54: `#define __set_bit(nr, addr)
 * bitop(___set_bit, nr, addr)` and its clear twin - the non-atomic, non-ordered
 * form. This port's set_bit/clear_bit (above) are already plain stores, so the
 * underscored spelling is the same operation under the interface's other name
 * rather than a second mechanism. */
#define	__set_bit(nr, addr)		set_bit((nr), (addr))
#define	__clear_bit(nr, addr)		clear_bit((nr), (addr))

static __always_inline __unused void
bitmap_zero(unsigned long *dst, unsigned int nbits)
{
	memset(dst, 0, BITS_TO_LONGS(nbits) * sizeof(unsigned long));
}

static __always_inline __unused unsigned long
find_first_zero_bit(const unsigned long *addr, unsigned long size)
{
	unsigned long i;

	for (i = 0; i < size; i++)
		if (!test_bit(i, addr))
			return i;
	return size;
}

static __always_inline __unused unsigned long
find_next_bit(const unsigned long *addr, unsigned long size, unsigned long off)
{
	unsigned long i;

	for (i = off; i < size; i++)
		if (test_bit(i, addr))
			return i;
	return size;
}

static __always_inline __unused unsigned long
find_next_zero_bit(const unsigned long *addr, unsigned long size,
    unsigned long off)
{
	unsigned long i;

	for (i = off; i < size; i++)
		if (!test_bit(i, addr))
			return i;
	return size;
}

static __always_inline __unused unsigned long
find_first_bit(const unsigned long *addr, unsigned long size)
{

	return find_next_bit(addr, size, 0);
}

/*
 * [LNX] include/linux/find.h:404-412 (the interface's inline, over the
 * _find_last_bit() library body at lib/find_bit.c:221): the highest set bit at or
 * below size, or size when none is set. The interface's body scans the last word
 * down; this port states the same walk directly over the bitmap helpers above
 * rather than carrying the interface's small_const_nbits() fast path, which is an
 * optimisation and not a difference in the answer.
 */
static __always_inline __unused unsigned long
find_last_bit(const unsigned long *addr, unsigned long size)
{
	unsigned long i;

	for (i = size; i > 0; i--)
		if (test_bit((unsigned int)(i - 1), addr))
			return i - 1;
	return size;
}

/* rtw89's MLO link bookkeeping walks these directly (12 sites) - the
 * upstream iterators over find_next_bit/find_first_zero_bit */
#define	for_each_set_bit(bit, addr, size)				\
	for ((bit) = find_next_bit((addr), (size), 0);			\
	     (bit) < (size);						\
	     (bit) = find_next_bit((addr), (size), (bit) + 1))

static __always_inline __unused void
bitmap_fill(unsigned long *dst, unsigned int nbits)
{
	unsigned int n = BITS_TO_LONGS(nbits) * sizeof(unsigned long);

	memset(dst, 0xff, n);
	if (nbits % BITS_PER_LONG != 0)
		dst[BITS_TO_LONGS(nbits) - 1] &=
		    ~(~0UL << (nbits % BITS_PER_LONG));
}

static __always_inline __unused void
bitmap_copy(unsigned long *dst, const unsigned long *src, unsigned int nbits)
{
	memcpy(dst, src, BITS_TO_LONGS(nbits) * sizeof(unsigned long));
}

static __always_inline __unused void
bitmap_or(unsigned long *dst, const unsigned long *src1,
    const unsigned long *src2, unsigned int nbits)
{
	unsigned int i, n = BITS_TO_LONGS(nbits);

	for (i = 0; i < n; i++)
		dst[i] = src1[i] | src2[i];
}

static __always_inline __unused int
bitmap_empty(const unsigned long *src, unsigned int nbits)
{
	unsigned int i, n = BITS_TO_LONGS(nbits);

	for (i = 0; i < n; i++)
		if (src[i])
			return 0;
	return 1;
}

static __always_inline __unused unsigned int
bitmap_weight(const unsigned long *src, unsigned int nbits)
{
	unsigned int i, n = BITS_TO_LONGS(nbits), w = 0;

	for (i = 0; i < n; i++)
		w += lkpi_hweight64(src[i]);
	return w;
}

/* BITMAP_FROM_U64/BITS_TO_U64: rtw89 uses the LE layout helpers */
#define	BITS_PER_TYPE(type)	(sizeof(type) * BITS_PER_BYTE)
#define	BITS_TO_BYTES(nr)	DIV_ROUND_UP(nr, BITS_PER_BYTE)
#define	BITS_TO_U64(nr)		DIV_ROUND_UP(nr, BITS_PER_TYPE(u64))

/* ------------------------------------------------------------------ */
/* byte order (asm/byteorder.h)                                        */
/* ------------------------------------------------------------------ */

/*
 * The Linux __LITTLE_ENDIAN macro: the drivers pick little-endian bitfield
 * layouts with it; without it every #ifdef __LITTLE_ENDIAN struct lays out
 * as big-endian and the driver writes garbage to the chip (the board-proven
 * trap: phy condition tiles parse as config pairs and the MAC clocks die).
 */
#define	__LITTLE_ENDIAN		1234
#define	__BIG_ENDIAN		4321
#define	__BYTE_ORDER		__LITTLE_ENDIAN

#if defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN
#define	cpu_to_le16(x)		((__u16)(x))
#define	cpu_to_le32(x)		((__u32)(x))
#define	cpu_to_le64(x)		((__u64)(x))
#define	le16_to_cpu(x)		((__u16)(x))
#define	le32_to_cpu(x)		((__u32)(x))
#define	le64_to_cpu(x)		((__u64)(x))
#else
#define	cpu_to_le16(x)		((__u16)__builtin_bswap16(x))
#define	cpu_to_le32(x)		((__u32)__builtin_bswap32(x))
#define	cpu_to_le64(x)		((__u64)__builtin_bswap64(x))
#define	le16_to_cpu(x)		((__u16)__builtin_bswap16(x))
#define	le32_to_cpu(x)		((__u32)__builtin_bswap32(x))
#define	le64_to_cpu(x)		((__u64)__builtin_bswap64(x))
#endif
#define	cpu_to_be16(x)		((__u16)__builtin_bswap16(x))
#define	cpu_to_be32(x)		((__u32)__builtin_bswap32(x))
#define	cpu_to_be64(x)		((__u64)__builtin_bswap64(x))
#define	be16_to_cpu(x)		((__u16)__builtin_bswap16(x))
#define	be32_to_cpu(x)		((__u32)__builtin_bswap32(x))
#define	be64_to_cpu(x)		((__u64)__builtin_bswap64(x))
#define	__le16_to_cpu(x)	le16_to_cpu(x)
#define	__le32_to_cpu(x)	le32_to_cpu(x)
#define	__cpu_to_le16(x)	cpu_to_le16(x)
#define	__cpu_to_le32(x)	cpu_to_le32(x)
#define	htole16(x)		cpu_to_le16(x)
#define	htole32(x)		cpu_to_le32(x)
#define	htole64(x)		cpu_to_le64(x)
#define	le32_to_cpup(p)		le32_to_cpu(*(const __le32 *)(p))
#define	le16_to_cpup(p)		le16_to_cpu(*(const __le16 *)(p))
/*
 * The underscored "pointer" spellings. The interface states them as
 * static __always_inline functions (include/uapi/linux/byteorder/little_endian.h:
 * 57,65,89), so they are carried as functions here rather than as macros: the
 * byteorder face (components/linuxkpi/include/byteorder/) spells the same three as
 * macros that reach the core's conversions, and a function and a function-like
 * macro of one name coexist (the macro shadows the function at the call site),
 * whereas two macros of one name would not. The compiled set reaches these names
 * through the core contract rather than through the byteorder face.
 */
/* [LNX] include/uapi/linux/byteorder/little_endian.h:57-60 */
static __always_inline __unused u32
__le32_to_cpup(const __le32 *p)
{
	return (u32)*p;
}

/* [LNX] include/uapi/linux/byteorder/little_endian.h:65-68 */
static __always_inline __unused u16
__le16_to_cpup(const __le16 *p)
{
	return (u16)*p;
}

/* [LNX] include/uapi/linux/byteorder/little_endian.h:89-92 (the little-endian
 * spelling of __be16_to_cpup, which byte-swaps the loaded value). */
static __always_inline __unused u16
be16_to_cpup(const __be16 *p)
{
	return be16_to_cpu(*p);
}
/*
 * [LNX] include/net/checksum.h:107-110: `static __always_inline __wsum
 * csum_unfold(__sum16 n) { return (__force __wsum)n; }`. The interface keeps it in
 * net/checksum.h, next to the checksum arithmetic; the compiled set reaches it
 * through a translation unit that does not include that header, so it is stated
 * here with the rest of the wire-order conversions it belongs with. It is the
 * interface's own body: the 16-bit folded sum and the 32-bit unfolded sum share a
 * representation, so the "unfold" is the identity reinterpretation.
 */
static __always_inline __unused __wsum
csum_unfold(__sum16 n)
{
	return (__wsum)n;
}
static __always_inline __unused void
le16_add_cpu(__le16 *var, u16 val)
{
	*var = cpu_to_le16((u16)(le16_to_cpu(*var) + val));
}
static __always_inline __unused void
le32_add_cpu(__le32 *var, u32 val)
{
	*var = cpu_to_le32(le32_to_cpu(*var) + val);
}
#define	le16toh(x)		le16_to_cpu(x)
#define	le32toh(x)		le32_to_cpu(x)
#define	le64toh(x)		le64_to_cpu(x)
#define	htobe16(x)		cpu_to_be16(x)
#define	htobe32(x)		cpu_to_be32(x)
#define	htobe64(x)		cpu_to_be64(x)
#define	be16toh(x)		be16_to_cpu(x)
#define	be32toh(x)		be32_to_cpu(x)
#define	be64toh(x)		be64_to_cpu(x)
#define	get_unaligned_le16(p)	le16_to_cpu((__force __le16)					\
				    ((const u8 *)(p))[0] |				\
				    ((u16)((const u8 *)(p))[1] << 8))
#define	get_unaligned_le32(p)	le32_to_cpu((__force __le32)					\
				    ((const u8 *)(p))[0] |				\
				    ((u32)((const u8 *)(p))[1] << 8) |			\
				    ((u32)((const u8 *)(p))[2] << 16) |			\
				    ((u32)((const u8 *)(p))[3] << 24))
#define	get_unaligned_le64(p)	le64_to_cpu((__force __le64)					\
				    (u64)get_unaligned_le32((const u8 *)(p)) |		\
				    ((u64)get_unaligned_le32((const u8 *)(p) + 4) << 32))
/*
 * The internal temporary of each store carries the width in its name. The
 * interface's own __put_unaligned_le32() is a function, so its local cannot
 * collide with the caller's; these are macros, and a shared `__v` makes the
 * 64-bit form's argument to the 32-bit form read the 32-bit form's own
 * not-yet-initialised local instead of the outer value (measured as an
 * uninitialised read and a shift past the width at the driver's warning level).
 */
#define	put_unaligned_le16(v, p) do {						\
	u16 __lkpi_le16v = le16_to_cpu((__force __u16)(v));			\
	((u8 *)(p))[0] = (u8)__lkpi_le16v;					\
	((u8 *)(p))[1] = (u8)(__lkpi_le16v >> 8);				\
} while (0)
#define	put_unaligned_le32(v, p) do {						\
	u32 __lkpi_le32v = le32_to_cpu((__force __u32)(v));			\
	((u8 *)(p))[0] = (u8)__lkpi_le32v;					\
	((u8 *)(p))[1] = (u8)(__lkpi_le32v >> 8);				\
	((u8 *)(p))[2] = (u8)(__lkpi_le32v >> 16);				\
	((u8 *)(p))[3] = (u8)(__lkpi_le32v >> 24);				\
} while (0)
#define	put_unaligned_le64(v, p) do {						\
	u64 __lkpi_le64v = le64_to_cpu((__force __u64)(v));			\
	put_unaligned_le32((__u32)__lkpi_le64v, p);				\
	put_unaligned_le32((__u32)(__lkpi_le64v >> 32), (u8 *)(p) + 4);		\
} while (0)

/* ------------------------------------------------------------------ */
/* bitfield helpers (linux/bitfield.h + the le*p forms)                */
/* ------------------------------------------------------------------ */

#ifndef FIELD_GET
#define	FIELD_GET(_mask, _reg)	\
	((__typeof(_mask))(((_reg) & (_mask)) >> __builtin_ctzl(_mask)))
#endif
#ifndef FIELD_PREP
#define	FIELD_PREP(_mask, _val)	\
	((__typeof(_mask))(((_val) << __builtin_ctzl(_mask)) & (_mask)))
#endif
/* the constant-folding flavor rtw89 uses in static initializers and
 * static_assert; 64-bit shift position covers the >32-bit masks */
#ifndef FIELD_PREP_CONST
#define	FIELD_PREP_CONST(_mask, _val)	\
	((__typeof(_mask))(((_val) << __builtin_ctzll((u64)(_mask))) & (_mask)))
#endif
#ifndef FIELD_GET_CONST
#define	FIELD_GET_CONST(_mask, _reg)	\
	((__typeof(_reg))(((_reg) & (_mask)) >> __builtin_ctzll((u64)(_mask))))
#endif
#define	FIELD_MAX(_mask)	((_mask) >> __builtin_ctzl(_mask))
#define	FIELD_FIT(_mask, _val)	(((typeof(_mask))(_val) << __builtin_ctzl(_mask)) >> __builtin_ctzl(_mask) == (typeof(_mask))(_val))
/* [LNX] include/linux/bitfield.h:47: the shift a field mask names, which a compiled
 * set uses to place a value into a multi-bit field constant. */
#define	__bf_shf		__builtin_ctzll

static __always_inline __unused u32
le32_get_bits(__le32 v, u32 mask)
{
	return (le32_to_cpu(v) & mask) >> __builtin_ctzl(mask);
}

static __always_inline __unused u32
le16_get_bits(__le16 v, u16 mask)
{
	return (le16_to_cpu(v) & mask) >> __builtin_ctzl(mask);
}

static __always_inline __unused u16
u16_encode_bits(u16 v, u16 mask)
{
	return (u16)((v << __builtin_ctz(mask)) & mask);
}

static __always_inline __unused __le32
le32_encode_bits(u32 v, u32 mask)
{
	return cpu_to_le32((v << __builtin_ctzl(mask)) & mask);
}

static __always_inline __unused u64
le64_get_bits(__le64 v, u64 mask)
{
	return (le64_to_cpu(v) & mask) >> __builtin_ctzll(mask);
}

static __always_inline __unused u32
u8_get_bits(u8 v, u8 mask)
{
	return (u32)((v & mask) >> __builtin_ctz(mask));
}

static __always_inline __unused u32
u16_get_bits(u16 v, u16 mask)
{
	return (u32)((v & mask) >> __builtin_ctz(mask));
}

static __always_inline __unused u32
get_bits(u32 v, u32 mask)
{
	return (v & mask) >> __builtin_ctzl(mask);
}

static __always_inline __unused u32
u32_get_bits(u32 v, u32 mask)
{
	return get_bits(v, mask);
}

static __always_inline __unused u32
u32_encode_bits(u32 v, u32 mask)
{
	return (v << __builtin_ctzl(mask)) & mask;
}

static __always_inline __unused u64
u64_encode_bits(u64 v, u64 mask)
{
	return (v << __builtin_ctzll(mask)) & mask;
}

static __always_inline __unused u16
le16_encode_bits(u16 v, u16 mask)
{
	return (u16)((v << __builtin_ctzl(mask)) & mask);
}

/* the 7.x generated encode/replace family (____MAKE_OP): u8/u16/u32/u64
 * and their le/be p_replace forms */
static __always_inline __unused u8
u8_encode_bits(u8 v, u8 mask)
{
	return (u8)((v << __builtin_ctz(mask)) & mask);
}

static __always_inline __unused u32
u32_replace_bits(u32 old, u32 val, u32 mask)
{
	return (old & ~mask) | u32_encode_bits(val, mask);
}

static __always_inline __unused u16
u16_replace_bits(u16 old, u16 val, u16 mask)
{
	return (u16)((old & ~mask) | u16_encode_bits(val, mask));
}

static __always_inline __unused void
le16p_replace_bits(__le16 *p, u16 val, u16 mask)
{
	*p = cpu_to_le16(u16_replace_bits(le16_to_cpu(*p), val, mask));
}

/* field_get: mask may not be a compile-time constant (regs tables) */
static __always_inline __unused u64
lkpi_field_get64(u64 mask, u64 reg)
{
	return (reg & mask) >> __builtin_ctzll(mask);
}

#define	field_get(mask, reg)						\
	((__typeof(mask))lkpi_field_get64((u64)(mask), (u64)(reg)))
#define	field_prep(mask, val)						\
	((__typeof(mask))(((u64)(val) << __builtin_ctzll((u64)(mask))) & (u64)(mask)))

static __always_inline __unused void
le32p_replace_bits(__le32 *p, u32 v, u32 mask)
{
	u32 val = le32_to_cpu(*p);

	val = (val & ~mask) | ((v << __builtin_ctzl(mask)) & mask);
	*p = cpu_to_le32(val);
}

static __always_inline __unused void
u8p_replace_bits(u8 *p, u8 v, u8 mask)
{
	*p = (u8)((*p & ~mask) | ((v << __builtin_ctzl(mask)) & mask));
}

static __always_inline __unused void
u32p_replace_bits(u32 *p, u32 v, u32 mask)
{
	*p = (*p & ~mask) | ((v << __builtin_ctzl(mask)) & mask);
}

/* ------------------------------------------------------------------ */
/* allocation                                                          */
/* ------------------------------------------------------------------ */

/*
 * GFP_* only selects wait-or-fail: the heap either sleeps or returns NULL.
 * The size header lets kfree()/vfree() recover the size the heap wants.
 */
#define	GFP_KERNEL		0x00000001
#define	GFP_ATOMIC		0x00000002
#define	GFP_DMA			0x00000004
#define	GFP_NOWAIT		0x00000008
#define	GFP_NOIO		0x00000010	/* wait-or-fail here: same as KERNEL */
#define	__GFP_ZERO		0x80000000

/* the forever wait the timeout paths sentinel */
#define	MAX_SCHEDULE_TIMEOUT	((unsigned long)0x7fffffff)

/* [LNX] include/uapi/linux/if_ether.h:14 */
#define	ETH_ALEN	6

void *lkpi_kmem_alloc(size_t, int flags, bool zero);
void lkpi_kmem_free(const void *);
/* The size the heap handed out for a block, read back from the block's own header
 * (linuxkpi_core.c, "allocation"). Zero for NULL or for a pointer this allocator
 * never handed out - which is what makes krealloc()/kfree_sensitive() below able to
 * preserve or clear exactly the old bytes. */
size_t lkpi_kmem_size(const void *);

static __always_inline __unused void *
kmalloc(size_t size, gfp_t flags)
{
	return lkpi_kmem_alloc(size, (int)flags, false);
}

static __always_inline __unused void *
kzalloc(size_t size, gfp_t flags)
{
	return lkpi_kmem_alloc(size, (int)flags, true);
}

static __always_inline __unused void *
kcalloc(size_t n, size_t size, gfp_t flags)
{
	return lkpi_kmem_alloc(n * size, (int)flags, true);
}

static __always_inline __unused void
kfree(const void *p)
{
	lkpi_kmem_free(p);
}

static __always_inline __unused void *
kmemdup(const void *src, size_t len, gfp_t flags)
{
	void *p = kmalloc(len, flags);

	if (p != NULL)
		memcpy(p, src, len);
	return p;
}

static __always_inline __unused void *
vmalloc(size_t size)
{
	return lkpi_kmem_alloc(size, GFP_KERNEL, false);
}

static __always_inline __unused void *
vzalloc(size_t size)
{
	return lkpi_kmem_alloc(size, GFP_KERNEL, true);
}

#define	kvmalloc(size, flags)	kmalloc((size), (flags))

static __always_inline __unused void
vfree(const void *p)
{
	lkpi_kmem_free(p);
}

/*
 * The reallocation and the "sensitive" free of the interface's allocator family
 * (include/linux/slab.h). Their bodies are in linuxkpi_compat.c, over this port's
 * heap: the interface declares them extern, and the port's heap carries each block's
 * size in its own header (lkpi_kmem_size() above), which is what lets krealloc()
 * preserve the old bytes and kfree_sensitive() clear them without a second
 * allocator. kvfree() is the interface's "free either a kmalloc or a vmalloc
 * block"; on this port vmalloc() is the same heap, so it is kfree().
 */
/* [LNX] include/linux/slab.h:593 (krealloc over krealloc_node) */
void *krealloc(const void *p, size_t new_size, gfp_t flags);
/* [LNX] include/linux/slab.h:1422 */
void kvfree(const void *addr);
/* [LNX] include/linux/string.h:304 */
void *kvmemdup(const void *src, size_t len, gfp_t gfp);
/* [LNX] include/linux/slab.h:597 */
void kfree_sensitive(const void *objp);

/* devm allocations are plain allocations: the driver lives as long as the
 * image and every boot starts from a clean slate. */
static __always_inline __unused void *
devm_kmalloc(struct device *dev, size_t size, gfp_t flags)
{
	(void)dev;
	return kmalloc(size, flags);
}

static __always_inline __unused void *
devm_kzalloc(struct device *dev, size_t size, gfp_t flags)
{
	(void)dev;
	return kzalloc(size, flags);
}

static __always_inline __unused void *
devm_kmemdup(struct device *dev, const void *src, size_t len, gfp_t flags)
{
	(void)dev;
	return kmemdup(src, len, flags);
}

static __always_inline __unused void *
devm_kmemdup_array(struct device *dev, const void *src, size_t n,
    size_t size, gfp_t flags)
{
	(void)dev;
	return kmemdup(src, n * size, flags);
}

/* The object/flexible allocation family of the interface (include/linux/slab.h). The
 * interface selects its optional trailing GFP argument with a comma-paste whose behaviour
 * for an absent argument differs between GNU and strict modes; this port compiles in
 * strict mode and carries the forms the compiled set calls (no explicit GFP), stating
 * GFP_KERNEL as the default the interface's own pick yields. */
#define	kmalloc_array(n, size, flags)	kmalloc((n) * (size), (flags))
#define	kcalloc(n, size, flags)		kzalloc((n) * (size), (flags))
#define	devm_kcalloc(dev, n, size, flags)				\
	({ (void)(dev); kcalloc((n), (size), (flags)); })
#define	devm_kfree(dev, p)		({ (void)(dev); kfree(p); })
#define	devm_kmalloc_array(dev, n, size, flags)				\
	({ (void)(dev); kmalloc((n) * (size), (flags)); })

/* ------------------------------------------------------------------ */
/* lists (linux/list.h)                                                */
/* ------------------------------------------------------------------ */

struct list_head {
	struct list_head *next, *prev;
};

#define	LIST_HEAD_INIT(name)	{ &(name), &(name) }
#define	LIST_HEAD(name)		struct list_head name = LIST_HEAD_INIT(name)

static __always_inline __unused void
INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

static __always_inline __unused void
__list_add(struct list_head *n, struct list_head *prev,
    struct list_head *next)
{
	next->prev = n;
	n->next = next;
	n->prev = prev;
	prev->next = n;
}

static __always_inline __unused void
list_add(struct list_head *n, struct list_head *head)
{
	__list_add(n, head, head->next);
}

static __always_inline __unused void
list_add_tail(struct list_head *n, struct list_head *head)
{
	__list_add(n, head->prev, head);
}

static __always_inline __unused void
__list_del(struct list_head *prev, struct list_head *next)
{
	next->prev = prev;
	prev->next = next;
}

static __always_inline __unused void
list_del(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
}

static __always_inline __unused void
list_del_init(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	INIT_LIST_HEAD(entry);
}

static __always_inline __unused int
list_empty(const struct list_head *head)
{
	return head->next == head;
}

static __always_inline __unused int
list_is_last(const struct list_head *list, const struct list_head *head)
{
	return list->next == head;
}

static __always_inline __unused void
list_move_tail(struct list_head *list, struct list_head *head)
{
	__list_del(list->prev, list->next);
	list_add_tail(list, head);
}

/* list_splice_tail: append every node of `list` to `head`'s tail */
static __always_inline __unused void
lkpi_list_splice_tail(struct list_head *list, struct list_head *head)
{
	struct list_head *pos, *tmp;

	if (list == head)
		return;
	for (pos = list->next; pos != list; pos = tmp) {
		tmp = pos->next;
		list_add_tail(pos, head);
	}
	INIT_LIST_HEAD(list);
}

static __always_inline __unused void
list_splice_tail(struct list_head *list, struct list_head *head)
{
	lkpi_list_splice_tail(list, head);
}

/* [LNX] include/linux/list.h:632-640: the tail-splice that also reinitialises the
 * emptied source. The core's lkpi_list_splice_tail() above already leaves the
 * source a valid empty list, so the init variant is that operation under the
 * interface's other name. */
static __always_inline __unused void
list_splice_tail_init(struct list_head *list, struct list_head *head)
{
	lkpi_list_splice_tail(list, head);
}

/* [LNX] include/linux/list.h:280-289: replace `old` with `new` in place, relinking
 * the neighbours around the new node (the interface's body). */
static __always_inline __unused void
list_replace(struct list_head *old, struct list_head *new)
{
	new->next = old->next;
	new->next->prev = new;
	new->prev = old->prev;
	new->prev->next = new;
}

/* [LNX] include/linux/list.h:303-307: the replace that also reinitialises the old
 * node, which is what lets it be reused or tested for emptiness afterwards. */
static __always_inline __unused void
list_replace_init(struct list_head *old, struct list_head *new)
{
	list_replace(old, new);
	INIT_LIST_HEAD(old);
}

static __always_inline __unused void
list_move(struct list_head *list, struct list_head *head)
{
	__list_del(list->prev, list->next);
	list_add(list, head);
}

#define	list_entry(ptr, type, member)		container_of(ptr, type, member)
#define	list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)
#define	list_last_entry(ptr, type, member) \
	list_entry((ptr)->prev, type, member)
#define	list_first_entry_or_null(ptr, type, member) \
	(!list_empty(ptr) ? list_first_entry(ptr, type, member) : NULL)
#define	list_next_entry(pos, member) \
	list_entry((pos)->member.next, typeof(*(pos)), member)
#define	list_prev_entry(pos, member) \
	list_entry((pos)->member.prev, typeof(*(pos)), member)

#define	list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)
#define	list_for_each_safe(pos, n, head)				\
	for (pos = (head)->next, n = pos->next; pos != (head);		\
	     pos = n, n = pos->next)
#define	list_for_each_entry(pos, head, member)				\
	for (pos = list_first_entry(head, typeof(*pos), member);	\
	     &pos->member != (head);					\
	     pos = list_next_entry(pos, member))
#define	list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = list_first_entry(head, typeof(*pos), member),	\
	     n = list_next_entry(pos, member);				\
	     &pos->member != (head);					\
	     pos = n, n = list_next_entry(n, member))
#define	list_for_each_entry_reverse(pos, head, member)			\
	for (pos = list_last_entry(head, typeof(*pos), member);		\
	     &pos->member != (head);					\
	     pos = list_prev_entry(pos, member))

/* hlist: the rxtx/completion paths iterate plain chains */
struct hlist_head {
	struct hlist_node *first;
};

struct hlist_node {
	struct hlist_node *next, **pprev;
};

static __always_inline __unused void
INIT_HLIST_HEAD(struct hlist_head *h)
{
	h->first = NULL;
}

static __always_inline __unused void
INIT_HLIST_NODE(struct hlist_node *n)
{
	n->next = NULL;
	n->pprev = NULL;
}

static __always_inline __unused int
hlist_unhashed(const struct hlist_node *n)
{
	return n->pprev == NULL;
}

static __always_inline __unused void
hlist_add_head(struct hlist_node *n, struct hlist_head *h)
{
	struct hlist_node *first = h->first;

	n->next = first;
	if (first != NULL)
		first->pprev = &n->next;
	h->first = n;
	n->pprev = &h->first;
}

static __always_inline __unused void
hlist_del(struct hlist_node *n)
{
	struct hlist_node *next = n->next;
	struct hlist_node **pprev = n->pprev;

	if (pprev != NULL) {
		*pprev = next;
		if (next != NULL)
			next->pprev = pprev;
	}
}

#define	hlist_entry_safe(ptr, type, member)				\
	((ptr) != NULL ? container_of(ptr, type, member) : NULL)
#define	hlist_for_each_entry(pos, head, member)				\
	for (pos = hlist_entry_safe((head)->first, typeof(*(pos)), member); \
	     pos != NULL;						\
	     pos = hlist_entry_safe((pos)->member.next, typeof(*(pos)), member))

/* ------------------------------------------------------------------ */
/* kref / rcu (passive-level stubs; drivers never race these here)     */
/* ------------------------------------------------------------------ */

/* refcount_t (7.x API; rtw89's ap-info refcount) */
typedef struct {
	int	refs;
} refcount_t;
#define	REFCOUNT_INIT(n)	{ .refs = (n) }
static __always_inline __unused void refcount_set(refcount_t *r, int n) { r->refs = n; }
static __always_inline __unused int refcount_read(const refcount_t *r) { return r->refs; }
static __always_inline __unused void refcount_inc(refcount_t *r) { r->refs++; }
static __always_inline __unused bool refcount_inc_not_zero(refcount_t *r)
{
	if (r->refs == 0)
		return false;
	r->refs++;
	return true;
}
static __always_inline __unused bool refcount_dec_and_test(refcount_t *r)
{
	return --r->refs == 0;
}

/* atomic64 (key_conf->tx_pn): single-threaded tx path, plain arithmetic */
typedef struct {
	volatile s64 counter;
} atomic64_t;
#define	ATOMIC64_INIT(i)	{ .counter = (i) }
static __always_inline __unused s64 atomic64_read(const atomic64_t *v)
{
	return v->counter;
}
static __always_inline __unused void atomic64_set(atomic64_t *v, s64 i)
{
	v->counter = i;
}
static __always_inline __unused s64 atomic64_inc_return(atomic64_t *v)
{
	return ++v->counter;
}

struct kref {
	int	refcount;
};

static __always_inline __unused void
kref_init(struct kref *k)
{
	k->refcount = 1;
}

static __always_inline __unused void
kref_get(struct kref *k)
{
	k->refcount++;
}

static __always_inline __unused int
kref_put(struct kref *k, void (*release)(struct kref *))
{
	if (--k->refcount == 0) {
		release(k);
		return 1;
	}
	return 0;
}

struct rcu_head {
	struct rcu_head	*next;
	void		(*func)(struct rcu_head *head);
};

#define	rcu_read_lock()		do { } while (0)
#define	rcu_read_unlock()	do { } while (0)
#define	rcu_dereference(p)	(p)
#define	rcu_access_pointer(p)	(p)
#define	rcu_assign_pointer(p, v) do { (p) = (v); } while (0)
#define	rcu_replace_pointer(p, v, c) do { (p) = (v); } while (0)
#define	synchronize_rcu()	do { } while (0)
#define	kfree_rcu(p, member)	kfree(p)
/* call_rcu has no grace period to defer to on this port; invoking the
 * callback in place is the same effective semantics as kfree_rcu above.
 * The only async RCU free site (rtw89_core_release_tx_wait) is actually
 * unreachable - the tx-wait path is polled - but the symbol is needed. */
static __always_inline __unused void
call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *head))
{
	func(head);
}

/*
 * The rest of the RCU pointer vocabulary the compiled set names. The interface's
 * checked forms (include/linux/rcupdate.h:660,741) are rcu_dereference() under a
 * condition that only the lockdep validator evaluates - with the validator off the
 * condition is not evaluated at all (RCU_LOCKDEP_WARN() expands to nothing), which
 * is exactly this port's state. So the checked dereferences are the plain read the
 * core's rcu_dereference() already is, and the condition argument is carried
 * (unused) so a caller reads unchanged.
 */
/* [LNX] include/linux/rcupdate.h:1041 */
#define	RCU_INIT_POINTER(p, v)		do { WRITE_ONCE((p), (v)); } while (0)
/* [LNX] include/linux/rcupdate.h:660-662 */
#define	rcu_dereference_check(p, c)	(p)
/* [LNX] include/linux/rcupdate.h:741-742 */
#define	rcu_dereference_protected(p, c)	(p)

/*
 * [LNX] include/net/cfg80211.h:6834-6835 and include/net/mac80211.h:2345-2346,
 * 2775-2776: the wiphy- and link-scoped accessors the wireless stack builds on
 * rcu_dereference_protected(). Their home headers are the net/cfg80211.h and
 * net/mac80211.h faces (which state the sibling _check accessors); the compiled set
 * reaches these names through the core contract, and the port has no wiphy mutex to
 * assert on, so they are the same plain link/wiphy read the face's _check accessors
 * already are.
 */
#define	wiphy_dereference(wiphy, p)					\
	rcu_dereference_protected((p), lockdep_is_held(&(wiphy)->mtx))
#define	link_conf_dereference_protected(vif, link_id)			\
	((vif)->link_conf[link_id])
#define	link_sta_dereference_protected(sta, link_id)			\
	((sta)->link[link_id])

/*
 * [LNX] include/linux/netdevice.h:90: `void synchronize_net(void);`. The interface's
 * body waits for RCU readers and drains the rtnl todo list; this port's
 * synchronize_rcu() is a no-op (no grace period exists) and its RTNL has no todo
 * list, so the body (linuxkpi_compat.c) is empty. It is stated with the RCU
 * synchronize it is built on.
 */
void synchronize_net(void);

/* ------------------------------------------------------------------ */
/* atomics (linux/atomic.h) - plain int with libcall fallbacks          */
/* ------------------------------------------------------------------ */

typedef struct {
	volatile int counter;
} atomic_t;

#define	ATOMIC_INIT(i)		{ .counter = (i) }
#define	atomic_read(v)		((v)->counter)
#define	atomic_set(v, i)	do { (v)->counter = (i); } while (0)
void lkpi_atomic_inc(atomic_t *v);
void lkpi_atomic_dec(atomic_t *v);
int lkpi_atomic_inc_return(atomic_t *v);
int lkpi_atomic_dec_return(atomic_t *v);
int lkpi_atomic_dec_and_test(atomic_t *v);
void lkpi_atomic_add(int i, atomic_t *v);
void lkpi_atomic_sub(int i, atomic_t *v);

static __always_inline __unused int
lkpi_atomic_cmpxchg(atomic_t *v, int old, int new)
{
	int cur = v->counter;

	if (cur == old)
		v->counter = (int)new;
	return cur;
}

#define	atomic_cmpxchg(v, old, new)	lkpi_atomic_cmpxchg((v), (old), (new))
#define	atomic_inc(v)		lkpi_atomic_inc(v)
#define	atomic_dec(v)		lkpi_atomic_dec(v)
#define	atomic_inc_return(v)	lkpi_atomic_inc_return(v)
#define	atomic_dec_return(v)	lkpi_atomic_dec_return(v)
#define	atomic_dec_and_test(v)	lkpi_atomic_dec_and_test(v)
#define	atomic_add(i, v)	lkpi_atomic_add((i), (v))
#define	atomic_sub(i, v)	lkpi_atomic_sub((i), (v))

/*
 * [LNX] include/linux/atomic/atomic-arch-fallback.h:2549-2563 (the body reached
 * through atomic-instrumented.h:1590): `atomic_dec_if_positive` decrements only
 * while the value is positive and returns the new value (or the unchanged,
 * non-positive value when it declined). The interface's body is a cmpxchg retry
 * loop; that is the same loop the contract's atomic_add_unless already spells, so
 * it is stated here in the same shape rather than as a second atomic model.
 */
static __always_inline __unused int
atomic_dec_if_positive(atomic_t *v)
{
	int old = atomic_read(v);
	int dec;

	for (;;) {
		dec = old - 1;
		if (dec < 0)
			return dec;
		if (lkpi_atomic_cmpxchg(v, old, dec) == old)
			return dec;
		old = atomic_read(v);
	}
}

/*
 * [LNX] include/linux/atomic/atomic-arch-fallback.h:2421-2432 (reached through
 * atomic-instrumented.h:1493): `atomic_fetch_add_unless` adds `a` only when the
 * value is not `u`, and returns the value it saw. The interface's cmpxchg retry
 * loop is carried in the same shape as atomic_dec_if_positive() above.
 */
static __always_inline __unused int
atomic_fetch_add_unless(atomic_t *v, int a, int u)
{
	int old = atomic_read(v);

	for (;;) {
		if (old == u)
			return old;
		if (lkpi_atomic_cmpxchg(v, old, old + a) == old)
			return old;
		old = atomic_read(v);
	}
}

/* ------------------------------------------------------------------ */
/* time (linux/{jiffies,ktime,delay,math64}.h)                         */
/* ------------------------------------------------------------------ */

/* the board tick is 1 kHz (CONFIG_TX_TICKS_PER_SECOND, [S3]); HZ must
 * equal the real tick rate or every jiffies<->ms conversion lies */
#define	HZ			1000
unsigned long lkpi_jiffies(void);
#define	jiffies			lkpi_jiffies()

#define	msecs_to_jiffies(m)	((unsigned long)(((m) * HZ + 999) / 1000))
#define	usecs_to_jiffies(u)	msecs_to_jiffies((u) / 1000)
#define	jiffies_to_msecs(j)	((unsigned int)(((j) * 1000) / HZ))
#define	jiffies_to_usecs(j)	((unsigned int)(((j) * 1000000) / HZ))
#define	round_jiffies_relative(j) ((unsigned long)(j))
#define	time_after(a, b)	((long)((b) - (a)) < 0)
#define	time_before(a, b)	time_after((b), (a))
#define	time_after_eq(a, b)	((long)((a) - (b)) >= 0)
#define	time_before_eq(a, b)	time_after_eq((b), (a))
/* [LNX] include/linux/jiffies.h:258,273: the two "against now" comparisons, which
 * the interface builds directly on the wrap-safe pair above. They are stated here
 * with that pair because a compiled translation unit reaches the clock through the
 * core contract rather than through linux/jiffies.h. */
#define	time_is_before_jiffies(a)	time_after(jiffies, (a))
#define	time_is_after_jiffies(a)	time_before(jiffies, (a))

/*
 * [LNX] include/linux/timer.h:176: `unsigned long round_jiffies(unsigned long j);`.
 * The interface rounds an absolute time up to the next second so many wakeups
 * coalesce; this port runs no deferred-timer wheel to round against, so the body
 * (linuxkpi_compat.c) is the identity - the same choice the relative form above
 * already records. It is stated as the interface's extern function rather than as
 * the macro an earlier revision of linux/jiffies.h carried, because the compiled
 * set reaches it through the core contract.
 */
unsigned long round_jiffies(unsigned long j);

/* [LNX] include/linux/ktime.h: `typedef s64 ktime_t;` - the interface's own
 * width, which the driver's %lld/%llu format strings are written against. */
typedef s64 ktime_t;

/* ktime counts nanoseconds, scaled off the hardclock tick */
ktime_t lkpi_ktime_get(void);
#define	ktime_get()		lkpi_ktime_get()
#define	ktime_get_real()	lkpi_ktime_get()
#define	ktime_get_boottime()	lkpi_ktime_get()
#define	ktime_to_ms(kt)		((unsigned int)((kt) / 1000000LL))
#define	ktime_to_us(kt)		((unsigned int)((kt) / 1000LL))
#define	ktime_add_ns(kt, n)	((kt) + (n))
#define	ktime_add_us(kt, us)	((kt) + (int64_t)(us) * 1000LL)
#define	ktime_sub_us(kt, us)	((kt) - (int64_t)(us) * 1000LL)
#define	ktime_add_ms(kt, ms)	((kt) + (int64_t)(ms) * 1000000LL)
#define	ktime_sub(a, b)		((a) - (b))
#define	ktime_ms_delta(later, earlier)	((s64)ktime_to_ms((later) - (earlier)))
#define	ktime_get_boottime_ns()	((u64)lkpi_ktime_get())
#define	ktime_compare(a, b)	((a) < (b) ? -1 : ((a) > (b) ? 1 : 0))
#define	ktime_after(a, b)	((a) > (b))

/* [LNX] include/vdso/time64.h: the nanosecond multiples the compiled set scales
 * tick arithmetic with. Stated before the ktime constructors below, which are
 * defined in terms of NSEC_PER_SEC. */
#define	NSEC_PER_SEC		1000000000L
#define	NSEC_PER_USEC		1000L
/* [LNX] include/linux/time64.h:22-24: the microsecond and millisecond multiples. */
#define	MSEC_PER_SEC		1000L
#define	USEC_PER_SEC		1000000L
#define	USEC_PER_MSEC		1000L

/* [LNX] include/linux/time64.h:30-32: the representable range of a ktime, which
 * ktime_set() saturates against. */
#define	KTIME_MAX		((s64)~((u64)1 << 63))
#define	KTIME_SEC_MAX		(KTIME_MAX / NSEC_PER_SEC)

/* [LNX] include/linux/ktime.h:36-41: `static inline ktime_t ktime_set(const s64
 * secs, const unsigned long nsecs)` - the interface's seconds/nanoseconds
 * constructor, saturating at KTIME_MAX rather than wrapping. */
static __always_inline __unused ktime_t
ktime_set(const s64 secs, const unsigned long nsecs)
{
	if (secs >= KTIME_SEC_MAX)
		return KTIME_MAX;
	return secs * NSEC_PER_SEC + (s64)nsecs;
}

/* [LNX] include/linux/ktime.h:78-81: `static inline s64 ktime_to_ns(const ktime_t
 * kt)` - a ktime already counts nanoseconds, so the conversion is the identity. */
static __always_inline __unused s64
ktime_to_ns(const ktime_t kt)
{
	return kt;
}

/* [LNX] include/linux/timekeeping.h:238-241: `static inline time64_t
 * ktime_get_boottime_seconds(void) { return ktime_divns(ktime_get_coarse_boottime(),
 * NSEC_PER_SEC); }`. This port's boot-time clock is the core's monotonic read
 * (ktime_get_boottime(), above), so the seconds form is that read divided by the
 * nanosecond multiple. */
static __always_inline __unused time64_t
ktime_get_boottime_seconds(void)
{
	return (time64_t)(ktime_get_boottime() / NSEC_PER_SEC);
}

unsigned int get_random_int(void);

void lkpi_udelay(unsigned int us);
void lkpi_mdelay(unsigned int ms);
void lkpi_usleep_range(unsigned int min_us, unsigned int max_us);
void lkpi_msleep(unsigned int ms);

#define	udelay(us)		lkpi_udelay(us)
#define	mdelay(ms)		lkpi_mdelay(ms)
#define	usleep_range(lo, hi)	lkpi_usleep_range((lo), (hi))
#define	msleep(ms)		lkpi_msleep(ms)
/* fsleep: <=10us -> udelay, small values -> usleep_range, else msleep */
static __always_inline __unused void
fsleep(unsigned long usecs)
{
	if (usecs <= 10)
		lkpi_udelay((unsigned int)usecs);
	else if (usecs < 20000)
		lkpi_usleep_range((unsigned int)usecs,
		    (unsigned int)usecs + (unsigned int)(usecs >> 2));
	else
		lkpi_msleep((unsigned int)DIV_ROUND_UP(usecs, 1000));
}

#define	nsecs_to_jiffies(n)	usecs_to_jiffies((unsigned long)((n) / 1000))

/* ------------------------------------------------------------------ */
/* logging                                                             */
/* ------------------------------------------------------------------ */

struct seq_file;
struct file;

#define	KERN_ERR	""
#define	KERN_WARNING	""
#define	KERN_INFO	""
#define	KERN_DEBUG	""

struct va_format {
	const char	*fmt;
	va_list		*va;
};

int lkpi_printf(ulog_level_t level, const char *fmt, ...);
void lkpi_dev_printk(struct device *dev, ulog_level_t level, const char *fmt, ...);
void lkpi_fatal(const char *what);

/*
 * The dist print helpers funnel everything through dev_*(); the debug paths
 * use the Linux "%pV" wrapped-format convention (the conversion's argument is
 * a struct va_format *). The port's printf carries no such conversion, so the
 * core expands it above whichever formatter the build selected (linuxkpi_core.c,
 * lkpi_vformat) - which is what lets the vendored callers keep their spelling
 * unchanged.
 *
 * Each helper carries the console level the vendored call site means. The
 * generic dev_printk() form has no level to carry here: every KERN_ constant is
 * an empty string in this port (the level prefix is not part of the line), so it
 * logs at INFO and the named helpers below are what select a level.
 */
#define	dev_printk(level, dev, fmt, ...)				\
	lkpi_dev_printk((dev), ULOG_INFO_LEVEL, (fmt), ##__VA_ARGS__)
/* [LNX] include/linux/dev_printk.h:149: dev_crit() logs at the critical level, which
 * is this port's ULOG_CRITICAL_LEVEL. It is stated here with the dev_* family the
 * core already carries (the interface's own header is not one a compiled set
 * includes on this port). */
#define	dev_crit(dev, fmt, ...)		lkpi_dev_printk((dev), ULOG_CRITICAL_LEVEL, (fmt), ##__VA_ARGS__)
#define	dev_err(dev, fmt, ...)		lkpi_dev_printk((dev), ULOG_ERROR_LEVEL, (fmt), ##__VA_ARGS__)
#define	dev_warn(dev, fmt, ...)		lkpi_dev_printk((dev), ULOG_WARNING_LEVEL, (fmt), ##__VA_ARGS__)
#define	dev_info(dev, fmt, ...)		lkpi_dev_printk((dev), ULOG_INFO_LEVEL, (fmt), ##__VA_ARGS__)
#define	dev_notice(dev, fmt, ...)	dev_info(dev, fmt, ##__VA_ARGS__)
#define	dev_dbg(dev, fmt, ...)		lkpi_dev_printk((dev), ULOG_DEBUG_LEVEL, (fmt), ##__VA_ARGS__)
#define	dev_warn_once(dev, fmt, ...)	dev_warn(dev, fmt, ##__VA_ARGS__)
#define	dev_err_once(dev, fmt, ...)	dev_err(dev, fmt, ##__VA_ARGS__)
#define	dev_dbg_ratelimited(dev, fmt, ...) do { } while (0)
#define	dev_info_once(dev, fmt, ...)	dev_info(dev, fmt, ##__VA_ARGS__)
#define	dev_notice_once(dev, fmt, ...)	dev_notice(dev, fmt, ##__VA_ARGS__)

/* string choice helpers (linux/string_choices.h) */
static __always_inline __unused const char *str_enable_disable(bool v) { return v ? "enable" : "disable"; }
static __always_inline __unused const char *str_on_off(bool v) { return v ? "on" : "off"; }
static __always_inline __unused const char *str_read_write(bool v) { return v ? "read" : "write"; }
static __always_inline __unused const char *str_yes_no(bool v) { return v ? "yes" : "no"; }
#define	pr_err(fmt, ...)		lkpi_printf(ULOG_ERROR_LEVEL, fmt "\n", ##__VA_ARGS__)
#define	pr_warn(fmt, ...)		lkpi_printf(ULOG_WARNING_LEVEL, fmt "\n", ##__VA_ARGS__)
#define	pr_info(fmt, ...)		lkpi_printf(ULOG_INFO_LEVEL, fmt "\n", ##__VA_ARGS__)
#define	pr_notice(fmt, ...)		lkpi_printf(ULOG_INFO_LEVEL, fmt "\n", ##__VA_ARGS__)
#define	pr_warn_once(fmt, ...)		pr_warn(fmt, ##__VA_ARGS__)
#define	pr_err_once(fmt, ...)		pr_err(fmt, ##__VA_ARGS__)
#define	pr_debug(fmt, ...)		do { } while (0)
#define	pr_cont(fmt, ...)		lkpi_printf(ULOG_INFO_LEVEL, fmt, ##__VA_ARGS__)

/*
 * [LNX] include/linux/net.h:313: `int net_ratelimit(void);` - the gate a driver
 * checks before an unthrottled message (the interface pairs it with its
 * net_ratelimited_function() macro). Its home header is linux/net.h, which this
 * face does not carry; the compiled set reaches it through the core contract, so
 * the declaration is stated here with the logging vocabulary it gates. The body
 * (linuxkpi_compat.c) is the port's rate limiter.
 */
int net_ratelimit(void);

static __always_inline __unused const char *
dev_name(const struct device *dev)
{
	return dev == NULL ? "none" : dev->name;
}

#define	dev_coredumpv(dev, data, size, gfp) \
	do { (void)(dev); (void)(data); (void)(size); } while (0)
#define	dev_coredumpm(dev, owner, data, size, gfp, read, free) \
	do { (void)(dev); (void)(data); (void)(size); } while (0)

/* ------------------------------------------------------------------ */
/* module plumbing (built into the image, not loadable modules)         */
/* ------------------------------------------------------------------ */

#define	THIS_MODULE		NULL
#define	EXPORT_SYMBOL(x)
#define	EXPORT_SYMBOL_GPL(x)
#define	MODULE_AUTHOR(x)
#define	MODULE_DESCRIPTION(x)
#define	MODULE_LICENSE(x)
#define	MODULE_FIRMWARE(x)
#define	MODULE_PARM_DESC(x, y)
#define	module_param_named(a, b, c, d)
#define	module_param(a, b, c)
#define	MODULE_DEVICE_TABLE(t, n)

/*
 * module_exit(fn): the interface records the function as an exitcall, which is
 * what makes a static exit function count as used. This port links one image
 * with no unload path, so nothing is ever called through it - but the reference
 * is kept, because dropping it leaves the driver's own static exit function
 * reading as "defined but not used" at the warning level the driver's build
 * takes (the kernel's own build has no such diagnostic, since its __exitcall
 * references the function). No exit section and no walk: registering one would
 * be a second mechanism for something the image's lifetime already answers.
 */
#define	module_exit(fn)							\
	static __used void (*__lkpi_exit_##fn)(void) = (fn)

/*
 * module_init(fn): the entry point lands in the .lkpi_init section and
 * lkpi_module_init_run() (linuxkpi_core.c) runs the entries in section order,
 * once. The section is kept apart from .lkpi_module - the usb_driver registry
 * below - so the two walks cannot disturb each other's byte layout: each
 * section is bracketed by its own pair of symbols and holds its own kind of
 * entry. A driver whose entry point is static and only reachable through this
 * macro (the vendored SDIO drivers register that way) is registered by the walk.
 */
typedef int (*lkpi_init_fn_t)(void);

#define	module_init(fn)							\
	static __used __section(".lkpi_init")				\
	lkpi_init_fn_t __lkpi_init_##fn = (fn)

/*
 * module_usb_driver(drv): the struct usb_driver pointer lands in the
 * .lkpi_module section; lkpi_usb_init() walks the section (the linker
 * script brackets it, the same way usbh_initialize() brackets the class
 * table).
 */
struct usb_driver;
#define	module_usb_driver(__usb_driver)					\
	static __used __section(".lkpi_module")				\
	void *__lkpi_module_##__usb_driver = &(__usb_driver)

/*
 * Run the .lkpi_init entries in section order, once. The image's start-up calls
 * this after the platform and the buses are up, so an entry that registers a
 * driver runs against a live system (the order the kernel's module loader gives
 * its initcalls). Idempotent; a link without the section says so and returns.
 */
void	lkpi_module_init_run(void);

/* ------------------------------------------------------------------ */
/* bugs and assertions                                                 */
/* ------------------------------------------------------------------ */

#define	WARN_ON(cond)		({					\
	bool __w = (cond);						\
	if (__w)							\
		lkpi_printf(ULOG_WARNING_LEVEL, "%s:%d: WARN_ON(%s)\n", __FILE__, __LINE__, #cond); \
	__w;								\
})
#define	WARN_ON_ONCE(cond)	WARN_ON(cond)
#define	WARN_ONCE(cond, fmt, ...)	WARN_ON(cond)
#define	WARN(cond, fmt, ...)	WARN_ON(cond)
#define	BUG_ON(cond)		do { if (cond) lkpi_fatal("BUG_ON(" #cond ")"); } while (0)
/* print_hex_dump prefix args ([LNX] kernel.h) */
#define	DUMP_PREFIX_NONE	0
#define	DUMP_PREFIX_OFFSET	1
#define	DUMP_PREFIX_ADDRESS	2

/* Real compile-time traps (2026-10-05: the old no-op silently disabled
 * the drivers' own size gates - e.g. rtw89 pci.h "BUILD_BUG_ON(sizeof
 * (struct rtw89_pci_rx_info) > sizeof(skb->cb))" compiled to nothing).
 * Upstream shape ([LNX] build_bug.h + compiler.h): an extern function
 * carrying __attribute__((error(msg))) is called only when the optimizer
 * can prove the condition true - a compile (or at worst link) kill.
 * Unlike _Static_assert this also folds const-table reads that are not
 * integer constant expressions (phy.c's rtw89_rs_idx_num_ax checks,
 * -std=c11).  Non-constant conditions leave an undefined reference and
 * fail the link - the upstream contract (BUILD_BUG_ON wants constants),
 * and still never the old silent no-op. */
#define	lkci_cat_(a, b)		a##b
#define	lkci_cat(a, b)		lkci_cat_(a, b)
#define	__lkci_assert(cond, msg, suffix)				\
	do {								\
		bool __lkci_c = (cond);					\
		extern void lkci_cat(lkci_fail_, suffix)(void)		\
		    __attribute__((error(msg)));			\
		if (!__lkci_c)						\
			lkci_cat(lkci_fail_, suffix)();			\
	} while (0)
#define	lkpi_compiletime_assert(cond, msg)				\
	__lkci_assert(cond, msg, __COUNTER__)
#define	BUILD_BUG_ON_MSG(cond, msg)					\
	lkpi_compiletime_assert(!(cond), msg)
#define	BUILD_BUG_ON(cond)	BUILD_BUG_ON_MSG(cond, "BUILD_BUG_ON(" #cond ") failed")
/* negative bitfield width when e holds - usable in constant contexts
 * (array sizes, wrappers); like upstream, needs an integer constant
 * expression */
#define	BUILD_BUG_ON_ZERO(e)	((int)sizeof(struct {			\
	int:(-!!(e));							\
}))
#define	might_sleep()		do { } while (0)
#define	might_sleep_if(cond)	do { } while (0)
/* [LNX] include/linux/lockdep.h: the validator-off arm of lockdep_assert_held()
 * still names its argument (`do { (void)(l); } while (0)`), which is what keeps a
 * lock pointer that is only read by the assertion from reading as an unused
 * variable at the driver's warning level. */
#define	lockdep_assert_held(l)		do { (void)(l); } while (0)
#define	lockdep_assert_held_read(l)	do { (void)(l); } while (0)
#define	lockdep_assert_held_write(l)	do { (void)(l); } while (0)
#define	DEBUG_LOCKS_WARN_ON(c)	0

static __always_inline __unused void
dump_stack(void)
{
}

/* ------------------------------------------------------------------ */
/* locking: mutex + spinlock (opaque storage, functions in the impl)    */
/* ------------------------------------------------------------------ */

/* 64 bytes = the port mutex shell's exact size; verified in the impl. */
struct mutex {
	void	*_storage[8];
};

void lkpi_mutex_init(struct mutex *m);
void lkpi_mutex_destroy(struct mutex *m);
void lkpi_mutex_lock(struct mutex *m);
void lkpi_mutex_unlock(struct mutex *m);
int lkpi_mutex_trylock(struct mutex *m);
int lkpi_mutex_is_locked(struct mutex *m);

#define	mutex_init(m)			lkpi_mutex_init(m)
#define	mutex_destroy(m)		lkpi_mutex_destroy(m)
#define	mutex_lock(m)			lkpi_mutex_lock(m)
#define	mutex_unlock(m)			lkpi_mutex_unlock(m)
#define	mutex_trylock(m)		lkpi_mutex_trylock(m)
#define	mutex_is_locked(m)		lkpi_mutex_is_locked(m)

/*
 * Spinlocks are DAIF critical sections, NOT mutexes (2026-10-05 W3: the
 * old passive-mutex mapping made every skb-queue head own a HEAP
 * allocation - rtw89's per-frame txcb ack queue then created and leaked
 * a ThreadX mutex object per TX frame, and the urb-completion mutex
 * churned a malloc/free per EP0 transfer, right through the drv-start
 * heap assert; board trail = osMutexDelete/lkpi_kmem_free/lkpi_skb_free
 * cycle).  On this single-core port masking IRQs IS the whole mutual
 * exclusion: no interrupt can fire, so no other thread can be scheduled
 * into the section - which also makes it safe from completion contexts
 * where a blocking mutex was always wrong.  The
 * no-flags pair parks the saved DAIF in the lock itself (the owner
 * cannot be preempted while holding it, so the slot has a single
 * writer); the irqsave pair carries it in the caller's flags, which
 * nests across different locks.  Sleeping under a spinlock stays as
 * forbidden as upstream - blocking inside would freeze the machine
 * instead of deadlocking, and the drivers never do it.
 */
typedef struct {
	unsigned long long	_daif;
} spinlock_t;

void lkpi_spin_lock(spinlock_t *l);
void lkpi_spin_unlock(spinlock_t *l);
unsigned long long lkpi_spin_lock_irqsave(spinlock_t *l);
void lkpi_spin_unlock_irqrestore(spinlock_t *l, unsigned long long flags);
int lkpi_spin_trylock(spinlock_t *l);

#define	spin_lock_init(l)		((void)((l)->_daif = 0))
/* [LNX] include/linux/spinlock_types.h: the static-initialiser spelling; the port's
 * spinlock is a passive mutex created lazily from zeroed storage, so the zeroed
 * object is the initialised one. */
#define	DEFINE_SPINLOCK(x)		spinlock_t x
#define	spin_lock_destroy(l)		do { } while (0)
#define	spin_lock(l)			lkpi_spin_lock(l)
#define	spin_unlock(l)			lkpi_spin_unlock(l)
#define	spin_lock_bh(l)			spin_lock(l)
#define	spin_unlock_bh(l)		spin_unlock(l)
#define	spin_lock_irqsave(l, flags)					\
	do { (flags) = lkpi_spin_lock_irqsave(l); } while (0)
#define	spin_unlock_irqrestore(l, flags)				\
	do { lkpi_spin_unlock_irqrestore((l), (flags));			\
	     (void)(flags); } while (0)
#define	spin_lock_irq(l)		spin_lock(l)
#define	spin_unlock_irq(l)		spin_unlock(l)
/* [LNX] include/linux/spinlock.h:362-366: `spin_lock_nested(lock, subclass)` is the
 * plain take with a lockdep subclass annotation naming a nested acquisition order.
 * This port's spinlock is an interrupt-masking section and it runs no lock
 * validator, so there is no subclass to record and the take is the plain lock; the
 * subclass argument is carried (unused) so a caller reads unchanged. */
#define	spin_lock_nested(l, subclass)	do { spin_lock(l); } while (0)
/* on this UP port a running thread cannot observe a taken spinlock: the
 * holder masked the interrupts and cannot have been preempted - so a
 * trylock always succeeds (recursive take is invalid upstream too) */
#define	spin_trylock(l)			lkpi_spin_trylock(l)
#define	spin_trylock_bh(l)		spin_trylock(l)
#define	spin_is_locked(l)		1
#define	spin_unlock_wait(l)		do { } while (0)
#define	assert_spin_locked(l)		do { } while (0)

/*
 * cleanup-attribute scope guards (linux/cleanup.h): rtw89 names exactly
 * two - `guard(rcu)()` (2 sites, core.c) and
 * `scoped_guard(spinlock_irqsave, &lock) { ... }` (3 sites, mac.c/mac.h).
 * Transcribe upstream's CLASS()/__scoped_guard() expansion (it is just
 * the for-loop idiom) plus the two concrete classes; conditional-lock
 * variants are not needed (no _try guard sites in the compile set).
 */
#ifndef __cleanup
#define	__cleanup(func)		__attribute__((__cleanup__(func)))
#endif
#ifndef __maybe_unused
#define	__maybe_unused		__attribute__((__unused__))
#endif
#define	__no_context_analysis
#define	__PASTE(a, b)		___PASTE(a, b)
#define	___PASTE(a, b)		a##b
#define	__UNIQUE_ID(prefix)	__PASTE(__PASTE(__UNIQUE_ID_, prefix), __COUNTER__)

#define	CLASS(_name, var)						\
	class_##_name##_t var __cleanup(class_##_name##_destructor) =	\
		class_##_name##_constructor
#define	guard(_name)		CLASS(_name, __UNIQUE_ID(guard))
#define	__guard_ptr(_name)	class_##_name##_lock_ptr
#define	__is_cond_ptr(_name)	class_##_name##_is_conditional
#define	__scoped_guard(_name, _label, args...)				\
	for (CLASS(_name, scope)(args);					\
	     __guard_ptr(_name)(&scope) || !__is_cond_ptr(_name);	\
	     ({ goto _label; }))					\
		if (0) {						\
_label:									\
			break;						\
		} else
#define	scoped_guard(_name, args...)					\
	__scoped_guard(_name, __UNIQUE_ID(label), args)

/* class rcu: rcu_read_lock()/unlock() */
typedef int class_rcu_t;
static __always_inline __unused class_rcu_t
class_rcu_constructor(void)
{
	rcu_read_lock();
	return 0;
}

static __always_inline __unused void
class_rcu_destructor(class_rcu_t *g)
{
	(void)g;
	rcu_read_unlock();
}

#define	class_rcu_is_conditional	false
static __always_inline __unused void *
class_rcu_lock_ptr(class_rcu_t *g)
{
	(void)g;
	return (void *)1;	/* DEFINE_CLASS_IS_GUARD's unconditional marker */
}

/* class spinlock_irqsave: the shim spinlock is a passive mutex, so the
 * "irqsave" flavor is lock + flags==0 (see the spin_* macros above) */
typedef struct {
	spinlock_t	*lock;
	unsigned long	flags;
} class_spinlock_irqsave_t;

static __always_inline __unused class_spinlock_irqsave_t
class_spinlock_irqsave_constructor(spinlock_t *l)
{
	class_spinlock_irqsave_t t = { .lock = l };

	spin_lock_irqsave(t.lock, t.flags);
	return t;
}

static __always_inline __unused void
class_spinlock_irqsave_destructor(class_spinlock_irqsave_t *t)
{
	spin_unlock_irqrestore(t->lock, t->flags);
}

#define	class_spinlock_irqsave_is_conditional	false
static __always_inline __unused void *
class_spinlock_irqsave_lock_ptr(class_spinlock_irqsave_t *t)
{
	(void)t;
	return (void *)1;
}

typedef unsigned long irqflags_t;
#define	local_irq_save(flags)		do { (flags) = 0; } while (0)
#define	local_irq_restore(flags)	do { (void)(flags); } while (0)
#define	local_bh_disable()		do { } while (0)
#define	local_bh_enable()		do { } while (0)
#define	in_interrupt()			0
#define	in_atomic()			0
#define	in_softirq()			0
#define	IRQ_NONE			0
#define	IRQ_HANDLED			1
/*
 * The deferred callback of the interrupt face. The object (struct tasklet_struct)
 * and its remaining entry points are the face header's (linux/interrupt.h); the
 * mechanism is this file's deferred-work seam - a tasklet is a coalescing work
 * item and the worker thread is the softirq context. The body is in linuxkpi_core.c
 * ("deferred callbacks"), because that is where the seam it stands on lives.
 */
struct tasklet_struct;
void lkpi_tasklet_schedule(struct tasklet_struct *t);
#define	tasklet_schedule(t)		lkpi_tasklet_schedule(t)

/* ------------------------------------------------------------------ */
/* sk_buff (linux/skbuff.h) - data shape, operations in the impl        */
/* struct sk_buff, NET_SKB_PAD and the pool-backed allocator family now
 * live in skbuff/skbuff_pool.h (TizenRT-lineage dual static pool,
 * [lkpi-stabilize] FR-1); this header includes it so every existing
 * consumer keeps compiling unchanged. */
#include "skbuff/skbuff_pool.h"

struct firmware;
struct pci_dev;
/* ------------------------------------------------------------------ */

struct sk_buff_head {
	struct sk_buff	*next;
	struct sk_buff	*prev;
	spinlock_t	lock;
	u32		qlen;
};

void lkpi_skb_queue_head_init_lockable(struct sk_buff_head *q);

void lkpi_skb_gen_report(void);
void lkpi_skb_reserve(struct sk_buff *skb, unsigned int len);
/* [S9b W3b probe] geometry violations so far (put/push/reserve) */
unsigned int lkpi_skb_geo_bad_count(void);
/* init-time (probe): reset + init the queue lock */
void lkpi_skb_queue_init(struct sk_buff_head *);
/* live/stack queues: reset the list shape, NEVER touch the lock
 * (re-arming a mutex leaked a ThreadX object per call) */
void lkpi_skb_queue_reset(struct sk_buff_head *);
/* public ops: take q->lock around the whole operation (Linux's
 * skb_* locking contract - drivers call them bare from any thread) */
void lkpi_skb_queue_tail(struct sk_buff_head *, struct sk_buff *);
void lkpi_skb_queue_head(struct sk_buff_head *, struct sk_buff *);
struct sk_buff *lkpi_skb_dequeue(struct sk_buff_head *);
void lkpi_skb_unlink(struct sk_buff *, struct sk_buff_head *);
void lkpi_skb_queue_purge(struct sk_buff_head *);
/* raw surgery: the __skb_* contract - caller holds q->lock or owns the
 * queue (rtw89 fw.c c2h splice sites, rtw88 usb.c aggregation) */
void lkpi_skb_do_queue_tail(struct sk_buff_head *, struct sk_buff *);
void lkpi_skb_do_queue_head(struct sk_buff_head *, struct sk_buff *);
struct sk_buff *lkpi_skb_do_dequeue(struct sk_buff_head *);
void lkpi_skb_do_unlink(struct sk_buff *, struct sk_buff_head *);

/* the dev_alloc_skb split ([LNX] skbuff.c:600): +NET_SKB_PAD reserve,
 * so a later push on the returned skb has the headroom Linux promises.
 * alloc_skb stays the raw zero-headroom form.  Both prototypes live in
 * skbuff/skbuff_pool.h now (pool-backed, [lkpi-stabilize] FR-1). */
#define	alloc_skb(size, gfp)		lkpi_skb_alloc((size), (gfp))
#define	__dev_alloc_skb(size, gfp)	lkpi_dev_alloc_skb((size), (gfp))
#define	dev_alloc_skb(size)		lkpi_dev_alloc_skb((size), GFP_ATOMIC)
#define	kfree_skb(skb)			lkpi_skb_free(skb)
#define	kfree_skb_any(skb)		lkpi_skb_free(skb)
#define	dev_kfree_skb(skb)		lkpi_skb_free(skb)
#define	dev_kfree_skb_any(skb)		lkpi_skb_free(skb)
#define	dev_kfree_skb_irq(skb)		lkpi_skb_free(skb)
#define	consume_skb(skb)		lkpi_skb_free(skb)

#define	skb_queue_head_init(q)		lkpi_skb_queue_init(q)
#define	skb_queue_head_init_lockable(q)	lkpi_skb_queue_head_init_lockable(q)
/* live/stack queues: reset shape only, lock preserved */
#define	__skb_queue_head_init(q)	lkpi_skb_queue_reset(q)
#define	skb_queue_tail(q, skb)		lkpi_skb_queue_tail((q), (skb))
/* the __ variants are the raw contract: caller holds q->lock or owns
 * the queue (Linux semantics - rtw89 fw.c c2h, rtw88 usb.c aggregation) */
#define	__skb_queue_tail(q, skb)	lkpi_skb_do_queue_tail((q), (skb))
#define	skb_queue_head(q, skb)		lkpi_skb_queue_head((q), (skb))
#define	__skb_queue_head(q, skb)	lkpi_skb_do_queue_head((q), (skb))
#define	skb_dequeue(q)			lkpi_skb_dequeue(q)
#define	__skb_dequeue(q)		lkpi_skb_do_dequeue(q)
#define	skb_unlink(skb, q)		lkpi_skb_unlink((skb), (q))
#define	__skb_unlink(skb, q)		lkpi_skb_do_unlink((skb), (q))
#define	skb_queue_purge(q)		lkpi_skb_queue_purge(q)

/* skb_trim: shrink len (linear skbs only on this image) */
static __always_inline __unused void
skb_trim(struct sk_buff *skb, unsigned int len)
{
	if (skb->len > len) {
		skb->len = len;
		skb->tail = (unsigned int)(skb->data - skb->head) + len;
	}
}

/* pskb_expand_head: no head/tail copies exist here (single linear
 * buffer); growing the tail would need a reallocation, which the rtw89
 * call sites only use to grow by noa_len before _skb_put.  Return the
 * failure path (-ENOMEM) so the driver takes its no-expand branch -
 * exactly what it would do on a failed allocation on Linux. */
static __always_inline __unused int
pskb_expand_head(struct sk_buff *skb, int nhead, int ntail, gfp_t gfp)
{
	(void)skb;
	(void)nhead;
	(void)ntail;
	(void)gfp;
	return -ENOMEM;
}

/* queue splice, Linux raw semantics ([LNX] skbuff.h): move ALL of list
 * onto the FRONT of head; the CALLER holds both queues' locks (the only
 * call sites, rtw89 fw.c's c2h drain/restore, do exactly that - the old
 * dequeue+append loop both changed the ordering and took the public
 * lock inside a caller-held lock section).  Moved skbs get ->list
 * re-pointed (this shim tracks it for the DOUBLE ENQUEUE / BAD UNLINK
 * diagnostics; a stale owner into the since-scoped stack queue would
 * cry wolf later).  Empty-splice is a no-op and leaves the source a
 * valid empty queue. */
static __always_inline __unused void
lkpi_skb_splice(struct sk_buff_head *list, struct sk_buff_head *head,
    int reset_src)
{
	struct sk_buff *first, *last, *at, *it;

	if (list->qlen == 0)
		return;
	first = list->next;
	last = list->prev;
	for (it = first; it != (struct sk_buff *)list; it = it->next)
		it->list = head;
	at = head->next;
	head->next = first;
	first->prev = (struct sk_buff *)head;
	last->next = at;
	at->prev = last;
	head->qlen += list->qlen;
	if (reset_src)
		lkpi_skb_queue_reset(list);
}

static __always_inline __unused void
skb_queue_splice(struct sk_buff_head *list, struct sk_buff_head *head)
{
	lkpi_skb_splice(list, head, 0);
}

static __always_inline __unused void
skb_queue_splice_init(struct sk_buff_head *list, struct sk_buff_head *head)
{
	lkpi_skb_splice(list, head, 1);
}

/* [S9 D-3 R3] Linux parity: NULL on an empty queue.  The old
 * `#define skb_peek(q) ((q)->next)` returned the sentinel on an empty
 * queue, and rtw88's aggregation loop
 * (skb_iter = skb_peek(list); if (skb_iter && ...)) then treated the
 * queue head as an skb - list surgery on the queue object itself
 * (codex audit, rank-1 heap corruptor candidate). */
static __always_inline __unused struct sk_buff *
skb_peek(const struct sk_buff_head *q)
{
	struct sk_buff *s = q->next;

	return (s == (const struct sk_buff *)q) ? NULL : s;
}

static __always_inline __unused struct sk_buff *
skb_peek_tail(const struct sk_buff_head *q)
{
	struct sk_buff *s = q->prev;

	return (s == (const struct sk_buff *)q) ? NULL : s;
}

#define	skb_queue_empty(q)		((q)->qlen == 0)
#define	skb_queue_len(q)		((q)->qlen)
#define	skb_headroom(skb)		((unsigned int)((skb)->data - (skb)->head))
#define	skb_tailroom(skb)		((int)((skb)->end - (skb)->tail))
#define	skb_priv(skb)			((void *)(skb)->cb)
#define	skb_queue_walk(q, skb)						\
	for ((skb) = (q)->next;						\
	     (skb) != (struct sk_buff *)(q);				\
	     (skb) = (skb)->next)
#define	skb_queue_walk_safe(q, skb, tmp)				\
	for ((skb) = (q)->next, (tmp) = (skb)->next;			\
	     (skb) != (struct sk_buff *)(q);				\
	     (skb) = (tmp), (tmp) = (skb)->next)

static __always_inline __unused unsigned int
skb_headlen(const struct sk_buff *skb)
{
	return skb->len - skb->data_len;
}

void lkpi_skb_reserve(struct sk_buff *skb, unsigned int len);

static __always_inline __unused void
skb_reserve(struct sk_buff *skb, unsigned int len)
{
	lkpi_skb_reserve(skb, len);
}

unsigned char *lkpi_skb_put(struct sk_buff *skb, unsigned int len);
void *lkpi_skb_push(struct sk_buff *skb, unsigned int len);

/* [LNX] include/linux/skbuff.h:2768: `void *skb_put(struct sk_buff *, unsigned
 * int)`. The interface returns void, not a byte pointer, and a driver relies on
 * it: the radiotap builders assign the result straight to a typed descriptor
 * pointer, which a byte-pointer return turns into a warning the interface's own
 * build does not have. */
static __always_inline __unused void *
skb_put(struct sk_buff *skb, unsigned int len)
{
	return lkpi_skb_put(skb, len);
}

static __always_inline __unused void *
skb_push(struct sk_buff *skb, unsigned int len)
{
	return lkpi_skb_push(skb, len);
}

static __always_inline __unused unsigned char *
skb_pull(struct sk_buff *skb, unsigned int len)
{
	/* [LNX] skb_pull (skbuff.h): "if len is larger than skb->len,
	 * return NULL and DO NOT modify the skb".  The old unconditional
	 * subtraction underflowed skb->len (unsigned) and walked data past
	 * the allocation - rtw89's mod512 branch calls it on a FRESH skb
	 * (usb.c:388, len=0 result expected), so the return-value contract
	 * matters, not just the arithmetic. */
	if (len > skb->len)
		return NULL;
	skb->len -= len;
	skb->data += len;
	return skb->data;
}

/* [LNX] include/linux/skbuff.h:2769: `static inline void *__skb_put(...)`. */
static __always_inline __unused void *
__skb_put(struct sk_buff *skb, unsigned int len)
{
	return lkpi_skb_put(skb, len);
}

static __always_inline __unused void *
skb_put_zero(struct sk_buff *skb, unsigned int len)
{
	unsigned char *p = lkpi_skb_put(skb, len);

	memset(p, 0, len);
	return p;
}

static __always_inline __unused void *
skb_put_data(struct sk_buff *skb, const void *data, unsigned int len)
{
	unsigned char *p = lkpi_skb_put(skb, len);

	memcpy(p, data, len);
	return p;
}

static __always_inline __unused void
skb_copy_to_linear_data(struct sk_buff *skb, const void *from,
    unsigned int len)
{
	memcpy(skb->data, from, len);
}

static __always_inline __unused void
skb_reset_tail_pointer(struct sk_buff *skb)
{
	skb->tail = (unsigned int)(skb->data - skb->head);
}

static __always_inline __unused struct sk_buff *
skb_copy(const struct sk_buff *skb, gfp_t gfp)
{
	struct sk_buff *n;
	unsigned int headroom = (unsigned int)(skb->data - skb->head);

	/* [LNX] skb_copy copies headroom + data + tailroom verbatim: the
	 * copy's data offset equals the source's so a later push on the
	 * copy has the same room it had on the original.  The old shape
	 * copied the source's TAIL OFFSET but left data at head - a copy
	 * of a reserved skb (data=32) came out data=0, tail=32, so the
	 * first push ate payload instead of headroom (rtw89 fw.c:9389
	 * scans the H2C template this way). */
	n = lkpi_skb_alloc(skb->end, gfp);
	if (n == NULL)
		return NULL;
	/* this shim never carries paged data (data_len 0), so tail always
	 * covers headroom + payload - copy the whole [head, head+tail) */
	memcpy(n->head, skb->head, skb->tail);
	n->data = n->head + headroom;
	n->tail = skb->tail;
	n->len = skb->len;
	n->protocol = skb->protocol;
	n->priority = skb->priority;
	memcpy(n->cb, skb->cb, sizeof(n->cb));
	return n;
}

static __always_inline __unused void
skb_reset_mac_header(struct sk_buff *skb)
{
	(void)skb;
}

static __always_inline __unused void
skb_reset_network_header(struct sk_buff *skb)
{
	(void)skb;
}

static __always_inline __unused u16
skb_get_queue_mapping(const struct sk_buff *skb)
{
	return skb->queue_mapping;
}

static __always_inline __unused void
skb_set_queue_mapping(struct sk_buff *skb, u16 q)
{
	skb->queue_mapping = q;
}

/* ------------------------------------------------------------------ */
/* completion                                                          */
/* ------------------------------------------------------------------ */

struct completion {
	void	*_mtx[8];	/* the port mutex shell's size */
	void	*_cv[12];	/* the port condvar shell's size - MUST fit
				 * lkpi_cv_shell IN THIS SLOT (linuxkpi_impl.h
				 * pins it with a per-slot _Static_assert; the
				 * rtw89 W2 board round proved a 72-byte shell
				 * in a 64-byte slot aliases `done`, and
				 * complete_all's waiter loop then reads the
				 * sticky marker as its trip count - a 63-second
				 * semaphore-put storm that held the completion
				 * mutex and froze the whole probe) */
	int	done;
};

/* complete_all marker: sticky, never consumed by wait_for_completion */
#define	LKPI_COMPLETION_STICKY	0x40000000

void lkpi_completion_init(struct completion *);
void lkpi_completion_destroy(struct completion *);
void lkpi_completion_reinit(struct completion *);
void lkpi_complete(struct completion *);
void lkpi_complete_all(struct completion *);
void lkpi_wait_for_completion(struct completion *);
unsigned long lkpi_wait_for_completion_timeout(struct completion *,
    unsigned long);
int lkpi_try_wait_for_completion(struct completion *);
int lkpi_completion_done(const struct completion *);

#define	init_completion(c)			lkpi_completion_init(c)
#define	reinit_completion(c)			lkpi_completion_reinit(c)
/* complete()/complete_all() are real functions, NOT macros: the urb's
 * ->complete callback member is called through the same identifier, and a
 * function-like macro would rewrite every urb->complete(urb) site. */
void complete(struct completion *c);
void complete_all(struct completion *c);
#define	wait_for_completion(c)			lkpi_wait_for_completion(c)
#define	wait_for_completion_timeout(c, j)	lkpi_wait_for_completion_timeout((c), (j))
#define	wait_for_completion_interruptible_timeout(c, j) \
	lkpi_wait_for_completion_timeout((c), (j))
#define	try_wait_for_completion(c)		lkpi_try_wait_for_completion(c)
#define	completion_done(c)			lkpi_completion_done(c)

/* [LNX] include/uapi/linux/stddef.h:56-60 via include/linux/stddef.h:93: the
 * flexible-array spelling usable inside a union or a group. This face is compiled by one
 * known compiler class, so the interface's arm selection is not carried and the GCC arm
 * is the one stated. */
#define	DECLARE_FLEX_ARRAY(TYPE, NAME)		\
	struct {				\
		struct { } __empty_ ## NAME;	\
		TYPE NAME[];			\
	}

/* ------------------------------------------------------------------ */
/* workqueues (linux/workqueue.h)                                      */
/* ------------------------------------------------------------------ */

struct work_struct {
	void	*_wq;
	void	(*func)(struct work_struct *);
	/* queueing state: wk_queued is CAS'd, wk_running is relaxed-stored;
	 * the queued flag must be cleared with the pending list still
	 * locked or completions get lost (the 2026-09-24 lesson) */
	int	wk_queued;
	volatile int wk_running;
} __aligned(8);

struct delayed_work {
	struct work_struct	work;
	/* the callout shell's size (TX_TIMER + fn/arg state, impl assert) */
	void			*_callout[14];
	bool			dw_scheduled;
	bool			dw_cancel;
};

#define	INIT_WORK(w, fn)		lkpi_work_init((w), (fn))
#define	INIT_DELAYED_WORK(w, fn)	lkpi_delayed_work_init((w), (fn))
#define	PREPARE_DELAYED_WORK(w, fn)	lkpi_delayed_work_init((w), (fn))
#define	INIT_DEFERRABLE_WORK(w, fn)	INIT_DELAYED_WORK((w), (fn))

struct workqueue_struct {
	void	*_wq;
};

#define	to_delayed_work(w)	container_of((w), struct delayed_work, work)

void lkpi_work_init(struct work_struct *, void (*)(struct work_struct *));
void lkpi_delayed_work_init(struct delayed_work *,
    void (*)(struct work_struct *));
bool lkpi_queue_work(struct workqueue_struct *, struct work_struct *);
/* the delay parameter must NOT be named "jiffies": the shim macro
 * (#define jiffies lkpi_jiffies()) would turn the prototype into a
 * function-pointer parameter - see lkpi_queue_delayed_work in .c */
bool lkpi_queue_delayed_work(struct workqueue_struct *,
    struct delayed_work *, unsigned long delay);
bool lkpi_cancel_delayed_work(struct delayed_work *);
bool lkpi_cancel_delayed_work_sync(struct delayed_work *);
void lkpi_flush_delayed_work(struct delayed_work *);
void lkpi_work_flush(struct work_struct *);
struct workqueue_struct *lkpi_alloc_workqueue(const char *fmt, int flags,
    int max_active);
void		*lkpi_worker_thread_id(void);
void lkpi_destroy_workqueue(struct workqueue_struct *);

extern struct workqueue_struct *lkpi_system_wq;

#define	schedule_work(w)		lkpi_queue_work(lkpi_system_wq, (w))
#define	schedule_delayed_work(w, j)	lkpi_queue_delayed_work(lkpi_system_wq, (w), (j))
/* re-arm: the shim's delayed work queue overwrite-on-requeue is
 * equivalent to mod_delayed_work's re-arm semantics here */
#define	mod_delayed_work(wq, w, j)	lkpi_queue_delayed_work((wq), (w), (j))
#define	queue_work(wq, w)		lkpi_queue_work((wq), (w))
#define	queue_delayed_work(wq, w, j)	lkpi_queue_delayed_work((wq), (w), (j))
#define	cancel_delayed_work(w)		lkpi_cancel_delayed_work(w)
#define	cancel_delayed_work_sync(w)	lkpi_cancel_delayed_work_sync(w)
#define	cancel_work_sync(w)		({ lkpi_work_flush(w); true; })
#define	flush_work(w)			lkpi_work_flush(w)
#define	flush_workqueue(wq)		do { (void)(wq); } while (0)
#define	flush_delayed_work(w)		lkpi_flush_delayed_work(w)
#define	destroy_workqueue(wq)		lkpi_destroy_workqueue(wq)

static __always_inline __unused struct workqueue_struct *
alloc_workqueue(const char *fmt, unsigned int flags, int max_active, ...)
{

	return lkpi_alloc_workqueue(fmt, (int)flags, max_active);
}

#define	create_singlethread_workqueue(name)	alloc_workqueue(name, 0, 1)
#define	create_workqueue(name)			alloc_workqueue(name, 0, 1)
#define	system_wq				lkpi_system_wq
/* [LNX] include/linux/workqueue.h: the default-flags system workqueue. This port
 * runs one system workqueue, so both spellings reach it. */
#define	system_dfl_wq				lkpi_system_wq
/* [LNX] include/linux/workqueue.h:469: the unbound system workqueue. This port runs
 * one system workqueue and it is not CPU-bound, so the unbound spelling reaches
 * the same queue. */
#define	system_unbound_wq			lkpi_system_wq
/* [LNX] include/linux/workqueue.h: the per-CPU system workqueue; this port runs one
 * system workqueue on its single CPU, so the per-CPU spelling reaches it too. */
#define	system_percpu_wq			lkpi_system_wq

/* [LNX] include/linux/workqueue.h:371-416 (the enumerators the compiled set names, at
 * their computed positions) */
enum wq_flags {
	WQ_UNBOUND = 2,
	WQ_HIGHPRI = 16
};

/* ------------------------------------------------------------------ */
/* timers (linux/timer.h)                                              */
/* ------------------------------------------------------------------ */

struct timer_list {
	/* the callout shell's size (TX_TIMER + fn/arg state, impl assert) */
	void	*_callout[14];
	void	(*tl_func)(struct timer_list *);
	bool	tl_pending;
	/* [LNX] include/linux/timer.h: the expiry a compiled consumer reads off a
	 * timer it armed. */
	unsigned long	expires;
};

void lkpi_timer_setup(struct timer_list *, void (*)(struct timer_list *));
int lkpi_mod_timer(struct timer_list *, unsigned long expires);
/* The delete pair returns whether the timer was still pending when the call arrived -
 * the interface's own contract for del_timer()/del_timer_sync() and for the
 * timer_delete()/timer_delete_sync() spellings it renamed them to, and a caller that
 * distinguishes "cancelled a live timer" from "it had already fired" reads it (the
 * vendored vendor driver's cancel path does). */
int lkpi_del_timer(struct timer_list *);
int lkpi_del_timer_sync(struct timer_list *);
int lkpi_timer_pending(const struct timer_list *);

#define	timer_setup(t, fn, flags)	lkpi_timer_setup((t), (fn))
#define	setup_timer(t, fn, arg)		lkpi_timer_setup((t), (fn))
#define	mod_timer(t, expires)		lkpi_mod_timer((t), (expires))
#define	del_timer(t)			lkpi_del_timer(t)
#define	del_timer_sync(t)		lkpi_del_timer_sync(t)
#define	timer_pending(t)		lkpi_timer_pending(t)
#define	timer_delete(t)			lkpi_del_timer(t)
#define	timer_delete_sync(t)		lkpi_del_timer_sync(t)
/* [LNX] include/linux/timer.h: the shutdown-sync form stops the timer and leaves
 * it unable to re-arm; this port's callout has no re-arm flag to clear once it is
 * deleted, so the delete-sync contract is the whole of it. */
#define	timer_shutdown_sync(t)		lkpi_del_timer_sync(t)
#define	from_timer(var, timer, field)	container_of(timer, typeof(*var), field)
/* 6.15+ rename of from_timer */
#define	timer_container_of(var, timer, field)	container_of(timer, typeof(*var), field)

/* ------------------------------------------------------------------ */
/* wait queues (only the coexistence code waits on one)                */
/* ------------------------------------------------------------------ */

struct wait_queue_head {
	void	*_mtx[8];
	/* see struct completion._cv: the slot must hold lkpi_cv_shell
	 * (the 72-byte shell in this 64-byte slot wrote its `waiters`
	 * 4 bytes PAST the whole wait_queue_head - a silent out-of-object
	 * store on every timedwait) */
	void	*_cv[12];
};

typedef struct wait_queue_head wait_queue_head_t;

void lkpi_waitqueue_init(struct wait_queue_head *wq);
void lkpi_waitqueue_wake(struct wait_queue_head *);
void lkpi_waitq_lock(struct wait_queue_head *);
void lkpi_waitq_unlock(struct wait_queue_head *);
int lkpi_waitq_timedwait(struct wait_queue_head *, int ms);

#define	init_waitqueue_head(wq)		lkpi_waitqueue_init(wq)
#define	wake_up(wq)			lkpi_waitqueue_wake(wq)
#define	wake_up_interruptible(wq)	lkpi_waitqueue_wake(wq)
#define	wake_up_all(wq)			lkpi_waitqueue_wake(wq)

/*
 * Returns 1 when the condition became true, 0 on timeout - the interface's
 * remaining-jiffies result read the only way this port's callers read it.
 *
 * The budget is a deadline on the port's own millisecond clock, not a count of
 * wake-ups: the condition is re-checked after every wake, a wake-up meant for
 * another waiter does not re-arm the full timeout, and a waiter whose wake was
 * missed still leaves at the deadline. The deadline is what makes the mapping
 * safe to state plainly - the previous form mapped lkpi_waitq_timedwait's own
 * result (1 is "woken") the wrong way round, so a satisfied condition read as a
 * timeout and a real timeout re-armed the whole budget and looped. Its first
 * live consumer was the vendored driver's firmware-chunk wait
 * (third-party/iwlwifi/pcie/gen1_2/trans.c:646), which reported
 * "Failed to load firmware chunk!" after 5 ms against a 5 s wait while the
 * device had in fact completed the transfer.
 */
#define	wait_event_timeout(wq, cond, timeout)				\
({									\
	int __ms = (int)jiffies_to_msecs(timeout);			\
	unsigned long __deadline = jiffies + (unsigned long)__ms;	\
	int __ret;							\
	lkpi_waitq_lock(&(wq));						\
	while (!(cond)) {						\
		long __left = (long)(__deadline - jiffies);		\
		if (__left <= 0 ||					\
		    !lkpi_waitq_timedwait(&(wq), (int)__left))		\
			break;						\
	}								\
	__ret = (cond) ? 1 : 0;						\
	lkpi_waitq_unlock(&(wq));					\
	__ret;								\
})

#define	wait_event(wq, cond)						\
	do {								\
		lkpi_waitq_lock(&(wq));					\
		while (!(cond))						\
			(void)lkpi_waitq_timedwait(&(wq), 1000);	\
		lkpi_waitq_unlock(&(wq));				\
	} while (0)

/* ------------------------------------------------------------------ */
/* usb_anchor (urb.c:812 family; rtl8xxxu embeds three in its softc;    */
/* implementation in linuxkpi_usb.c (the USB face's own entry)           */
/* ------------------------------------------------------------------ */

struct urb;

struct usb_anchor {
	struct list_head	urb_list;
	wait_queue_head_t	wait;
	spinlock_t		lock;
	unsigned char		killed;
	unsigned char		suspended;
};

void init_usb_anchor(struct usb_anchor *anchor);
void usb_anchor_urb(struct urb *urb, struct usb_anchor *anchor);
void usb_unanchor_urb(struct urb *urb);
void usb_kill_anchored_urbs(struct usb_anchor *anchor);

/* ------------------------------------------------------------------ */
/* misc kernel helpers                                                 */
/* ------------------------------------------------------------------ */

int scnprintf(char *buf, size_t size, const char *fmt, ...);

static __always_inline __unused char *
strscpy(char *dst, const char *src, size_t len)
{
	size_t srclen = strlen(src);

	if (len != 0) {
		size_t n = srclen < len - 1 ? srclen : len - 1;

		memcpy(dst, src, n);
		dst[n] = '\0';
	}
	return dst;
}

/*
 * [LNX] include/linux/string.h:126-146: strscpy_pad(). The interface spells it as a
 * macro chain that dispatches on the argument count; this port's compiled set names
 * only the explicit-count form, so that one is stated here, over strscpy() above,
 * with the interface's semantics: the destination is always NUL-terminated and the
 * tail is zero-filled, and a truncation answers -E2BIG.
 */
static __always_inline __unused ssize_t
strscpy_pad(char *dst, const char *src, size_t count)
{
	size_t srclen = strlen(src);
	size_t n;

	if (count == 0)
		return -E2BIG;
	n = srclen < count - 1 ? srclen : count - 1;
	memcpy(dst, src, n);
	for (; n < count; n++)
		dst[n] = '\0';

	return srclen < count ? (ssize_t)srclen : -E2BIG;
}

static __always_inline __unused void *
memset_io(void *addr, int val, size_t len)
{
	return memset(addr, val, len);
}

static __always_inline __unused void
memcpy_fromio(void *dst, const volatile void *src, size_t len)
{
	memcpy(dst, (const void *)(uintptr_t)src, len);
}

static __always_inline __unused void
memcpy_toio(volatile void *dst, const void *src, size_t len)
{
	memcpy((void *)(uintptr_t)dst, src, len);
}

int lkpi_strcasecmp(const char *, const char *);
char *lkpi_strnstr(const char *, const char *, size_t);
unsigned long simple_strtoul(const char *, char **, unsigned int base);
int kstrtou8(const char *s, unsigned int base, u8 *res);
int kstrtou16(const char *s, unsigned int base, u16 *res);
int kstrtouint(const char *s, unsigned int base, unsigned int *res);
int kstrtobool(const char *s, bool *res);


/* ------------------------------------------------------------------ */
/* LED class types: main.h's own <linux/leds.h> include is version-     */
/* gated (< 5.14), and the drivers need the shapes at struct level, so  */
/* they ride the core (the register/unregister calls are no-ops - the   */
/* image has no LED subsystem).                                         */
/* ------------------------------------------------------------------ */

#ifndef _LINUXKPI_LINUX_LEDS_TYPES_
#define _LINUXKPI_LINUX_LEDS_TYPES_

enum led_brightness {
	LED_OFF		= 0,
	LED_ON		= 1,
	LED_HALF	= 127,
	LED_FULL	= 255,
};

struct led_classdev {
	const char		*name;
	const char		*default_trigger;
	enum led_brightness	 brightness;
	enum led_brightness	 max_brightness;
	int			 flags;
	void			(*brightness_set)(struct led_classdev *,
				    enum led_brightness);
	int			(*brightness_set_blocking)(struct led_classdev *,
				    enum led_brightness);
};

struct led_trigger;

/* the throughput-blink table type (tpt trigger; never fires here) */
struct ieee80211_tpt_blink {
	u64	throughput;
	u32	blink_time;
};

#define	IEEE80211_TPT_LEDTRIG_FL_RADIO		BIT(0)
#define	IEEE80211_TPT_LEDTRIG_FL_WORK		BIT(1)
#define	IEEE80211_TPT_LEDTRIG_FL_CONNECTED	BIT(2)

#define	led_classdev_register(dev, cdev)	0
#define	led_classdev_unregister(cdev)		do { (void)(cdev); } while (0)

#endif /* _LINUXKPI_LINUX_LEDS_TYPES_ */

/* [LNX] helper the compiled set names */
/* [LNX] include/linux/netdevice.h:5034 */
struct net_device *alloc_netdev_dummy(int sizeof_priv);

/* [LNX] helper the compiled set names */
/* [LNX] include/linux/firmware.h:105 */
int firmware_request_nowarn(const struct firmware **fw, const char *name,
			    struct device *device);

void get_random_mask_addr(u8 *buf, const u8 *addr, const u8 *mask);



/* [LNX] helper the compiled set names */
/* [LNX] include/linux/slab.h:1159 */
/* [LNX] include/linux/gfp.h:17-18, include/linux/overflow.h:452-453, and
 * include/linux/slab.h:1106-1114: the flexible-family allocator the compiled set calls.
 * The interface writes the trailing element count through __set_flex_counter(), which is
 * keyed on the __counted_by annotation; this port's compiler has no such attribute, so
 * the count store is the caller's - stated here rather than silently dropped, because
 * the value a caller reads back through the counter member differs. The size arithmetic
 * is the interface's own. */
/* [LNX] include/linux/slab.h:1096-1100: the object-array arm of the same family */
#define	__alloc_objs(KMALLOC, GFP, TYPE, COUNT)				\
({									\
	const size_t __obj_size = (size_t)sizeof(TYPE) * (size_t)(COUNT); \
	(TYPE *)KMALLOC(__obj_size, GFP);				\
})

/* Two spellings of the object form are in use across the compiled set: one passes a
 * pointer expression (the newer spelling) and one passes the type itself (the older one).
 * Both are arity-correct under one mechanism here: the size is taken with sizeof(), which
 * accepts a type name and a dereferenced pointer alike, and the optional trailing GFP
 * argument is selected by arity - no comma-paste, so the strict mode this port compiles in
 * cannot change the pick. */
#define	__lkpi_obj_pick(_1, _2, NAME, ...)	NAME
#define	__lkpi_obj_pick3(_1, _2, _3, NAME, ...)	NAME
#define	__lkpi_obj_pick4(_1, _2, _3, _4, NAME, ...)	NAME
#define	__lkpi_kmalloc_obj1(T)			kmalloc(sizeof(T), GFP_KERNEL)
#define	__lkpi_kmalloc_obj2(T, GFP)		kmalloc(sizeof(T), (GFP))
#define	__lkpi_kzalloc_obj1(T)			kzalloc(sizeof(T), GFP_KERNEL)
#define	__lkpi_kzalloc_obj2(T, GFP)		kzalloc(sizeof(T), (GFP))
#define	kmalloc_obj(...)							\
	__lkpi_obj_pick(__VA_ARGS__, __lkpi_kmalloc_obj2, __lkpi_kmalloc_obj1)(__VA_ARGS__)
#define	kzalloc_obj(...)							\
	__lkpi_obj_pick(__VA_ARGS__, __lkpi_kzalloc_obj2, __lkpi_kzalloc_obj1)(__VA_ARGS__)
/* The array form carries an optional trailing GFP argument the same way the
 * object form does; three positions are needed because the two mandatory
 * arguments precede it. */
#define	__lkpi_kzalloc_objs1(P, COUNT)		kzalloc(sizeof(P) * (COUNT), GFP_KERNEL)
#define	__lkpi_kzalloc_objs2(P, COUNT, GFP)	kzalloc(sizeof(P) * (COUNT), (GFP))
#define	__lkpi_kmalloc_objs1(P, COUNT)		kmalloc(sizeof(P) * (COUNT), GFP_KERNEL)
#define	__lkpi_kmalloc_objs2(P, COUNT, GFP)	kmalloc(sizeof(P) * (COUNT), (GFP))
#define	kzalloc_objs(...)							\
	__lkpi_obj_pick3(__VA_ARGS__, __lkpi_kzalloc_objs2, __lkpi_kzalloc_objs1)(__VA_ARGS__)
#define	kmalloc_objs(...)							\
	__lkpi_obj_pick3(__VA_ARGS__, __lkpi_kmalloc_objs2, __lkpi_kmalloc_objs1)(__VA_ARGS__)

#define	__alloc_flex(KMALLOC, GFP, TYPE, FAM, COUNT)			\
({									\
	const size_t __count = (COUNT);					\
	const size_t __obj_size = struct_size_t(TYPE, FAM, __count);	\
	TYPE *__obj_ptr = KMALLOC(__obj_size, GFP);			\
	__obj_ptr;							\
})

#define	kzalloc_flex(...)							\
	__lkpi_obj_pick4(__VA_ARGS__, __lkpi_kzalloc_flex2, __lkpi_kzalloc_flex1)(__VA_ARGS__)
#define	__lkpi_kzalloc_flex1(P, FAM, COUNT)					\
	__alloc_flex(kzalloc, GFP_KERNEL, typeof(P), FAM, COUNT)
#define	__lkpi_kzalloc_flex2(P, FAM, COUNT, GFP)				\
	__alloc_flex(kzalloc, (GFP), typeof(P), FAM, COUNT)

/* [LNX] helper the compiled set names */
/* [LNX] include/linux/string.h:281 */
void *memchr_inv(const void *s, int c, size_t n);

/* [LNX] helper the compiled set names */
#endif /* LINUXKPI_CORE_H */
