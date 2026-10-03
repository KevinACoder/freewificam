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
/* the __kernel_* spellings (vendor basic_types.h SIZE_T/SSIZE_T); the
 * __u8..__u64 family already lives below (lines 74-97) */
typedef unsigned long		__kernel_size_t;
typedef long			__kernel_ssize_t;
typedef long			__kernel_ptrdiff_t;
typedef int			__kernel_pid_t;
typedef unsigned int		__kernel_uid_t;
typedef unsigned int		__kernel_gid_t;
typedef long			__kernel_off_t;
typedef long long		__kernel_loff_t;
typedef int			__kernel_time_t;

/* host-endian conversions: identity macros on this little-endian target -
 * MACROS, not functions: `case htons(ETH_P_IP):` needs a constant */
#define	__cpu_to_be16(x)	((__u16)__builtin_bswap16(x))
#define	__cpu_to_be32(x)	((__u32)__builtin_bswap32(x))
#define	__be16_to_cpu(x)	((__u16)__builtin_bswap16(x))
#define	__be32_to_cpu(x)	((__u32)__builtin_bswap32(x))
#define	htons(x)		((__u16)(x))
#define	htonl(x)		((__u32)(x))
#define	ntohs(x)		((__u16)(x))
#define	ntohl(x)		((__u32)(x))
#define	cpu_to_be16(x)		__cpu_to_be16(x)
#define	cpu_to_be32(x)		__cpu_to_be32(x)
#define	be16_to_cpu(x)		__be16_to_cpu(x)
#define	be32_to_cpu(x)		__be32_to_cpu(x)
typedef long			ssize_t;

/* the limits the drivers reference */
#define	U8_MAX		((u8)~0U)
#define	S8_MAX		((s8)127)
#define	U16_MAX		((u16)~0U)
#define	S16_MAX		((s16)32767)
#define	U32_MAX		((u32)~0U)
#define	S32_MAX		((s32)2147483647)
#define	INT_MAX		((int)2147483647)
#define	UINT_MAX	((unsigned int)~0U)
#define	LONG_MAX	((long)2147483647)
#define	ULONG_MAX	((unsigned long)~0UL)

typedef int8_t			s8;
typedef int16_t			s16;
typedef int32_t			s32;
typedef int64_t			s64;

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
typedef uint64_t		__le64;
typedef uint16_t		__be16;
typedef uint32_t		__be32;
typedef uint64_t		__be64;

typedef unsigned long		ulong;
typedef unsigned int		uint;
typedef int64_t			loff_t;
typedef uint64_t		dma_addr_t;
typedef unsigned int		gfp_t;

#define	__user
#define	__iomem
#define	__force
#define	__must_check
#define	__init
#define	__exit
#define	__acquires(x)
#define	__releases(x)
#define	__printf(a, b)		__attribute__((__format__(printf, a, b)))
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
#ifndef fallthrough
#define	fallthrough		do {} while (0)
#endif

#define	U8_MAX			0xffU
#define	U16_MAX			0xffffU
#define	U32_MAX			0xffffffffU
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

struct device {
	const char	*name;
	void		*driver_data;
	void		*platform_data;
	struct device	*parent;
};

/* [S6] B7: only the shutdown initializer site the vendor usb_driver needs;
 * this image never calls it */
