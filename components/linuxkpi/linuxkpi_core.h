/* SPDX-License-Identifier: BSD-2-Clause */
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

/* -std=c11 has no typeof keyword; the GNU spelling always exists. */
#ifndef typeof
#define	typeof(x)	__typeof__(x)
/* [LNX] include/linux/random.h:39 */
void get_random_bytes(void *buf, size_t len);

/* [LNX] include/asm-generic/barrier.h:196 */
void barrier(void);

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

typedef uint8_t			u8;
typedef uint16_t		u16;
typedef uint32_t		u32;
typedef uint64_t		u64;
typedef int8_t			s8;
typedef int16_t			s16;
typedef int32_t			s32;
typedef int64_t			s64;

typedef uint8_t			__u8;
typedef uint16_t		__u16;
typedef uint32_t		__u32;
typedef uint64_t		__u64;
typedef int8_t			__s8;
typedef int16_t			__s16;
typedef int32_t			__s32;
typedef int64_t			__s64;

typedef uint16_t		__le16;
typedef uint32_t		__le32;
typedef uint64_t		__le64;
typedef uint16_t		__be16;
typedef uint32_t		__be32;
typedef uint64_t		__be64;
/* the checksum types the wire-header layouts name (uapi/linux/types.h);
 * __bitwise is a sparse annotation and a no-op for the compiler */
typedef uint16_t		__sum16;
typedef uint32_t		__wsum;

typedef unsigned long		ulong;
typedef unsigned int		uint;
typedef int64_t			loff_t;
typedef int64_t			ssize_t;	/* sysfs show/store signatures */
typedef uint64_t		dma_addr_t;
typedef unsigned int		gfp_t;

#define	__user
#define	__iomem
#define	__force
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

#define	ENOTSUPP		252	/* Linux's private ENOTSUPP */

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

struct device {
	const char	*name;
	void		*driver_data;
	void		*platform_data;
	struct device	*parent;
	/* rtw89's usb.c assigns the sysfs attribute groups here (the
	 * groups are never walked on this image; see linux/sysfs.h) */
	const struct attribute_group **groups;
};

#define	dev_set_drvdata(dev, data)	do { (dev)->driver_data = (data); } while (0)
#define	dev_get_drvdata(dev)		((dev)->driver_data)

/* ------------------------------------------------------------------ */
/* constants and small helpers                                         */
/* ------------------------------------------------------------------ */

#define	BITS_PER_LONG		(8 * sizeof(unsigned long))
#define	BITS_PER_BYTE		8
#ifndef PAGE_SIZE
#define	PAGE_SIZE		4096
#endif
#ifndef PAGE_SHIFT
#define	PAGE_SHIFT		12
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

#define	do_div(n, base)		({ unsigned long __r = (n) % (base);	\
				   (n) = (n) / (base); __r; })

#define	__stringify(x)		#x

/* ilog2: exact for constants (the drivers use it on table sizes) and a
 * floor-log2 at runtime */
#define	ilog2(n)		((unsigned long)__builtin_ctzll((unsigned long long)(n)))

#ifndef READ_ONCE
#define	READ_ONCE(x)		(*(volatile typeof(x) *)&(x))
#endif
#ifndef WRITE_ONCE
#define	WRITE_ONCE(x, v)	do { *(volatile typeof(x) *)&(x) = (v); } while (0)
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
#define	put_unaligned_le16(v, p) do {						\
	u16 __v = le16_to_cpu((__force __u16)(v));				\
	((u8 *)(p))[0] = (u8)__v;						\
	((u8 *)(p))[1] = (u8)(__v >> 8);					\
} while (0)
#define	put_unaligned_le32(v, p) do {						\
	u32 __v = le32_to_cpu((__force __u32)(v));				\
	((u8 *)(p))[0] = (u8)__v;						\
	((u8 *)(p))[1] = (u8)(__v >> 8);					\
	((u8 *)(p))[2] = (u8)(__v >> 16);					\
	((u8 *)(p))[3] = (u8)(__v >> 24);					\
} while (0)
#define	put_unaligned_le64(v, p) do {						\
	u64 __v = le64_to_cpu((__force __u64)(v));				\
	put_unaligned_le32((__u32)__v, p);					\
	put_unaligned_le32((__u32)(__v >> 32), (u8 *)(p) + 4);			\
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

typedef int64_t ktime_t;

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

int lkpi_printf(const char *fmt, ...);
void lkpi_dev_printk(struct device *dev, const char *fmt, ...);
void lkpi_fatal(const char *what);

/*
 * The dist print helpers funnel everything through dev_*(); the debug
 * paths use the Linux "%pV" wrapped-format convention, which the port
 * printf does not understand - without the special case it prints the
 * pointer instead of the message.
 */
