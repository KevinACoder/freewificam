/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_compat.c
 * @brief   The out-of-line bodies of the interface's generic helpers: the
 *          memory-allocator surface (krealloc/kvfree/kvmemdup/kfree_sensitive, the
 *          named caches, the page family), and the small library entry points the
 *          compiled set names (bsearch, crc32_le, scnprintf, the 64-bit
 *          multiply-add-divide, the RTNL lock, net_ratelimit, the clock
 *          conversions, the RCU/IRQ synchronizes and the lock query).
 *
 * @design adapter over the port's one allocator, plus transcriptions of the
 *         interface's own library bodies. Every allocator body here is expressed
 *         over the heap linuxkpi_core.c already owns (lkpi_kmem_alloc/_free/_size,
 *         whose blocks carry their size in a header); there is no second heap. The
 *         named cache is a heap record holding a name and an object size, and the
 *         "page" the interface hands out is, on this port, the buffer address itself
 *         (the interface's struct page is an opaque handle no compiled caller reads a
 *         field of - callers only pass it to page_address()/free_pages()), so the page
 *         family is the heap under the interface's names. The non-memory bodies are
 *         transcribed from the interface's own source and say so; where a body has no
 *         counterpart on this port (a uevent with no userspace listener, a synchronize
 *         with no grace period), the body is the honest empty answer and says why.
 *         Reference for the family shapes: the Linux semantics quoted per body below.
 * @date    12.10.2026
 */

#include <asm/uaccess.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* [LNX] include/linux/fs.h: the inode/file records simple_open() reads. */
#include <linux/fs.h>

#include "linuxkpi_core.h"

#include <linux/bsearch.h>
#include <linux/crc32.h>
#include <linux/interrupt.h>
#include <linux/kobject.h>
#include <linux/lockdep.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>

/* ------------------------------------------------------------------ */
/* time: the jiffies round-up and the ktime conversion                 */
/* ------------------------------------------------------------------ */

/*
 * [LNX] include/linux/timer.h:176 (declaration) and kernel/time/timer.c:432
 * (the interface's body, which rounds up to a whole second so many wakeups
 * coalesce). This port runs no deferred-timer wheel to round against - a timer's
 * expiry here is the tick it was armed for - so there is nothing to coalesce and
 * the rounding is the identity, exactly as the core's round_jiffies_relative()
 * records.
 */
unsigned long round_jiffies(unsigned long j)
{
	return j;
}

/*
 * [LNX] kernel/time/time.c:535-559 (the interface's body). Splits a signed
 * nanosecond count into the second/nanosecond record, with tv_nsec always
 * non-negative for negative inputs (tv_sec then names the earlier second).
 */
struct timespec64 ns_to_timespec64(s64 nsec)
{
	struct timespec64 ts = { 0, 0 };
	u32 rem;

	if (nsec > 0) {
		ts.tv_sec = (time64_t)div_u64_rem((u64)nsec, (u32)NSEC_PER_SEC, &rem);
		ts.tv_nsec = (long)rem;
	} else if (nsec < 0) {
		ts.tv_sec = -(time64_t)div_u64_rem((u64)(-nsec - 1),
		    (u32)NSEC_PER_SEC, &rem) - 1;
		ts.tv_nsec = (long)((u32)NSEC_PER_SEC - rem - 1);
	}

	return ts;
}

/* ------------------------------------------------------------------ */
/* library entry points                                                */
/* ------------------------------------------------------------------ */

/*
 * [LNX] lib/bsearch.c:41-44 (the interface's body): the entry point is a call into
 * the inline search (carried in linux/bsearch.h). The comparator and the walk are
 * the interface's; nothing here is platform-specific.
 */
void *bsearch(const void *key, const void *base, size_t num, size_t size,
	      cmp_func_t cmp)
{
	return __inline_bsearch(key, base, num, size, cmp);
}