struct device_driver {
	void	(*shutdown)(struct device *dev);
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

#define	min(a, b)		((a) < (b) ? (a) : (b))
#define	max(a, b)		((a) > (b) ? (a) : (b))
#define	MIN(a, b)		min((a), (b))
#define	MAX(a, b)		max((a), (b))
#define	min_t(t, a, b)		((t)min((a), (b)))
#define	max_t(t, a, b)		((t)max((a), (b)))
#define	clamp_val(v, lo, hi)	max(min((v), (hi)), (lo))
#define	clamp_t(t, v, lo, hi)	((t)clamp_val((v), (lo), (hi)))
#define	swap(a, b)		do { typeof(a) __t = (a); (a) = (b); (b) = __t; } while (0)
#define	abs_diff(a, b)		((a) > (b) ? (a) - (b) : (b) - (a))

/* Linux's container_of; the drivers' version includes array members. */
#define	container_of(ptr, type, member)	\
	((type *)(void *)((char *)(void *)(ptr) - offsetof(type, member)))

#define	VERIFY_OCTAL_PERMISSIONS(x)	(x)

/* Linux's fls() is the 1-based position of the highest set bit. */
static __always_inline __unused int
lkpi_fls32(uint32_t x)
{

	return x == 0 ? 0 : 32 - __builtin_clz(x);
}

#define	fls(x)			lkpi_fls32((uint32_t)(x))

static __always_inline __unused unsigned int
hweight8(u8 w)
{
	unsigned int res = w - ((w >> 1) & 0x55);

	res = (res & 0x33) + ((res >> 2) & 0x33);
	return (res + (res >> 4)) & 0x0f;
}

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
#define	EOPNOTSUPP		95
#define	EPIPE			32
/* the urb-cancellation code usb_kill_urb delivers on Linux (hub.c's
 * stop set consumes it); the kpi kill surfaces -ENOENT/-ENODEV, the
 * driver's stop set lists both spellings */
#define	ECONNRESET		104

/* the ERR_PTR convention (osdep file paths test IS_ERR on filp_open) */
static __always_inline __unused void *
ERR_PTR(long error)
{
	return (void *)(long)error;
}

static __always_inline __unused long
PTR_ERR(const void *ptr)
{
	return (long)(uintptr_t)ptr;
}

static __always_inline __unused bool
IS_ERR(const void *ptr)
{
	return (uintptr_t)ptr >= (uintptr_t)-4095L;
}

static __always_inline __unused bool
IS_ERR_OR_NULL(const void *ptr)
{
	return ptr == NULL || IS_ERR(ptr);
}

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

/* The 6.14+/6.15+ allocation API forms. */
#define	kzalloc_obj(p)		kzalloc(sizeof(p), GFP_KERNEL)
#define	kmalloc_obj(p, flags)	kmalloc(sizeof(p), (flags))

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

#define	rcu_read_lock()		do { } while (0)
#define	rcu_read_unlock()	do { } while (0)
#define	rcu_dereference(p)	(p)
#define	rcu_access_pointer(p)	(p)
#define	rcu_assign_pointer(p, v) do { (p) = (v); } while (0)
#define	synchronize_rcu()	do { } while (0)
#define	kfree_rcu(p, member)	kfree(p)

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
int lkpi_atomic_sub_return(int i, atomic_t *v);
int lkpi_atomic_add_return(int i, atomic_t *v);
#define	atomic_sub_return(i, v)	lkpi_atomic_sub_return((i), (v))
#define	atomic_add_return(i, v)	lkpi_atomic_add_return((i), (v))
#define	atomic_dec_and_test(v)	(lkpi_atomic_sub_return(1, (v)) == 0)
#define	atomic_inc_and_test(v)	(lkpi_atomic_add_return(1, (v)) == 0)
#define	atomic_read(v)		__atomic_load_n(&(v)->counter, __ATOMIC_RELAXED)
#define	atomic_set(v, i)	__atomic_store_n(&(v)->counter, (i), __ATOMIC_RELAXED)
void lkpi_atomic_dec(atomic_t *v);
int lkpi_atomic_inc_return(atomic_t *v);
int lkpi_atomic_dec_return(atomic_t *v);
int lkpi_atomic_dec_and_test(atomic_t *v);
void lkpi_atomic_add(int i, atomic_t *v);
void lkpi_atomic_sub(int i, atomic_t *v);

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
#define	MODULE_VERSION(x)
#define	MODULE_FIRMWARE(x)
#define	MODULE_PARM_DESC(x, y)
#define	module_param_named(a, b, c, d)
#define	module_param(a, b, c)
#define	module_param_array(a, b, c, d)
#define	module_param_array_named(a, b, c, d, e)
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
#define	WARN(cond, fmt, ...)	WARN_ON(cond)
#define	BUG_ON(cond)		do { if (cond) lkpi_fatal("BUG_ON(" #cond ")"); } while (0)
/* print_hex_dump prefix args ([LNX] kernel.h) */
#define	DUMP_PREFIX_NONE	0
#define	DUMP_PREFIX_OFFSET	1
#define	DUMP_PREFIX_ADDRESS	2

#define	BUILD_BUG_ON(cond)	do { } while (0)
#define	BUILD_BUG_ON_ZERO(cond)	0
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

typedef struct mutex	spinlock_t;

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
#define	mutex_lock_interruptible(m)	(lkpi_mutex_lock(m), 0)
#define	mutex_lock_killable(m)		(lkpi_mutex_lock(m), 0)
#define	mutex_trylock(m)		lkpi_mutex_trylock(m)
#define	mutex_is_locked(m)		lkpi_mutex_is_locked(m)

/*
 * The driver code takes spinlocks from process context only (queue lists,
 * stats, the register scratch) - never from an interrupt handler: USB
 * completions are deferred to the lkpi workqueue before they can reach
 * driver code.  A passive mutex is therefore the whole contract; if a
 * driver ever grows an irq-context spin_lock site, that is a linuxkpi
 * layering bug, not something to paper over with a real spinlock.
 */
#define	spin_lock_init(l)		lkpi_mutex_init((struct mutex *)(l))
#define	spin_lock_destroy(l)		lkpi_mutex_destroy((struct mutex *)(l))
#define	spin_lock(l)			lkpi_mutex_lock((struct mutex *)(l))
#define	spin_unlock(l)			lkpi_mutex_unlock((struct mutex *)(l))
#define	spin_lock_bh(l)			spin_lock(l)
#define	spin_unlock_bh(l)		spin_unlock(l)
#define	spin_lock_irqsave(l, flags)					\
	do { (flags) = 0; spin_lock(l); } while (0)
#define	spin_unlock_irqrestore(l, flags)				\
	do { (void)(flags); spin_unlock(l); } while (0)
#define	spin_lock_irq(l)		spin_lock(l)
#define	spin_unlock_irq(l)		spin_unlock(l)
#define	spin_trylock(l)			lkpi_mutex_trylock((struct mutex *)(l))
#define	spin_trylock_bh(l)		spin_trylock(l)
#define	spin_is_locked(l)		lkpi_mutex_is_locked((struct mutex *)(l))
#define	spin_unlock_wait(l)		do { } while (0)
#define	assert_spin_locked(l)		do { } while (0)

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
/* tasklet_schedule is REAL since [S6] §3.4 B3 (the rtl8821cu RX/TX rearm
 * chain schedules it); the real face lives in the tasklet section below. */

/* ------------------------------------------------------------------ */
/* sk_buff (linux/skbuff.h) - data shape, operations in the impl        */
/* ------------------------------------------------------------------ */

struct sk_buff_head {
	struct sk_buff	*next;
	struct sk_buff	*prev;
	spinlock_t	lock;
	u32		qlen;
};

void lkpi_skb_queue_head_init_lockable(struct sk_buff_head *q);

struct sk_buff {
	struct sk_buff	*next;
	struct sk_buff	*prev;
	struct sk_buff_head *list;

