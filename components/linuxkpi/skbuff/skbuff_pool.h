/*
 * lkpi skbuff pool - header
 *
 * Dual static pool for struct sk_buff headers and their linear data
 * buffers, descended from the Realtek/TizenRT pool (rtl8720e osdep
 * freertos_skbuff.c, Apache-2.0; snapshot runs/20261006-lkpi-stabilize/
 * refs/tizenrt-skbuff/).  Function-level lineage in skbuff/PROVENANCE.md,
 * deviations in skbuff/DIVERGENCES.md.
 *
 * The struct sk_buff definition lives HERE (moved from linuxkpi_core.h)
 * so the pool and the host-side contract harness compile the exact
 * production shape.  It stays the Linux-shaped shim contract: cb[192]
 * (the flat ieee80211_tx_info is 184 bytes - the 48-byte Linux cb
 * caused the mgmt-header wipe of 2026-10-05), offset-style tail/end.
 */
#ifndef _LKPI_SKBUFF_POOL_H
#define _LKPI_SKBUFF_POOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- pool sizing (overridable from configs) ----------------------- */
#ifndef CONFIG_LKPI_SKB_POOL_NUM
#define CONFIG_LKPI_SKB_POOL_NUM	48	/* skb header slots */
#endif
#ifndef CONFIG_LKPI_SKB_DATA_POOL_NUM
#define CONFIG_LKPI_SKB_DATA_POOL_NUM	48	/* data buffer slots */
#endif
#ifndef CONFIG_LKPI_SKB_DATA_BUF_SZ
#define CONFIG_LKPI_SKB_DATA_BUF_SZ	2048	/* per-slot buffer */
#endif

/* NET_SKB_PAD ([LNX] skbuff.h): the headroom dev_alloc_skb reserves so
 * a later push of a header cannot underrun (rtw89 usb.c pull/push pair
 * depends on it). */
#define NET_SKB_PAD	32

/* ---- struct sk_buff (Linux shape, moved from linuxkpi_core.h) ------ */
struct sk_buff_head;
struct net_device;
struct page;

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

	uint16_t	protocol;
	uint16_t	queue_mapping;
	uint32_t	priority;
	uint8_t		pkt_type;
	uint8_t		ip_summed;
	/* [LNX] skbuff.h: the running checksum a transmit path stamps onto a buffer
	 * it hands to a hardware-checksum device. */
	uint32_t	csum;

	/* slot state, pool-owned: 0 = never handed out, small positive =
	 * allocation generation of the current owner, LKPI_SKB_FREED_MAGIC
	 * = released back to the free list.  A put/push on an skb whose
	 * value is the magic is a use-after-free write and is named. */
	uint32_t	_gen;

	struct net_device *dev;	/* the interface this buffer is on, as on the interface
				 * ([LNX] skbuff.h: `struct net_device *dev`); typed, not
				 * void, because the drivers read fields through it */
	void		*sk;

	/* mac80211 control/status block (IEEE80211_SKB_CB lives here).
	 * 192, not Linux's 48: this shim's flat ieee80211_tx_info is 184
	 * bytes and lkpi_n80211_drv_tx_mgmt memsets sizeof(tx_info)
	 * through the cb cast - with 48 that memset wiped the first 24
	 * payload bytes of every mgmt frame (2026-10-05 board evidence).
	 * Keep in lockstep with sizeof (static-asserted in
	 * linuxkpi_80211.c). */
	unsigned long	cb[192 / sizeof(unsigned long)];
} __attribute__((aligned(4)));

#define LKPI_SKB_FREED_MAGIC	0xf2eef2eeU

/* ---- pool API ------------------------------------------------------ */
struct lkpi_skb_pool_stats {
	uint32_t	skb_used, skb_high;	/* header slots */
	uint32_t	data_used, data_high;	/* data slots */
	uint32_t	dyalloc_live;		/* heap-backed data in flight */
	uint32_t	dyalloc_oversize;	/* requests > DATA_BUF_SZ */
	uint32_t	slot_overflow;		/* header pool exhausted -> heap */
	uint32_t	data_overflow;		/* data pool exhausted -> heap */
	uint32_t	alloc_fail;		/* everything failed */
	uint32_t	n_alloc, n_free;
	uint32_t	double_free;		/* named and skipped */
	uint32_t	stray_free;		/* pointer outside the pool */
	uint32_t	uaf_write;		/* put/push on a freed slot */
	uint32_t	fence_bad;		/* free-slot integrity scan */
};

/* idempotent; called lazily from the allocator under the pool's
 * critical section (first alloc wins, callers may also call it early) */
void lkpi_skb_pool_init(void);

/*
 * Select the pool's geometry and reset it: the compiled CONFIG_ macros are upper
 * bounds, a request above them (or empty) is refused with a non-zero return and
 * the geometry stays unchanged.  A build that never calls this runs at the
 * compiled geometry - the board configuration; the host contract harness selects
 * a scenario geometry per run, the same way the fact source's weak geometry
 * variables let a board choose its pool sizes.  Resetting invalidates every live
 * object by contract.  Returns 0 on success.
 */
int lkpi_skb_pool_configure(unsigned int n_header, unsigned int n_data,
			    unsigned int data_sz, int heap_fallback);

/* the three symbols the linuxkpi_core.h macros have always mapped to;
 * only the implementation moved here */
struct sk_buff *lkpi_skb_alloc(unsigned int size, unsigned int gfp);
struct sk_buff *lkpi_dev_alloc_skb(unsigned int size, unsigned int gfp);
void lkpi_skb_free(struct sk_buff *skb);

void lkpi_skb_pool_stats(struct lkpi_skb_pool_stats *out);
/* emergency outlet: called from the heap assert path, where the normal
 * print lock can be held (board_log_emergency semantics) */
void lkpi_skb_gen_report(void);
/* put/push on a freed slot: the geometry entry points in the pool body call
 * this when they see the release magic on a live-looking skb */
void lkpi_skb_uaf_hit(struct sk_buff *skb);
/* full counters, normal console */
void lkpi_skb_diag_dump(void);
/* periodic free-slot integrity scan; returns nothing but bumps
 * stats.fence_bad and names the first hit.  Heap-side caller invokes
 * it through its weak declaration. */
void lkpi_skb_fence_scan(void);

/* ---- platform hooks (target vs host contract harness) -------------
 * The pool body is platform-pure; these are the only bindings.  The
 * declarations are visible in both builds (the host contract harness
 * provides its own definitions); the target definitions live at the
 * bottom of skbuff_pool.c. */
unsigned long lkpi_spl_irqsave(void);
void lkpi_spl_irqrestore(unsigned long flags);
void lkpi_spl_log(const char *fmt, ...);
void *lkpi_spl_malloc(unsigned int size);	/* dyalloc backing */
void lkpi_spl_free(void *p);
/* [LNX] include/linux/mm.h: return one reference to a page (put_page).  The
 * fragment release calls it when a payload with rx fragments dies; the target
 * binds it to the interface's own put_page, a host harness to whatever its page
 * model needs. */
void lkpi_spl_page_put(struct page *page);

#ifdef __cplusplus
}
#endif
#endif /* _LKPI_SKBUFF_POOL_H */