/*
 * [LNX] lib/crc/crc32-main.c:72 (the interface's entry point) over the reflected
 * IEEE CRC-32 its header documents (include/linux/crc32.h:19-28): polynomial
 * 0xedb88320 in integer form, bit order least-significant-bit-first, and no
 * inversion at either end (the caller does that if it wants it). The interface's
 * shipped implementation is a table-driven slicing-by-8 for speed; this port states
 * the definitional bit-at-a-time form, which computes the same value - the table is
 * an optimisation of exactly this recurrence.
 */
u32 crc32_le(u32 crc, const void *p, size_t len)
{
	const u8 *q = (const u8 *)p;

	while (len--) {
		int i;

		crc ^= *q++;
		for (i = 0; i < 8; i++)
			crc = (crc >> 1) ^ (0xedb88320u & (u32)-(int)(crc & 1u));
	}
	return crc;
}

/*
 * [LNX] lib/vsprintf.c:3011-3023 (vscnprintf) and 3065-3072 (scnprintf): format
 * like snprintf() but return the number of bytes actually stored, never the length
 * the format would have produced. The port's own printf is the formatter
 * (linuxkpi_core.h declares vsnprintf); the clamp is the interface's.
 */
static int lkpi_vscnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	int i;

	if (size == 0)
		return 0;

	i = vsnprintf(buf, size, fmt, args);
	if ((size_t)i < size)
		return i;

	return (int)(size - 1);
}

int scnprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = lkpi_vscnprintf(buf, size, fmt, args);
	va_end(args);

	return i;
}

/*
 * [LNX] fs/sysfs/file.c: sysfs_emit().  The interface formats a show caller's
 * answer into its PAGE_SIZE buffer and clamps the count the way scnprintf does;
 * this port has no sysfs, so the body is the formatting half and the size rule.
 * rtw89's USB unit names it for its efuse/uuid attributes.
 */
int sysfs_emit(char *buf, const char *fmt, ...)
{
	va_list args;
	int i;

	if (buf == NULL || fmt == NULL)
		return 0;
	va_start(args, fmt);
	i = lkpi_vscnprintf(buf, PAGE_SIZE, fmt, args);
	va_end(args);

	return i;
}

/*
 * [LNX] include/linux/math64.h:328 (declaration) and lib/math/div64.c:256-341 (the
 * interface's body): the full 128-bit product a * b + c, divided by d, answering ~0
 * when the quotient does not fit a u64. Two arms state the same contract:
 *   - a compiler with a 128-bit integer (the 64-bit board) computes the product with
 *     it - the interface's own __int128 arm, kept verbatim;
 *   - a 32-bit target has no such type (Cortex-M4), so the product is built from
 *     32-bit limbs and the quotient is restored bit by bit; neither a 128-bit type nor
 *     a library 64-bit-divide call is needed there.
 * The interface faults on a zero divisor (it divides by a hidden zero to raise a trap);
 * a freestanding port refuses instead, answering ~0. A carry out of the 128-bit
 * product+c sum answers ~0 for the same reason: the quotient could not be the truncated
 * value. (For u64 inputs that carry cannot actually occur - (2^64-1)^2 + (2^64-1) <
 * 2^128 - so the rule is stated, not exercised.)
 */
/* LKPI_MULDIV_BEGIN - the host differential test extracts this block verbatim */
#if defined(__SIZEOF_INT128__)
u64 mul_u64_add_u64_div_u64(u64 a, u64 b, u64 c, u64 d)
{
	unsigned __int128 prod = (unsigned __int128)a * b + c;
	unsigned __int128 q;

	if (d == 0)
		return ~0ULL;

	q = prod / d;
	if (q > (unsigned __int128)~0ULL)
		return ~0ULL;

	return (u64)q;
}
#else /* !__SIZEOF_INT128__: the 32-bit target */
/*
 * The 128-bit sum a * b + c in 32-bit limbs: p0 = a0*b0 sits at bit 0, p1 = a0*b1
 * and p2 = a1*b0 at bit 32, p3 = a1*b1 at bit 64. The middle pair is added first;
 * its bit 64 (`mid < p1`) is part of the high half's `mid >> 32` term, hence the
 * `(mid < p1) << 32`. Every partial sum is bounded by the exact result, so no u64
 * overflows, and each product is a 32x32->64 the target multiplies natively (UMULL).
 * Returns 1 when the sum does not fit 128 bits.
 */