	unsigned char	*head;
	unsigned char	*data;
	unsigned int	tail;
	unsigned int	end;
	unsigned int	len;
	unsigned int	data_len;
	unsigned int	truesize;

	u16		protocol;
	u16		queue_mapping;
	u32		priority;
	u8		pkt_type;
	u8		ip_summed;

	void		*dev;		/* struct device * */
	void		*sk;

	/* mac80211 control/status block (IEEE80211_SKB_CB lives here) */
	unsigned long	cb[48 / sizeof(unsigned long)];
} __aligned(4);

void *lkpi_skb_alloc(unsigned int size, gfp_t gfp);
void lkpi_skb_free(struct sk_buff *);
void lkpi_skb_queue_init(struct sk_buff_head *);
void lkpi_skb_queue_tail(struct sk_buff_head *, struct sk_buff *);
void lkpi_skb_queue_head(struct sk_buff_head *, struct sk_buff *);
struct sk_buff *lkpi_skb_dequeue(struct sk_buff_head *);
void lkpi_skb_unlink(struct sk_buff *, struct sk_buff_head *);
void lkpi_skb_queue_purge(struct sk_buff_head *);

#define	alloc_skb(size, gfp)		lkpi_skb_alloc((size), (gfp))
#define	__dev_alloc_skb(size, gfp)	lkpi_skb_alloc((size), (gfp))
#define	dev_alloc_skb(size)		lkpi_skb_alloc((size), GFP_ATOMIC)
#define	kfree_skb(skb)			lkpi_skb_free(skb)
#define	kfree_skb_any(skb)		lkpi_skb_free(skb)
#define	dev_kfree_skb(skb)		lkpi_skb_free(skb)
#define	dev_kfree_skb_any(skb)		lkpi_skb_free(skb)
#define	dev_kfree_skb_irq(skb)		lkpi_skb_free(skb)
#define	consume_skb(skb)		lkpi_skb_free(skb)

#define	skb_queue_head_init(q)		lkpi_skb_queue_init(q)
#define	skb_queue_head_init_lockable(q)	lkpi_skb_queue_head_init_lockable(q)
#define	skb_queue_tail(q, skb)		lkpi_skb_queue_tail((q), (skb))
#define	__skb_queue_tail(q, skb)	lkpi_skb_queue_tail((q), (skb))
#define	skb_queue_head(q, skb)		lkpi_skb_queue_head((q), (skb))
#define	__skb_queue_head(q, skb)	lkpi_skb_queue_head((q), (skb))
#define	skb_dequeue(q)			lkpi_skb_dequeue(q)
#define	__skb_dequeue(q)		lkpi_skb_dequeue(q)
#define	skb_unlink(skb, q)		lkpi_skb_unlink((skb), (q))
#define	__skb_unlink(skb, q)		lkpi_skb_unlink((skb), (q))
#define	skb_queue_purge(q)		lkpi_skb_queue_purge(q)
#define	skb_peek(q)			((q)->next)
#define	skb_peek_tail(q)		((q)->prev)
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

static __always_inline __unused void
skb_reserve(struct sk_buff *skb, unsigned int len)
{
	skb->data += len;
	skb->tail += len;
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

	n = lkpi_skb_alloc(skb->end, gfp);
	if (n == NULL)
		return NULL;
	memcpy(n->data, skb->data, skb->len);
	n->len = skb->len;
	n->tail = skb->tail;
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
/* skb widening ([S6] §3.4 B6 - the rtl8821cu RX hot path clones)      */
/* ------------------------------------------------------------------ */

struct net_device;		/* the net_device section below */

struct skb_shared_info {
	unsigned char	nr_frags;
	/* the shared-info face is a constant-zero stub: nr_frags==0 makes the
	 * drivers' frag loops fall through; nothing may ever write here */
	unsigned char	__reserved[63];
};

extern struct skb_shared_info lkpi_shinfo_zero;

#define	skb_shinfo(skb)			(&lkpi_shinfo_zero)

struct sk_buff *lkpi_skb_clone(const struct sk_buff *skb, gfp_t gfp);
struct sk_buff *lkpi_skb_copy_header(const struct sk_buff *skb, gfp_t gfp);

static __always_inline __unused struct sk_buff *
skb_clone(const struct sk_buff *skb, gfp_t gfp)
{
	return lkpi_skb_clone(skb, gfp);
}

static __always_inline __unused struct sk_buff *
pskb_copy(const struct sk_buff *skb, gfp_t gfp)
{
	return lkpi_skb_copy_header(skb, gfp);
}

static __always_inline __unused struct sk_buff *
netdev_alloc_skb(struct net_device *dev, unsigned int len)
{
	struct sk_buff *skb = lkpi_skb_alloc(len, GFP_ATOMIC);

	if (skb != NULL)
		skb->dev = dev;
	return skb;
}

static __always_inline __unused void
skb_trim(struct sk_buff *skb, unsigned int len)
{

	if (skb->len > len) {
		skb->len = len;
		skb->tail = (unsigned int)(skb->data - skb->head) + len;
	}
}

static __always_inline __unused void
__skb_trim(struct sk_buff *skb, unsigned int len)
{
	skb_trim(skb, len);
}

/* the pointer-arithmetic face (vendor osdep only carries some spellings;
 * W0L-S9 leftovers land here) */
static __always_inline __unused unsigned char *
skb_tail_pointer(const struct sk_buff *skb)
{
	return skb->head + skb->tail;
}

static __always_inline __unused unsigned char *
skb_end_pointer(const struct sk_buff *skb)
{
	return skb->head + skb->end;
}

static __always_inline __unused void
skb_set_tail_pointer(struct sk_buff *skb, unsigned int offset)
{
	/* Linux contract: set the tail RELATIVE TO data, leave data alone
	 * (skb->tail = skb->data + offset in kernel pointer terms; the KPI
	 * stores tail as a head-relative offset).  The earlier shape here
	 * also moved skb->data to head + offset and cost the rtl8821cu RX
	 * line its payload: the vendor's delivery path calls this with
	 * precv_frame->u.hdr.len right after setting skb->data = rx_data,
	 * so data teleported len bytes past the frame (board 2026-10-03:
	 * EAPOL msg1 arrived, sink read head+127 = zero region, 4-way
	 * never started). */
	skb->tail = (unsigned int)(skb->data - skb->head) + offset;
}

static __always_inline __unused void
skb_orphan(struct sk_buff *skb)
{
	skb->sk = NULL;
}

static __always_inline __unused int
skb_cloned(const struct sk_buff *skb)
{
	/* the clone face is full-copy; nothing is ever shared */
	(void)skb;
	return 0;
}

/* ------------------------------------------------------------------ */
/* completion                                                          */
/* ------------------------------------------------------------------ */

struct completion {
	void	*_mtx[8];	/* the port mutex shell's size */
	void	*_cv[8];	/* the port condvar shell's size */
	int	done;
};

/* complete_all marker: sticky, never consumed by wait_for_completion */
#define	LKPI_COMPLETION_STICKY	0x40000000

void lkpi_completion_init(struct completion *);
void lkpi_completion_deinit(struct completion *);
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
/* [W3] the RTOS completion registers a TX object at init; the vendor's
 * submit_ctx lifetime kfree()s the embedding memory (legal on Linux,
 * where a completion is just a struct) - the release must pair with a
 * deinit or the created chain keeps a zombie.  Patches call this before
 * rtw_mfree of any completion-bearing allocation. */
#define	_rtw_deinit_completion(c)		lkpi_completion_deinit(c)
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

/* ------------------------------------------------------------------ */
/* semaphores ([S6] §3.4 B1 - the vendor osdep _sema base,             */
/* osdep_service.c down/up/sema_init; TX counting semaphore shell)     */
/* ------------------------------------------------------------------ */

struct semaphore {
	void	*_sem[10];	/* the TX_SEMAPHORE shell (impl assert) */
};

void lkpi_sema_init(struct semaphore *s, int val);
void lkpi_sema_destroy(struct semaphore *s);
void lkpi_sema_down(struct semaphore *s);
int lkpi_sema_down_interruptible(struct semaphore *s);
int lkpi_sema_down_trylock(struct semaphore *s);
void lkpi_sema_up(struct semaphore *s);

#define	sema_init(s, v)			lkpi_sema_init((s), (v))
#define	sema_destroy(s)			lkpi_sema_destroy(s)
#define	down(s)				lkpi_sema_down(s)
#define	down_interruptible(s)		lkpi_sema_down_interruptible(s)
#define	down_trylock(s)			lkpi_sema_down_trylock(s)
#define	up(s)				lkpi_sema_up(s)

/* the signal family the down_interruptible callers expect: nothing in this
 * image ever signals a thread, so pending is always false */
struct task_struct;

#define	signal_pending(t)		((void)(t), 0)
#define	allow_signal(sig)		do { (void)(sig); } while (0)
#define	disallow_signal(sig)		do { (void)(sig); } while (0)
#define	daemonize(fmt, ...)		do { } while (0)

/* the userspace-notify family (osdep rtw_signal_process -> kill_pid):
 * every pid in this image stays 0, so the calls are guarded no-ops */
#define	SIGALRM		14
#define	SIGUSR1		10
#define	SIGUSR2		12
#define	SIGTERM		15
#define	SIGINT		2
#define	SIGHUP		1
#define	SIGKILL		9
#define	find_vpid(pid)		((void *)(uintptr_t)(unsigned long)(pid))
#define	kill_pid(pid, sig, priv)					\
	((void)(pid), (void)(sig), (void)(priv), 0)
#define	kill_proc(pid, sig, priv)					\
	((void)(pid), (void)(sig), (void)(priv), 0)

/* ------------------------------------------------------------------ */
/* kthreads ([S6] §3.4 B2 - os_intfs.c four threads + osdep            */
/* thread_exit/rtw_thread_stop; TX thread + stop flag)                 */
/* ------------------------------------------------------------------ */

struct task_struct {
	void	*_tx[48];	/* the TX_THREAD shell (impl assert) */
	struct task_struct *lk_next;	/* the kthread registry chain */
	void	(*fn)(void *);
	void	*arg;
	volatile int	should_stop;
	volatile int	exited;
	const char	*name;
	int		pid;		/* stamp for current->pid debug prints */
	char		comm[16];	/* current->comm (the kthread name) */
};

struct task_struct *lkpi_kthread_run(int (*threadfn)(void *), void *data,
    const char *name);
int lkpi_kthread_stop(struct task_struct *t);
bool lkpi_kthread_should_stop(void);
struct task_struct *lkpi_current(void);

#define	kthread_run(fn, data, fmt, ...)					\
	lkpi_kthread_run((fn), (data), (fmt))
#define	kthread_stop(t)			lkpi_kthread_stop(t)
#define	kthread_should_stop()		lkpi_kthread_should_stop()
#define	kthread_create(fn, data, fmt, ...)				\
	lkpi_kthread_run((fn), (data), (fmt))
#define	current				lkpi_current()

/* complete_and_exit never returns: it fires the thread's completion and
 * terminates the TX thread in place (the vendor thread_exit wrapper lands
 * here; the W0-L kthread_complete_and_exit rename is a 5.17+ kernel-only
 * rename that does not apply to this face). */
void complete_and_exit(struct completion *comp, long code)
	__attribute__((__noreturn__));

/* task states (stamps; nothing schedules off them) */
#define	TASK_RUNNING		0
#define	TASK_INTERRUPTIBLE	1
#define	TASK_UNINTERRUPTIBLE	2

/* scheduler stubs: the drivers only set state and sleep out timeouts.
 * These resolve through real functions (core.c) - the driver TUs have no
 * TX header, so the macros must not name a ThreadX symbol directly; the
 * usleep_range face already exists above (lkpi_usleep_range). */
#define	set_current_state(state)	do { (void)(state); } while (0)
#define	__set_current_state(state)	do { (void)(state); } while (0)
#define	schedule()			lkpi_msleep(1)
long lkpi_sched_timeout(unsigned int ms);	/* returns 0: timeout elapsed */
#define	schedule_timeout(j)		lkpi_sched_timeout(jiffies_to_msecs(j))
#define	schedule_timeout_interruptible(j) schedule_timeout(j)
#define	freezable_schedule()		schedule()
/* msleep/usleep_range ride the pre-existing lkpi faces above */
#define	ssleep(s)			lkpi_msleep((unsigned int)(s) * 1000U)
#define	io_schedule()			schedule()
#define	cond_resched()			do { } while (0)
#define	yield()				lkpi_msleep(1)
unsigned int prandom_u32(void);

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
#define	queue_work(wq, w)		lkpi_queue_work((wq), (w))
#define	queue_delayed_work(wq, w, j)	lkpi_queue_delayed_work((wq), (w), (j))
#define	cancel_delayed_work(w)		lkpi_cancel_delayed_work(w)
#define	cancel_delayed_work_sync(w)	lkpi_cancel_delayed_work_sync(w)
#define	cancel_work_sync(w)		(lkpi_work_flush(w), true)
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

/* ------------------------------------------------------------------ */
/* tasklets ([S6] §3.4 B3 - REAL since the rtl8821cu RX/TX rearm       */
/* chain depends on them; softirq semantics map to a pass through the  */
/* system workqueue, the same passive-context shape as the drain)      */
/* ------------------------------------------------------------------ */

struct tasklet_struct {
	struct work_struct	work;
	void			(*func)(unsigned long);
	unsigned long		data;
	bool			armed;
};

void lkpi_tasklet_init(struct tasklet_struct *t, void (*fn)(unsigned long),
    unsigned long data);
void lkpi_tasklet_schedule(struct tasklet_struct *t);
void lkpi_tasklet_kill(struct tasklet_struct *t);

#define	tasklet_init(t, fn, data)					\
	lkpi_tasklet_init((t), (fn), (unsigned long)(data))
#define	tasklet_schedule(t)		lkpi_tasklet_schedule(t)
#define	tasklet_hi_schedule(t)		lkpi_tasklet_schedule(t)
#define	tasklet_kill(t)			lkpi_tasklet_kill(t)

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
void lkpi_del_timer(struct timer_list *);
void lkpi_del_timer_sync(struct timer_list *);
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
	void	*_cv[8];
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
/* net_device ([S6] §3.4 B4/B5 - the vendor os_intfs.c minimal face:   */
/* flat struct + trailing priv region, no-op registration, netif_rx    */
/* as a registerable sink (W5 mounts the lwip bridge there))           */
/* ------------------------------------------------------------------ */

#define	IFNAMSIZ		16
#define	ETH_ALEN		6
#define	LKPI_NETDEV_ALIGN	32u

struct net_device_stats {
	unsigned long	rx_packets;
	unsigned long	tx_packets;
	unsigned long	rx_bytes;
	unsigned long	tx_bytes;
	unsigned long	rx_errors;
	unsigned long	tx_errors;
	unsigned long	rx_dropped;
	unsigned long	tx_dropped;
	unsigned long	multicast;
	unsigned long	collisions;
	unsigned long	rx_length_errors;
	unsigned long	rx_over_errors;
	unsigned long	rx_crc_errors;
	unsigned long	rx_frame_errors;
	unsigned long	rx_fifo_errors;
	unsigned long	rx_missed_errors;
	unsigned long	tx_aborted_errors;
	unsigned long	tx_carrier_errors;
	unsigned long	tx_fifo_errors;
	unsigned long	tx_heartbeat_errors;
	unsigned long	tx_window_errors;
	unsigned long	rx_compressed;
	unsigned long	tx_compressed;
};

/* we only carry the net_device_ops members the vendor tables initialize
 * (select_queue keeps the vendor 4.20-era three-net_device* signature) */
/* [LNX] netdevice.h 4.19-5.1 spelling: the vendor's live select-queue
 * branch carries the fallback function pointer as the fourth parameter */
typedef u16 (*select_queue_fallback_t)(struct net_device *dev,
    struct sk_buff *skb);

typedef int netdev_tx_t;
#define	NETDEV_TX_OK		0
#define	NETDEV_TX_BUSY		1
#define	NETDEV_TX_LOCKED	0x100

/* ifreq/IFF faces: if.h carries the shapes; the core needs the flags and
 * an incomplete ifreq for the ndo_do_ioctl prototype */
struct ifreq;		/* linux/if.h */
#define	ARPHRD_ETHER		1
#define	ARPHRD_IEEE80211	801
#define	ARPHRD_IEEE80211_RADIOTAP 803
#define	IFF_UP		0x1
#define	IFF_BROADCAST	0x2
#define	IFF_DEBUG	0x4
#define	IFF_LOOPBACK	0x8
#define	IFF_POINTOPOINT	0x10
#define	IFF_RUNNING	0x40
#define	IFF_NOARP	0x80
#define	IFF_PROMISC	0x100
#define	IFF_ALLMULTI	0x200
#define	IFF_MULTICAST	0x1000

struct net_device_ops {
	void		(*ndo_init)(struct net_device *dev);
	void		(*ndo_uninit)(struct net_device *dev);
	int		(*ndo_open)(struct net_device *dev);
	int		(*ndo_stop)(struct net_device *dev);
	netdev_tx_t	(*ndo_start_xmit)(struct sk_buff *skb,
			    struct net_device *dev);
	u16		(*ndo_select_queue)(struct net_device *dev,
			    struct sk_buff *skb, struct net_device *sb_dev,
			    select_queue_fallback_t fallback);
	int		(*ndo_do_ioctl)(struct net_device *dev,
			    struct ifreq *ifr, int cmd);
	int		(*ndo_set_mac_address)(struct net_device *dev,
			    void *addr);
	struct net_device_stats *(*ndo_get_stats)(struct net_device *dev);
	void		(*ndo_set_rx_mode)(struct net_device *dev);
	void		(*ndo_tx_timeout)(struct net_device *dev);
};

struct net_device {
	char			name[IFNAMSIZ];
	unsigned char		dev_addr[ETH_ALEN];
	unsigned char		perm_addr[ETH_ALEN];
	unsigned char		addr_len;
	unsigned short		type;
	unsigned short		flags;
	unsigned int		mtu;
	unsigned int		needed_headroom;
	unsigned long		watchdog_timeo;
	const struct net_device_ops *netdev_ops;
	struct device		dev;		/* embedded; SET_NETDEV_DEV */
	unsigned long		tx_queued;	/* queue stop/start state */
	/* trailing priv region: netdev_priv() = (char *)ndev + priv offset;
	 * alloc_etherdev_mq stores sizeof_priv in the private head */
	unsigned int		_priv_len;
} __aligned(LKPI_NETDEV_ALIGN);

struct lkpi_tx_queue {
	unsigned char	stopped;
};

struct lkpi_tx_queue *netdev_get_tx_queue(struct net_device *dev,
    unsigned int index);
#define	netif_tx_queue_stopped(q)	((q)->stopped != 0)
#define	netif_queue_stopped(dev)	netif_tx_queue_stopped(		\
					netdev_get_tx_queue((dev), 0))

void *lkpi_netdev_priv(const struct net_device *dev);
#define	netdev_priv(dev)		lkpi_netdev_priv(dev)

struct net_device *alloc_etherdev_mq(int sizeof_priv, unsigned int txqs);
#define	alloc_etherdev(sizeof_priv)	alloc_etherdev_mq((sizeof_priv), 1)
void free_netdev(struct net_device *dev);
int register_netdev(struct net_device *dev);
#define	register_netdevice(dev)		(register_netdev(dev))
#define	unregister_netdevice(dev)	(unregister_netdev(dev))
#define	dev_close(dev)			((void)(dev), 0)
#define	dev_open(dev)			((void)(dev), 0)
void unregister_netdev(struct net_device *dev);
int dev_alloc_name(struct net_device *dev, const char *fmt);

#define	SET_NETDEV_DEV(ndev, pdev)	do {				\
	(ndev)->dev.parent = (pdev);					\
} while (0)

