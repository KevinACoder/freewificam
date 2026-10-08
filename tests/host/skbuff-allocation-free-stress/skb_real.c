/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * skb_real.c - the production pool body behind the same observation face.
 *
 * The harness's matrix, judge and fuzz layers are written against the
 * observation face skb_contract.h declares (skb_double_*), which the double
 * (skb_contract.c) implements over its own storage.  This file implements the
 * same face over the *production* pool body, so the identical nineteen-cell
 * matrix, the identical seeded streams and the identical judge run against the
 * code that ships, not against a model of it.
 *
 * The one technique this needs: the production translation unit is included
 * directly, because a compiled pool keeps its slot arrays and counters static
 * (they must stay static - the pool owns its storage and no consumer may reach
 * past the API).  Including it here is what lets the observation face read the
 * exact arrays the production code mutates; every observation below is a read
 * of that real state, not a mirror of it.
 *
 * What changes between this build and the board build is only the platform
 * hooks: this build defines LKPI_SKB_POOL_NO_TARGET_HOOKS and supplies the
 * host's critical-section section and allocator (the same role the double's
 * hooks play), and maps the linuxkpi spinlock pair the queue operations take
 * onto that same section - this harness is single threaded, so the section is
 * what the production mapping's interrupt mask also provides, nothing weaker.
 *
 * The two builds of this adapter differ only in the compiled geometry bounds:
 * the default build carries the production bounds, the companion build is
 * compiled with the constrained bounds (eight slots, 256-byte payloads) to
 * prove the body stays inside an MCU-shaped budget - including at build time,
 * through the pool's own storage-budget assertion.
 */

#include "skb_contract.h"

/* the production body, with the host's hooks instead of the target's */
#define LKPI_SKB_POOL_NO_TARGET_HOOKS 1
#include "linuxkpi/skbuff/skbuff_pool.c"

/* ------------------------------------------------------------------ */
/* platform hooks (the only bindings the production pool has)          */
/* ------------------------------------------------------------------ */

static int verbose;

void
skb_double_set_verbose(int on)
{

	verbose = on != 0;
}

void
lkpi_spl_log(const char *fmt, ...)
{
	va_list ap;

	if (!verbose)
		return;
	fputs("lkpi_skb: ", stdout);
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fputc('\n', stdout);
	fflush(stdout);
}

unsigned long
lkpi_spl_irqsave(void)
{

	return 0;		/* single-threaded host run */
}

void
lkpi_spl_irqrestore(unsigned long flags)
{

	(void)flags;
}

void *
lkpi_spl_malloc(unsigned int size)
{

	return malloc(size != 0 ? size : 1u);
}

void
lkpi_spl_free(void *p)
{

	free(p);
}

/*
 * The queue operations take the queue's own spinlock, which on the target is
 * the port's interrupt-mask section (os/cmsis2/cmsis2_ext.h).  A host run has
 * no interrupt to mask and no second thread, so both takes map onto the same
 * hook pair above - the section is preserved (an operation cannot be split),
 * which is the whole contract the queue lock carries here.
 */
unsigned long long
lkpi_spin_lock_irqsave(spinlock_t *l)
{

	(void)l;
	return (unsigned long long)lkpi_spl_irqsave();
}

void
lkpi_spin_unlock_irqrestore(spinlock_t *l, unsigned long long flags)
{

	(void)l;
	lkpi_spl_irqrestore((unsigned long)flags);
}

/* ------------------------------------------------------------------ */
/* the observation face over the production storage                    */
/* ------------------------------------------------------------------ */

static const struct skb_pool_cfg profiles[2] = {
	/* host: the geometry this build compiled for (the production bounds, or
	 * the constrained bounds in the companion build) */
	{ .n_header = CONFIG_LKPI_SKB_POOL_NUM,
	  .n_data = CONFIG_LKPI_SKB_DATA_POOL_NUM,
	  .data_sz = CONFIG_LKPI_SKB_DATA_BUF_SZ,
	  .heap_fallback = 1 },
	/* mcu: the constrained profile, fixed geometry, no heap behind it */
	{ .n_header = 8, .n_data = 8, .data_sz = 256, .heap_fallback = 0 },
};

static struct skb_pool_cfg	cfg;
static enum skb_profile		profile_sel = SKB_PROFILE_HOST;
static int			cfg_custom;