#define	dev_printk(level, dev, fmt, ...)				\
	lkpi_dev_printk((dev), (fmt), ##__VA_ARGS__)
#define	dev_err(dev, fmt, ...)		dev_printk(KERN_ERR, dev, fmt, ##__VA_ARGS__)
#define	dev_warn(dev, fmt, ...)		dev_printk(KERN_WARNING, dev, fmt, ##__VA_ARGS__)
#define	dev_info(dev, fmt, ...)		dev_printk(KERN_INFO, dev, fmt, ##__VA_ARGS__)
#define	dev_notice(dev, fmt, ...)	dev_info(dev, fmt, ##__VA_ARGS__)
#define	dev_dbg(dev, fmt, ...)		dev_printk(KERN_DEBUG, dev, fmt, ##__VA_ARGS__)
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
#define	pr_err(fmt, ...)		lkpi_printf(fmt "\n", ##__VA_ARGS__)
#define	pr_warn(fmt, ...)		lkpi_printf(fmt "\n", ##__VA_ARGS__)
#define	pr_info(fmt, ...)		lkpi_printf(fmt "\n", ##__VA_ARGS__)
#define	pr_notice(fmt, ...)		lkpi_printf(fmt "\n", ##__VA_ARGS__)
#define	pr_warn_once(fmt, ...)		pr_warn(fmt, ##__VA_ARGS__)
#define	pr_err_once(fmt, ...)		pr_err(fmt, ##__VA_ARGS__)
#define	pr_debug(fmt, ...)		do { } while (0)
#define	pr_cont(fmt, ...)		lkpi_printf(fmt, ##__VA_ARGS__)

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
#define	module_exit(fn)
#define	module_init(fn)

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

/* ------------------------------------------------------------------ */
/* bugs and assertions                                                 */
/* ------------------------------------------------------------------ */

#define	WARN_ON(cond)		({					\
	bool __w = (cond);						\
	if (__w)							\
		lkpi_printf("%s:%d: WARN_ON(%s)\n", __FILE__, __LINE__, #cond); \
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
#define	lockdep_assert_held(l)	do { } while (0)
#define	lockdep_assert_held_read(l) do { } while (0)
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
#define	tasklet_schedule(t)		do { (void)(t); } while (0)

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

static __always_inline __unused unsigned char *
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

static __always_inline __unused unsigned char *
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

/* Returns 1 if the condition became true, 0 on timeout (Linux semantics). */
#define	wait_event_timeout(wq, cond, timeout)				\
({									\
	int __ret = 0;							\
	int __ms = (int)jiffies_to_msecs(timeout);			\
	lkpi_waitq_lock(&(wq));						\
	while (!(cond) && __ret == 0)					\
		__ret = lkpi_waitq_timedwait(&(wq), __ms);		\
	lkpi_waitq_unlock(&(wq));					\
	__ret == 0 ? 1 : 0;						\
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
#define	__lkpi_kmalloc_obj1(T)			kmalloc(sizeof(T), GFP_KERNEL)
#define	__lkpi_kmalloc_obj2(T, GFP)		kmalloc(sizeof(T), (GFP))
#define	__lkpi_kzalloc_obj1(T)			kzalloc(sizeof(T), GFP_KERNEL)
#define	__lkpi_kzalloc_obj2(T, GFP)		kzalloc(sizeof(T), (GFP))
#define	kmalloc_obj(...)							\
	__lkpi_obj_pick(__VA_ARGS__, __lkpi_kmalloc_obj2, __lkpi_kmalloc_obj1)(__VA_ARGS__)
#define	kzalloc_obj(...)							\
	__lkpi_obj_pick(__VA_ARGS__, __lkpi_kzalloc_obj2, __lkpi_kzalloc_obj1)(__VA_ARGS__)
#define	kzalloc_objs(P, COUNT)			kzalloc(sizeof(P) * (COUNT), GFP_KERNEL)
#define	kmalloc_objs(P, COUNT)			kmalloc(sizeof(P) * (COUNT), GFP_KERNEL)

#define	__alloc_flex(KMALLOC, GFP, TYPE, FAM, COUNT)			\
({									\
	const size_t __count = (COUNT);					\
	const size_t __obj_size = struct_size_t(TYPE, FAM, __count);	\
	TYPE *__obj_ptr = KMALLOC(__obj_size, GFP);			\
	__obj_ptr;							\
})

#define	kzalloc_flex(P, FAM, COUNT)	__alloc_flex(kzalloc, GFP_KERNEL, typeof(P), FAM, COUNT)

/* [LNX] helper the compiled set names */
/* [LNX] include/linux/string.h:281 */
void *memchr_inv(const void *s, int c, size_t n);

/* [LNX] helper the compiled set names */
#endif /* LINUXKPI_CORE_H */