/* netif_rx return codes + skb classification faces */
#define	NET_RX_SUCCESS		0
#define	NET_RX_CN_LOW		1
#define	NET_RX_DROP		2

#define	PACKET_HOST		0
#define	PACKET_BROADCAST	1
#define	PACKET_MULTICAST	2
#define	PACKET_OTHERHOST	3
#define	PACKET_OUTGOING		4

#define	CHECKSUM_NONE		0
#define	CHECKSUM_UNNECESSARY	1
#define	CHECKSUM_COMPLETE	2
#define	CHECKSUM_PARTIAL	3

/* the network-header accessor the TX classify path reads; at the
 * select-queue point the vendor expects the IP header at skb->data */
#define	ip_hdr(skb)	((struct iphdr *)((skb)->data))

/* the radiotap + skb-buffer copy helpers (monitor RX + TxBF paths) */
int skb_copy_bits(const struct sk_buff *skb, unsigned int offset,
    void *to, unsigned int len);
unsigned int ieee80211_get_radiotap_len(const struct sk_buff *skb);
#define	__netif_subqueue_stopped(dev, i)	0
#define	__netif_subqueue_queued(dev, i)	1

/* the register face is a no-op with a stamp: there is no net core in this
 * image, the adapter layer drives ndo_open/ndo_stop directly ([S6] B4). */