static void
geometry_select(const struct skb_pool_cfg *c)
{
	unsigned int i;

	if (c->n_header == 0 || c->n_data == 0 || c->data_sz == 0 ||
	    c->n_header > SKB_DOUBLE_MAX_HDR ||
	    c->n_data > SKB_DOUBLE_MAX_DATA ||
	    c->data_sz > SKB_DOUBLE_MAX_DATA_SZ) {
		lkpi_spl_log("requested geometry is empty or exceeds the harness's request domain");
		abort();
	}
	if (lkpi_skb_pool_configure(c->n_header, c->n_data, c->data_sz,
	    c->heap_fallback) != 0) {
		/*
		 * The compiled bounds refuse the geometry.  This only happens when
		 * the harness asks for more than the body was built for, i.e. the
		 * run is driven against the wrong binary - never silently continue
		 * at some other geometry.
		 */
		fprintf(stdout,
		    "SKBST: geometry header=%u data=%u data_sz=%u is refused by the compiled pool bounds\n",
		    c->n_header, c->n_data, c->data_sz);
		fflush(stdout);
		abort();
	}
	cfg = *c;
	/* remember whether the request still matches a named preset */
	cfg_custom = 1;
	for (i = 0; i < 2; i++)
		if (cfg.n_header == profiles[i].n_header &&
		    cfg.n_data == profiles[i].n_data &&
		    cfg.data_sz == profiles[i].data_sz &&
		    cfg.heap_fallback == profiles[i].heap_fallback) {
			profile_sel = (enum skb_profile)i;
			cfg_custom = 0;
		}
}

void
skb_double_init_cfg(const struct skb_pool_cfg *c)
{

	geometry_select(c);
}

void
skb_double_init(enum skb_profile profile)
{

	if (profile != SKB_PROFILE_HOST && profile != SKB_PROFILE_MCU) {
		lkpi_spl_log("unknown profile %d, refusing to initialise", (int)profile);
		abort();
	}
	geometry_select(&profiles[profile]);
}

void
skb_double_reset(void)
{

	/* same geometry, fresh generations, empty free state */
	geometry_select(&cfg);
}

const struct skb_pool_cfg *
skb_double_cfg(void)
{

	return &cfg;
}

const char *
skb_double_profile_name(void)
{

	if (cfg_custom)
		return "custom";
	return profile_sel == SKB_PROFILE_MCU ? "mcu" : "host";
}

unsigned int
skb_double_live_count(void)
{

	return stats.skb_used + heap_live_n;
}

int
skb_double_visit_live(skb_live_fn fn, void *arg)
{
	uint32_t i;
	int rc;

	for (i = 0; i < cfg_n_header; i++) {
		const struct sk_buff *skb = &skb_slots[i];
		uint32_t g = skb->_gen;

		if (g == 0 || g == LKPI_SKB_FREED_MAGIC)
			continue;
		rc = fn(&skb_slots[i], (int)i,
		    slot_buf[i].kind == LKPI_SKB_BUF_POOL, arg);
		if (rc != 0)
			return rc;
	}
	for (i = 0; i < heap_live_n; i++) {
		rc = fn(heap_live[i].p, -1,
		    heap_live[i].buf.kind == LKPI_SKB_BUF_POOL, arg);
		if (rc != 0)
			return rc;
	}
	return 0;
}

/* the production index rule: a slot of the compiled array at a stride
 * boundary, interior pointers and foreign addresses are -1 */
static int
real_slot_index(const struct sk_buff *skb)
{
	int slot = pool_slot_index(skb);

	if (slot < 0 || slot >= (int)cfg_n_header)
		return -1;
	return slot;
}

int
skb_double_is_pool_slot(const struct sk_buff *skb)
{

	return real_slot_index(skb) >= 0;
}

int
skb_double_slot_index(const struct sk_buff *skb)
{

	return real_slot_index(skb);
}

int
skb_double_is_released(const struct sk_buff *skb)
{
	int slot = skb != NULL ? pool_slot_index(skb) : -1;

	if (slot >= 0 && slot < (int)cfg_n_header)
		return skb_slots[slot]._gen == LKPI_SKB_FREED_MAGIC;
	if (skb == NULL)
		return 0;
	/*
	 * A heap address lives again as soon as the allocator hands it out, so
	 * "still released" has to mean "in the free window and not live now".
	 */
	if (heap_live_find(skb) >= 0)
		return 0;
	return tomb_find((uintptr_t)skb);
}