static int lkpi_mul_u64_add_u64(u64 a, u64 b, u64 c, u64 *hi, u64 *lo)
{
	u32 a0 = (u32)a, a1 = (u32)(a >> 32);
	u32 b0 = (u32)b, b1 = (u32)(b >> 32);
	u64 p0 = (u64)a0 * b0;
	u64 p1 = (u64)a0 * b1;
	u64 p2 = (u64)a1 * b0;
	u64 p3 = (u64)a1 * b1;
	u64 mid = p1 + p2;
	u64 h, low, carry;

	h = p3 + (mid >> 32) + ((u64)(mid < p1) << 32);
	low = p0 + ((mid & 0xffffffffULL) << 32);
	carry = (low < p0);
	*lo = low + c;
	carry += (*lo < low);
	*hi = h + carry;

	return *hi < h;
}

u64 mul_u64_add_u64_div_u64(u64 a, u64 b, u64 c, u64 d)
{
	u64 hi, lo, r, q = 0;
	int i;

	if (d == 0)
		return ~0ULL;
	if (lkpi_mul_u64_add_u64(a, b, c, &hi, &lo))
		return ~0ULL;
	if (hi >= d)		/* the quotient would not fit a u64 */
		return ~0ULL;

	/*
	 * Restoring long division of hi:lo by d, one quotient bit at a time from the
	 * most significant. hi < d keeps the remainder in range; the doubled remainder
	 * needs a 65th bit only when it was already >= 2^63, and then it is larger than
	 * d, so the subtract is taken unconditionally - the wrapped u64 low half of
	 * (2^64 + t - d) is the true remainder (which is < d < 2^64).
	 */
	r = hi;
	for (i = 63; i >= 0; i--) {
		u64 t = (r << 1) | ((lo >> i) & 1ULL);

		if ((r >> 63) || t >= d) {
			r = t - d;	/* modulo 2^64 is the true value here */
			q = (q << 1) | 1ULL;
		} else {
			r = t;
			q <<= 1;
		}
	}

	return q;
}
#endif /* __SIZEOF_INT128__ */
/* LKPI_MULDIV_END */

/* ------------------------------------------------------------------ */
/* net: the rate limiter and the RTNL lock                             */
/* ------------------------------------------------------------------ */

/*
 * [LNX] net/core/utils.c:39-42 (the interface's body) over
 * include/linux/ratelimit_types.h:9-10: the interface's default state allows
 * DEFAULT_RATELIMIT_BURST messages per DEFAULT_RATELIMIT_INTERVAL and then drops
 * them until the window rolls over. This port states that window-and-budget rule
 * directly (the interface wraps it in ___ratelimit()); the clock is the core's
 * jiffies. It is not a per-call-site state machine: the interface's net_ratelimit()
 * has one global state, which is what this mirrors.
 */
#define LKPI_RATELIMIT_INTERVAL	(5 * HZ)	/* [LNX] DEFAULT_RATELIMIT_INTERVAL */
#define LKPI_RATELIMIT_BURST	10		/* [LNX] DEFAULT_RATELIMIT_BURST */

int net_ratelimit(void)
{
	static unsigned long window_start;
	static int budget = LKPI_RATELIMIT_BURST;
	unsigned long now = jiffies;

	if (time_after(now, window_start + LKPI_RATELIMIT_INTERVAL)) {
		window_start = now;
		budget = LKPI_RATELIMIT_BURST;
	}
	if (budget > 0) {
		budget--;
		return 1;
	}
	return 0;
}

/*
 * [LNX] net/core/rtnetlink.c:78-81 (rtnl_lock) and 154-158 (rtnl_unlock): the one
 * global lock that serialises network-configuration changes. The interface takes a
 * static mutex; this port does the same, over the core's mutex (a blocking lock, as
 * the interface's is - the driver takes it at passive level). The interface's unlock
 * also drains a netdev todo list; this port's netdev seam has no such list, so the
 * release is the whole of it.
 */
static struct mutex lkpi_rtnl_mutex;