/* netif sink: netif_rx hands the frame to the registered sink (counted);
 * the default sink drops.  W5 mounts the lwip bridge via
 * lkpi_netif_rx_sink_register(). */
struct sk_buff;
typedef int (*lkpi_netif_rx_sink_t)(struct sk_buff *skb);
int netif_rx(struct sk_buff *skb);
void lkpi_netif_rx_sink_register(lkpi_netif_rx_sink_t sink);
unsigned long lkpi_netif_rx_dropped(void);

void netif_carrier_on(struct net_device *dev);
void netif_carrier_off(struct net_device *dev);
void netif_device_attach(struct net_device *dev);
void netif_device_detach(struct net_device *dev);
#define	netif_tx_stop_all_queues(dev)	do {				\
	netdev_get_tx_queue((dev), 0)->stopped = 1;			\
} while (0)
#define	netif_tx_wake_all_queues(dev)	do {				\
	netdev_get_tx_queue((dev), 0)->stopped = 0;			\
} while (0)
#define	netif_tx_start_all_queues(dev)	netif_tx_wake_all_queues(dev)
#define	netif_stop_queue(dev)		netif_tx_stop_all_queues(dev)
#define	netif_start_queue(dev)		netif_tx_start_all_queues(dev)
#define	netif_wake_queue(dev)		netif_tx_wake_all_queues(dev)
#define	netif_trans_update(dev)		do { (void)(dev); } while (0)
#define	netif_wake_subqueue(dev, i)	netif_tx_wake_all_queues(dev)
#define	netif_stop_subqueue(dev, i)	netif_tx_stop_all_queues(dev)
#define	netif_running(dev)		(((dev)->flags & IFF_UP) != 0)