struct sk_buff *
skb_double_slot_at(unsigned int idx)
{

	if (idx >= LKPI_SKB_SLOT_MAX) {
		lkpi_spl_log("slot index %u is outside the compiled array", idx);
		abort();
	}
	return &skb_slots[idx];
}

unsigned char *
skb_double_slot_buffer(struct sk_buff *skb)
{
	int slot = real_slot_index(skb);

	if (slot < 0)
		return NULL;
	if (slot_buf[slot].kind == LKPI_SKB_BUF_POOL)
		return skb_data[slot_buf[slot].idx].buf;
	return NULL;
}

unsigned char *
skb_double_released_buffer(struct sk_buff *skb)
{
	int slot = real_slot_index(skb);

	if (slot < 0)
		return NULL;
	if (slot_buf[slot].kind != LKPI_SKB_BUF_POOL)
		return NULL;
	/* only while the data slot is actually free: a recycled slot would
	 * describe some newer owner's payload */
	if (skb_data[slot_buf[slot].idx].ref != 0)
		return NULL;
	return skb_data[slot_buf[slot].idx].buf;
}

/* ------------------------------------------------------------------ */
/* the state digest                                                    */
/* ------------------------------------------------------------------ */

#define DG_OFFSET	0xcbf29ce484222325ULL
#define DG_PRIME	0x100000001b3ULL

static uint64_t
dg_bytes(uint64_t h, const void *p, size_t n)
{
	const unsigned char *b = (const unsigned char *)p;
	size_t i;

	for (i = 0; i < n; i++) {
		h ^= b[i];
		h *= DG_PRIME;
	}
	return h;
}

static uint64_t
dg_u64(uint64_t h, uint64_t v)
{
	unsigned int i;

	for (i = 0; i < 8; i++) {
		h ^= (v >> (8u * i)) & 0xffu;
		h *= DG_PRIME;
	}
	return h;
}

static uint64_t
dg_live_payload(uint64_t h, const struct sk_buff *skb)
{
	uintptr_t off;

	if (skb->head == NULL || skb->data == NULL || skb->len == 0)
		return h;
	if ((uintptr_t)skb->data < (uintptr_t)skb->head)
		return h;
	off = (uintptr_t)skb->data - (uintptr_t)skb->head;
	if (off > skb->end || skb->len > skb->end - off)
		return h;
	return dg_bytes(h, skb->data, skb->len);
}

uint64_t
skb_double_digest(void)
{
	uint64_t h = DG_OFFSET;
	uint32_t i;

	h = dg_bytes(h, &stats, sizeof(stats));
	h = dg_u64(h, gen_counter);
	h = dg_u64(h, geo_bad);
	h = dg_u64(h, heap_live_n);
	/* pointer values are deliberately not part of the digest */
	for (i = 0; i < cfg_n_header; i++) {
		h = dg_u64(h, skb_slots[i]._gen);
		h = dg_u64(h, skb_slots[i].end);
		h = dg_u64(h, skb_slots[i].tail);
		h = dg_u64(h, skb_slots[i].len);
		h = dg_u64(h, (uint64_t)slot_buf[i].kind);
		h = dg_u64(h, (uint64_t)(unsigned int)slot_buf[i].idx);
		h = dg_live_payload(h, &skb_slots[i]);
	}
	for (i = 0; i < cfg_n_data; i++) {
		h = dg_u64(h, skb_data[i].ref);
		if (skb_data[i].ref == 0)
			h = dg_bytes(h, skb_data[i].buf, cfg_data_sz);
	}
	for (i = 0; i < heap_live_n; i++) {
		h = dg_u64(h, heap_live[i].p->_gen);
		h = dg_u64(h, (uint64_t)heap_live[i].buf.kind);
		h = dg_u64(h, (uint64_t)(unsigned int)heap_live[i].buf.idx);
		h = dg_live_payload(h, heap_live[i].p);
	}
	h = dg_u64(h, tomb_filled);
	h = dg_u64(h, tomb_head);
	return h;
}