void rtnl_lock(void)
{
	lkpi_mutex_lock(&lkpi_rtnl_mutex);
}

void rtnl_unlock(void)
{
	lkpi_mutex_unlock(&lkpi_rtnl_mutex);
}

/*
 * [LNX] include/linux/lockdep.h:389: the query the interface keeps even with the
 * validator off. There is no validator here to answer "is this lock held", and the
 * interface's off-arm treats a lock as held (lockdep_assert_held() is
 * __assume_ctx_lock, and lockdep_is_held_type() is the constant 1); this port's
 * answer is that same constant, so a caller that guards a dereference with it takes
 * the "held" path, exactly as it would with the validator compiled out.
 */
int lockdep_is_held(const void *l)
{
	(void)l;
	return 1;
}

/* ------------------------------------------------------------------ */
/* uevent and the RCU/IRQ synchronizes                                 */
/* ------------------------------------------------------------------ */

/*
 * [LNX] lib/kobject_uevent.c (the interface's body) over include/linux/kobject.h:282:
 * the interface builds a uevent and delivers it to userspace over netlink. This port
 * has no userspace listener and no netlink socket, so there is nothing to deliver
 * the event to and the call is the honest empty answer (success, no event sent). It
 * is stated rather than dropped because a compiled caller reaches the name and must
 * link; the driver's one call site (a PCIe link-state change notification) is a
 * courtesy to userspace and carries no driver-visible result.
 */
int kobject_uevent_env(struct kobject *kobj, enum kobject_action action,
		       char *envp[])
{
	(void)kobj;
	(void)action;
	(void)envp;
	return 0;
}

/*
 * [LNX] include/linux/netdevice.h:90 (declaration): the interface waits for RCU
 * readers and drains the rtnl todo list. This port's synchronize_rcu() is a no-op
 * (there is no grace period to wait for) and its RTNL has no todo list, so the body
 * is empty - the same effective synchronize the core already records for RCU.
 */
void synchronize_net(void)
{
	/* [LNX] net/core/dev.c: the interface waits for RCU readers and drains the
	 * rtnl todo list.  This port's RTNL has no todo list; the RCU half is the
	 * reader-count grace period (linuxkpi_core.c). */
	lkpi_synchronize_rcu();
}

/*
 * [LNX] include/linux/hardirq.h:13 (declaration): the interface waits until no
 * handler is running on the line. On this port a handler runs to completion before
 * the engine returns from the interrupt (the driver's completion work is deferred to
 * the workqueue, not to a nested interrupt context), so there is never an in-flight
 * handler to wait for and the wait is empty.
 */
void synchronize_irq(unsigned int irq)
{
	(void)irq;
}

/* ------------------------------------------------------------------ */
/* the memory-allocator surface, over the port's one heap              */
/* ------------------------------------------------------------------ */

/*
 * [LNX] include/linux/slab.h:558-593 (the declaration and its documented semantics):
 * reallocate, preserving the contents up to the lesser of the old and new sizes,
 * behaving like kmalloc() when the old pointer is NULL and freeing when the new size
 * is zero. This port's heap carries each block's size in its header (lkpi_kmem_size),
 * which is what lets the body copy exactly the old bytes; on a shrink the block
 * already holds the requested bytes, so it is returned as-is (the interface permits
 * either the same or a moved pointer).
 */
void *krealloc(const void *p, size_t new_size, gfp_t flags)
{
	size_t old_size;
	void *np;

	if (p == NULL)
		return kmalloc(new_size, flags);
	if (new_size == 0) {
		kfree(p);
		return NULL;
	}

	old_size = lkpi_kmem_size(p);
	if (old_size >= new_size)
		return (void *)(uintptr_t)p;

	np = kmalloc(new_size, flags);
	if (np == NULL)
		return NULL;	/* the old block is left untouched, as the interface promises */
	memcpy(np, p, old_size);
	kfree(p);
	return np;
}

/*
 * [LNX] include/linux/slab.h:1422: free either a kmalloc or a vmalloc block. On this
 * port vmalloc() is the same heap (the contract header says why), so the free is the
 * core's kfree().
 */
void kvfree(const void *addr)
{
	kfree(addr);
}