/* rtnetlink: no net core, the lock is a stamp (the vendor interface-rename
 * path wraps rename in rtnl_lock/unlock) */
void rtnl_lock(void);
void rtnl_unlock(void);
int rtnl_is_locked(void);
#define	rtnl_trylock()			rtnl_is_locked()
#define	BR_NETPROTO_LOCK()		do { } while (0)

/* netdevice notifiers: nothing in this image ever notifies */
struct notifier_block {
	int	(*notifier_call)(struct notifier_block *nb,
	    unsigned long action, void *data);
};
#define	register_netdevice_notifier(nb)		((void)(nb), 0)
#define	unregister_netdevice_notifier(nb)	((void)(nb), 0)
#define	NETDEV_UP		1
#define	NETDEV_GOING_DOWN	2
#define	NETDEV_DOWN		3
#define	NETDEV_REBOOT		4
#define	NETDEV_CHANGEADDR	5
#define	NETDEV_CHANGENAME	6
#define	NOTIFY_DONE		0

/* the wext event exit (recv path michael-mic indication; the ioctl face
 * is eliminated, so this is a counted stamp) */
union iwreq_data;
struct iw_event;
unsigned short eth_type_trans(struct sk_buff *skb,
    struct net_device *dev);
int wireless_send_event(struct net_device *dev, unsigned int cmd,
    union iwreq_data *wrqu, const char *extra);

