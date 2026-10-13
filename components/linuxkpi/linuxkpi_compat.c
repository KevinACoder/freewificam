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

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
 * [LNX] include/linux/math64.h:328 (declaration) and lib/math/div64.c:256-341 (the
 * interface's body): the full 128-bit product a * b + c, divided by d, answering ~0
 * when the quotient does not fit a u64. The interface has an __int128 arm that
 * computes exactly this product; this port compiles for a target whose compiler
 * provides the 128-bit integer, so the body is that product and the interface's own
 * overflow rule. The interface faults on a zero divisor (it divides by a hidden
 * zero to raise a trap); a freestanding port refuses instead, answering ~0.
 */
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
 * A caller that requires the kernel's other page property (an allocator that
 * swaps, a struct page the kernel walks) is still not served by this, and is
 * the DMA face's concern, not this one's.
 */
struct lkpi_page_base {
	uintptr_t	base;	/* the heap block the page was carved from */
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
	page = (uint8_t *)aligned;

	if ((gfp_mask & __GFP_ZERO) != 0)
		memset(page, 0, size);
	return (struct page *)page;
}

void __free_pages(struct page *page, unsigned int order)
{
	const struct lkpi_page_base *hdr;

	(void)order;
	if (page == NULL)
		return;
	hdr = (const struct lkpi_page_base *)page - 1;
	lkpi_kmem_free((const void *)hdr->base);
}

void free_pages(unsigned long addr, unsigned int order)
{

	__free_pages((struct page *)(uintptr_t)addr, order);
}

/* page_address() is the accessor's inline in include/linux/mm.h, not a body here:
 * a unit that reaches the page API only through it must not have to link this
 * file. */