/*
 * [LNX] include/linux/string.h:304: the "kv" duplicate, matching kmemdup() but taking
 * a block from the kv family. The port's kv family is the kmalloc family, so the body
 * is the core's kmemdup().
 */
void *kvmemdup(const void *src, size_t len, gfp_t gfp)
{
	return kmemdup(src, len, gfp);
}

/*
 * [LNX] include/linux/slab.h:597 (declaration) and its documented contract: wipe the
 * object before returning it to the allocator, so a secret it held does not survive
 * in a freed block. The port's heap records each block's size, so the wipe covers
 * exactly the bytes that were handed out (and a foreign pointer wipes nothing rather
 * than reading a header it does not own).
 */
void kfree_sensitive(const void *objp)
{
	size_t n;

	if (objp == NULL)
		return;

	n = lkpi_kmem_size(objp);
	if (n != 0)
		memset((void *)(uintptr_t)objp, 0, n);
	kfree(objp);
}

/* ---- the named object cache ---- */

/*
 * [LNX] include/linux/slab.h:394-396,485,891,1308 (the declarations). The interface's
 * cache is a slab of same-sized objects; this port has one heap and no slab layer, so
 * the cache is a heap record naming an object size, and an object from it is a plain
 * heap block of that size (zeroed when the caller asked with __GFP_ZERO, which is what
 * kmem_cache_zalloc() sets). The free ignores the cache because the block's own header
 * already carries its size - the interface needs the cache there only because a slab
 * object has no per-object size record.
 */
struct kmem_cache {
	unsigned int	size;
	const char	*name;
};

struct kmem_cache *kmem_cache_create(const char *name, unsigned int size,
				     unsigned int align, slab_flags_t flags,
				     void (*ctor)(void *))
{
	struct kmem_cache *s;

	(void)align;
	(void)flags;
	(void)ctor;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (s == NULL)
		return NULL;
	s->size = size;
	s->name = name;
	return s;
}

void kmem_cache_destroy(struct kmem_cache *s)
{
	/* the interface tolerates a NULL cache (mm/slab_common.c:534) */
	if (s == NULL)
		return;
	kfree(s);
}

void *kmem_cache_alloc(struct kmem_cache *s, gfp_t flags)
{
	return lkpi_kmem_alloc(s->size, (int)flags, (flags & __GFP_ZERO) != 0);
}

void kmem_cache_free(struct kmem_cache *s, void *objp)
{
	(void)s;
	kfree(objp);
}

/* ---- the page family ---- */

/*
 * [LNX] include/linux/gfp.h:302-307 and 342-346, include/linux/mm.h:3104 (the
 * declarations). On this port a "page" is a buffer whose address is the handle:
 * the interface's struct page is opaque and every compiled caller only passes it
 * to page_address() (the CPU address) or back to __free_pages()/free_pages().
 *
 * The alignment is not a convenience here, it is the contract's load-bearing
 * part, and getting it wrong cost the AC 7260 a firmware ALIVE handshake: the
 * receive path hands each "page" to the device through the single-queue
 * descriptor form, which keeps the address shifted by eight bits
 * (third-party/iwlwifi/pcie/gen1_2/rx.c:137-140), so a pointer whose low eight
 * bits are not zero makes the device write to a different address than the one
 * the driver later reads - and the driver read the heap's stale bytes instead
 * of the notification ("frame on invalid queue - is on 0 and indicates 1",
 * then a firmware ALIVE that never arrived). The heap hands out headers, so
 * pages are carved here: the block is over-allocated by one page, the page
 * starts at the next page boundary, and the block's own base is kept in the
 * word before it for the release path.
 *
 * The record also carries the interface's reference count and the allocation
 * order: a page is released when its last reference goes (put_page /
 * __free_pages), and the vendored PCIe receive path leans on exactly that - one
 * order-N block serves several receive buffers, each holding its own reference
 * (third-party/iwlwifi/pcie/gen1_2/rx.c:380-410 and iwl-trans.h:252), and a
 * buffer whose page is stolen for a socket-buffer fragment releases the
 * receive queue's reference while the fragment's keeps the block alive until
 * the fragment is freed.
 *
 * A caller that requires the kernel's other page property (an allocator that
 * swaps, a struct page the kernel walks) is still not served by this, and is
 * the DMA face's concern, not this one's.
 */