/* dev lookups: there is no device database - always miss */
struct net_device *dev_get_by_name(void *net, const char *name);
struct net_device *dev_get_by_index(void *net, int ifindex);
#define	init_net		(NULL)
#define	dev_net(dev)		(NULL)
#define	dev_hold(dev)		do { (void)(dev); } while (0)
#define	dev_put(dev)		do { (void)(dev); } while (0)
#define	NET_NAME_UNKNOWN	0

/* ------------------------------------------------------------------ */
/* usb_anchor (urb.c:812 family; rtl8xxxu embeds three in its softc;    */
/* implementation in linuxkpi_usb.c - spec §3.6-4)                      */
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

static __always_inline __unused int
scnprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return (size_t)n < size ? n : (int)size - 1;
}

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

/* [S6] B8/W0L-S9: the vendor still calls strncpy in live units - minilibc
 * owns the definition (minilibc.h:28); nothing to add here. */

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

/* ------------------------------------------------------------------ */
/* file-I/O face ([S6] B8 / W0L-S1): the image has NO filesystem - the  */
/* vendor debug-dump and efuse-config paths only need to compile (every */
/* open fails, callers fall back to their built-in defaults).  Shapes   */
/* match the vendor's 4.x call sites: f_op->write direct call,          */
/* kernel_read(fp, buf, len, &f_pos), set_fs around the callers.        */
/* loff_t comes from the core types (minilibc.h:97); the byte-count     */
/* face is `long` (no ssize_t in this freestanding world).              */
/* ------------------------------------------------------------------ */

struct file_operations {
	long	(*read)(struct file *, char *, size_t, loff_t *);
	long	(*write)(struct file *, const char *, size_t, loff_t *);
};

struct lkpi_dentry {
	const void		*d_inode;
};

struct file {
	unsigned int		f_mode;
	loff_t			f_pos;
	const struct file_operations *f_op;
	void			*private_data;
	struct {
		struct lkpi_dentry *dentry;
		void		*mnt;
	}			f_path;
};

/* the inode size read is dead-path (every open fails) */
#define	i_size_read(inode)	((loff_t)0)

#define	FMODE_READ		0x00000001
#define	FMODE_WRITE		0x00000002
#define	FMODE_CAN_READ		0x00040000
#define	FMODE_CAN_WRITE		0x00080000

/* the fs-segment dance: a no-op here (no address-space switch possible) */
typedef int	mm_segment_t;
#define	KERNEL_DS		((mm_segment_t)0)
#define	USER_DS			((mm_segment_t)1)
#define	get_ds()		KERNEL_DS
#define	get_fs()		KERNEL_DS
#define	set_fs(seg)		do { (void)(seg); } while (0)

struct file *filp_open(const char *path, int flags, int mode);
int filp_close(struct file *fp, void *files);
long kernel_read(struct file *fp, void *buf, size_t len, loff_t *pos);
long kernel_write(struct file *fp, const void *buf, size_t len, loff_t *pos);
long __vfs_read(struct file *fp, char *buf, size_t len, loff_t *pos);
long __vfs_write(struct file *fp, const char *buf, size_t len, loff_t *pos);
int vfs_fsync(struct file *fp, int datasync);

/* the kernel-libc face the vendor osdep calls into (print discipline
 * rides board_log through the impl) */
int printk(const char *fmt, ...) __attribute__((__format__(printf, 1, 2)));
int sprintf(char *buf, const char *fmt, ...)
	__attribute__((__format__(printf, 2, 3)));
int sscanf(const char *buf, const char *fmt, ...)
	__attribute__((__format__(scanf, 2, 3)));
char *strsep(char **s, const char *delim);
char *strpbrk(const char *s, const char *accept);

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

#endif /* LINUXKPI_CORE_H */