struct lkpi_page_base {
	uintptr_t	base;	/* the heap block the page was carved from */
	uint32_t	refcnt;	/* the allocator's one plus one per get_page() */
	uint32_t	order;	/* kept so put_page() can free the block it names */
};

struct page *alloc_pages(gfp_t gfp_mask, unsigned int order)
{
	size_t size = (size_t)PAGE_SIZE << order;
	struct lkpi_page_base *hdr;
	uint8_t *raw, *page;
	uintptr_t aligned;

	raw = lkpi_kmem_alloc(size + PAGE_SIZE, (int)gfp_mask, false);
	if (raw == NULL) {
		/*
		 * The vendored receive path reports the refusal but not the
		 * request: its order-carrying line is debug-gated and its
		 * always-on line is not (third-party/iwlwifi/pcie/gen1_2/
		 * rx.c:394-403). Name the request once, so a refusal this port
		 * causes is diagnosable from the serial log alone - and a size
		 * of zero says the caller's order was garbage, which is the
		 * other defect the same line exposes.
		 */
		static bool said;

		if (!said) {
			said = true;
			lkpi_printf(ULOG_ERROR_LEVEL,
			    "lkpi: alloc_pages(order=%u) refused, %u bytes\n",
			    order, (unsigned)size);
		}
		return NULL;
	}

	aligned = ((uintptr_t)raw + sizeof(*hdr) + PAGE_SIZE - 1u) &
	    ~(uintptr_t)(PAGE_SIZE - 1u);
	hdr = (struct lkpi_page_base *)(aligned - sizeof(*hdr));
	hdr->base = (uintptr_t)raw;
	hdr->refcnt = 1;
	hdr->order = order;
	page = (uint8_t *)aligned;

	if ((gfp_mask & __GFP_ZERO) != 0)
		memset(page, 0, size);
	return (struct page *)page;
}

/* [LNX] include/linux/mm.h: the take on a page about to be handed to another
 * owner; the face's get_page() is this call. */
void lkpi_page_get(struct page *page)
{
	struct lkpi_page_base *hdr;

	if (page == NULL)
		return;
	hdr = (struct lkpi_page_base *)page - 1;
	hdr->refcnt++;
}

/* [LNX] include/linux/mm.h: `put_page()` drops one reference and returns the
 * block on the last. The interface reads the order back from its page record;
 * this port's header kept it at allocation, so no argument is needed here
 * either. A release below zero is named and refused rather than passed to the
 * allocator - the header bytes sit in the block being freed, so the check is
 * best-effort, and the heap's own guard is the backstop. */
void put_page(struct page *page)
{
	struct lkpi_page_base *hdr;

	if (page == NULL)
		return;
	hdr = (struct lkpi_page_base *)page - 1;
	if (hdr->refcnt == 0) {
		static bool said;

		if (!said) {
			said = true;
			lkpi_printf(ULOG_INFO_LEVEL,
			    "lkpi: release of a page with no references left refused (base=%p)\n",
			    (void *)hdr->base);
		}
		return;
	}
	if (--hdr->refcnt != 0)
		return;
	lkpi_kmem_free((const void *)hdr->base);
}

void __free_pages(struct page *page, unsigned int order)
{

	(void)order;		/* the record carries its own; see put_page() */
	put_page(page);
}

/*
 * The skbuff pool's fragment release binds here (skbuff_pool.h): the page
 * record and its reference count live in this file, so the binding is the real
 * one wherever this allocator is linked, overriding the pool's weak default.
 */
void lkpi_spl_page_put(struct page *page)
{

	put_page(page);
}

void free_pages(unsigned long addr, unsigned int order)
{

	__free_pages((struct page *)(uintptr_t)addr, order);
}

/* page_address() is the accessor's inline in include/linux/mm.h, not a body here:
 * a unit that reaches the page API only through it must not have to link this
 * file. */

/*
 * [LNX] lib/string.c: `char *strreplace(char *s, char old, char new)` - replace
 * every occurrence of one character, returning the string's end pointer (the
 * interface's return is the walk's end, not the start). brcmfmac uses it to turn
 * the NVRAM's comment markers into a form its parser accepts (common.c:397).
 */
char *
strreplace(char *s, char old, char new)
{
	for (; *s; ++s)
		if (*s == old)
			*s = new;
	return s;
}

/*
 * [LNX] lib/kasprintf.c / include/linux/sprintf.h:17: format into a freshly
 * allocated buffer. The interface asks vsnprintf() for the length with a NULL
 * buffer first (that is how it handles a format whose size it cannot know), then
 * allocates length+1 and formats again. This port's formatter is the component's
 * vsnprintf, which answers the "would have written" count when the buffer is NULL,
 * so the same two-pass body is the correct one here - and it is what brcmfmac's
 * firmware-path builder needs (firmware.c:638), a string whose size depends on the
 * board and chip names.
 */
char *
kasprintf(gfp_t gfp, const char *fmt, ...)
{
	va_list ap, aq;
	char *p;
	int n;

	if (fmt == NULL)
		return NULL;
	va_start(ap, fmt);
	va_copy(aq, ap);
	n = vsnprintf(NULL, 0, fmt, aq);
	va_end(aq);
	if (n < 0) {
		va_end(ap);
		return NULL;
	}
	p = kmalloc((size_t)n + 1u, gfp);
	if (p != NULL)
		(void)vsnprintf(p, (size_t)n + 1u, fmt, ap);
	va_end(ap);
	return p;
}

/*
 * [LNX] include/linux/interrupt.h:233-241: the interrupt line's mask/unmask calls
 * and the system-wake pair.
 *
 * The mask pair belongs to the out-of-band-interrupt path in brcmfmac: the driver
 * masks its own level-triggered line from inside the handler (bcmsdh.c:73) and
 * unmasks it once the deferred work has drained the device (sdio.c:2551). It
 * requests that handler only when its platform data says the board wires an OOB
 * interrupt (bcmsdh.c's brcmf_sdiod_intr_register), and this port has no platform
 * data at all, so the card's in-band SDIO interrupt is the only one used and no
 * caller reaches these. The engine's seam exposes no per-line mask either, so an
 * empty body is the honest statement of "there is no such line here" rather than
 * a mask that silently does nothing on a line that exists.
 *
 * The wake pair is the suspend/WOWL path (bcmsdh.c:120,125,1206,1248). This image
 * has no system sleep (linux/suspend.h's face; CONFIG_PM is off in the driver's
 * configuration face), so there is no sleep state for a wake source to be armed
 * against; the interface returns an error for a line that cannot wake the system,
 * and -EOPNOTSUPP is that answer here. It must not report success for an arm that
 * did not happen.
 */
void
disable_irq_nosync(unsigned int irq)
{
	(void)irq;
}

void
disable_irq(unsigned int irq)
{
	(void)irq;
}

void
enable_irq(unsigned int irq)
{
	(void)irq;
}

/*
 * [LNX] kernel/irq/manage.c: the out-of-band IRQ names.  The SDIO line hears
 * its card through DAT1 (sdio_claim_irq -> the engine's pump), not through an
 * OOB line: the board's WL_HOST_WAKE is PA0, shared with KEY1/WKUP, and the
 * port deliberately never registers it.  brcmfmac names these only in its
 * OOB arm, unreachable with no platform data; the bodies state "no such
 * line" rather than pretending one exists.
 */
int
request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
	    const char *name, void *dev)
{
	(void)irq; (void)handler; (void)flags; (void)name; (void)dev;
	return -EINVAL;
}

void
free_irq(unsigned int irq, void *dev_id)
{
	(void)irq; (void)dev_id;
}

int
enable_irq_wake(unsigned int irq)
{
	(void)irq;
	return -EOPNOTSUPP;
}

int
disable_irq_wake(unsigned int irq)
{
	(void)irq;
	return -EOPNOTSUPP;
}

/*
 * [LNX] include/linux/kstrtox.h:30-40 (lib/kstrtox.c): parse an unsigned long with
 * the interface's contract - leading whitespace is skipped, one optional '+', digits
 * in the given base, and the first character that is not a digit ends the number
 * (`\n` is accepted as the terminator because that is what a sysfs store leaves
 * behind). An empty or sign-invalid string is -EINVAL and a value that does not fit
 * is -ERANGE. The body is a straightforward digit loop because the port's 32-bit
 * `unsigned long` is the plain C type here (no _parse_integer_fixup_radix dependency).
 */
int
kstrtoul(const char *s, unsigned int base, unsigned long *res)
{
	unsigned long v = 0;
	int any = 0;

	if (s == NULL || res == NULL)
		return -EINVAL;
	if (base != 0 && (base < 2 || base > 36))
		return -EINVAL;
	while (*s == ' ' || *s == '\t' || *s == '\n')
		s++;
	if (*s == '+')
		s++;
	else if (*s == '-')
		return -EINVAL;
	if (base == 0) {
		if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
			base = 16;
			s += 2;
		} else if (s[0] == '0') {
			base = 8;
			s++;
		} else {
			base = 10;
		}
	} else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		s += 2;
	}
	for (;; s++) {
		unsigned int d;

		if (*s >= '0' && *s <= '9')
			d = (unsigned int)(*s - '0');
		else if (*s >= 'a' && *s <= 'z')
			d = (unsigned int)(*s - 'a') + 10u;
		else if (*s >= 'A' && *s <= 'Z')
			d = (unsigned int)(*s - 'A') + 10u;
		else
			break;
		if (d >= base)
			break;
		if (v > (~0UL - d) / base)
			return -ERANGE;
		v = v * base + d;
		any = 1;
	}
	if (*s != '\0' && *s != '\n')
		return -EINVAL;
	if (!any && base != 8)
		return -EINVAL;
	*res = v;
	return 0;
}

/*
 * [LNX] include/linux/kstrtox.h:111 (lib/kstrtox.c): the debugfs store form. The
 * interface copies at most a small buffer's worth of the caller's bytes into a
 * local string and parses that, which is what makes a short write behave like a
 * terminated string. This port has one address space, so copy_from_user is the
 * direct copy of asm/uaccess.h; the parse is the same kstrtou8 the rest of the face
 * declares. brcmfmac's "bus_reset" debugfs attribute is the caller (core.c:1213),
 * and its entry is only ever created when a debugfs exists - which this port does
 * not have - so the path is unreachable in an image but the body is a real parse,
 * not a refusal.
 */
/*
 * [LNX] lib/kstrtox.c:16-22: the 8-bit form.  The port's unsigned long is 32
 * bits, so the range check is the interface's own -ERANGE arm for a value
 * that does not fit a u8.
 */
int
kstrtou8(const char *s, unsigned int base, u8 *res)
{
	unsigned long v = 0;
	int rc;

	if (res == NULL)
		return -EINVAL;
	rc = kstrtoul(s, base, &v);
	if (rc != 0)
		return rc;
	if (v > 0xffUL)
		return -ERANGE;
	*res = (u8)v;
	return 0;
}

int
kstrtou8_from_user(const char __user *s, size_t count, unsigned int base, u8 *res)
{
	char buf[32];
	unsigned int len = (unsigned int)min_t(size_t, count, sizeof(buf) - 1u);

	if (copy_from_user(buf, s, len))
		return -EFAULT;
	buf[len] = '\0';
	return kstrtou8(buf, base, res);
}

/*
 * [LNX] fs/libfs.c / include/linux/fs.h: `simple_open()` - the opener that copies the
 * inode's private pointer into the open file and succeeds. It is the whole body
 * upstream, and this port's inode and file records carry the same two members, so the
 * transcription is exact. brcmfmac's bus-reset attribute names it (core.c:1224); the
 * debugfs entry that would call it is never created on this port, but the body is the
 * interface's, not a refusal.
 */
int
simple_open(struct inode *inode, struct file *file)
{
	if (inode->i_private != NULL)
		file->private_data = inode->i_private;
	return 0;
}
