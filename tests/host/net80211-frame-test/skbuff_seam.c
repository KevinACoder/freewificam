/*
 * @file    skbuff_seam.c
 * @brief   The host backend of the driver-side sk_buff seam.
 *
 * The API and the named counters are the repository's contract header
 * (linuxkpi/skbuff/skbuff_pool.h); this file provides the host backend the
 * mock needs - a small static slot table with heap-backed data, and the
 * named accounting for the misuse classes (double free, stray pointer, a
 * write through a released slot, a broken free slot). The header slots are
 * owned by the table and never handed to the system allocator, which is what
 * lets a released slot still be recognised as released.
 *
 * @design adapter - ref linuxkpi:skbuff/skbuff_pool.h:102
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linuxkpi/skbuff/skbuff_pool.h>

#include "skbuff_seam.h"

#define SEAM_SLOTS 16

struct seam_slot {
	struct sk_buff *skb;	/* the slot's header storage, stable */
	void *head;		/* the data buffer of the current owner */
};

static struct seam_slot seam_slots[SEAM_SLOTS];
static struct lkpi_skb_pool_stats seam_counters;
static uint32_t seam_gen;
static int seam_ready;

/* ------------------------------------------------------------------ */
/* the platform hooks of the contract header                           */
/* ------------------------------------------------------------------ */

unsigned long lkpi_spl_irqsave(void)
{
	return 0;		/* the host run is single threaded here */
}

void lkpi_spl_irqrestore(unsigned long flags)
{
	(void) flags;
}

void lkpi_spl_log(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

void *lkpi_spl_malloc(unsigned int size)
{
	return malloc(size);
}

void lkpi_spl_free(void *p)
{
	free(p);
}

/* ------------------------------------------------------------------ */
/* the pool API                                                        */
/* ------------------------------------------------------------------ */

void lkpi_skb_pool_init(void)
{
	if (seam_ready)
		return;
	memset(seam_slots, 0, sizeof(seam_slots));
	memset(&seam_counters, 0, sizeof(seam_counters));
	seam_gen = 0;
	seam_ready = 1;
}

static struct seam_slot *seam_slot_of(const struct sk_buff *skb)
{
	unsigned i;

	for (i = 0; i < SEAM_SLOTS; i++) {
		if (seam_slots[i].skb == skb)
			return &seam_slots[i];
	}
	return NULL;
}

struct sk_buff *lkpi_skb_alloc(unsigned int size, unsigned int gfp)
{
	struct seam_slot *slot = NULL;
	struct sk_buff *skb;
	void *head;
	unsigned int buflen;
	unsigned i;

	(void) gfp;
	lkpi_skb_pool_init();
	if (size == 0) {
		seam_counters.alloc_fail++;
		return NULL;
	}
	buflen = NET_SKB_PAD + size;
	if (buflen < size) {		/* the addition wrapped */
		seam_counters.alloc_fail++;
		return NULL;
	}
	for (i = 0; i < SEAM_SLOTS; i++) {
		if (seam_slots[i].skb == NULL) {
			slot = &seam_slots[i];
			break;
		}
		if (seam_slots[i].skb->_gen == LKPI_SKB_FREED_MAGIC &&
		    seam_slots[i].head == NULL) {
			slot = &seam_slots[i];	/* reusable: header is stable */
			break;
		}
	}
	if (slot == NULL) {
		seam_counters.slot_overflow++;
		seam_counters.alloc_fail++;
		return NULL;
	}
	if (slot->skb == NULL) {
		slot->skb = calloc(1, sizeof(*slot->skb));
		if (slot->skb == NULL) {
			seam_counters.alloc_fail++;
			return NULL;
		}
	}
	head = calloc(1, buflen);
	if (head == NULL) {
		seam_counters.alloc_fail++;
		return NULL;
	}
	skb = slot->skb;
	memset(skb, 0, sizeof(*skb));
	skb->head = head;
	skb->data = (unsigned char *) head + NET_SKB_PAD;
	skb->tail = NET_SKB_PAD;
	skb->end = buflen;
	skb->truesize = (unsigned int) sizeof(*skb) + buflen;
	skb->_gen = ++seam_gen;
	slot->head = head;

	seam_counters.n_alloc++;
	seam_counters.skb_used++;
	seam_counters.data_used = seam_counters.skb_used;
	if (seam_counters.skb_used > seam_counters.skb_high)
		seam_counters.skb_high = seam_counters.skb_used;
	return skb;
}

struct sk_buff *lkpi_dev_alloc_skb(unsigned int size, unsigned int gfp)
{
	return lkpi_skb_alloc(size, gfp);
}

void lkpi_skb_free(struct sk_buff *skb)
{
	struct seam_slot *slot;

	if (skb == NULL)
		return;
	slot = seam_slot_of(skb);
	if (slot == NULL) {
		seam_counters.stray_free++;
		return;
	}
	if (skb->_gen == LKPI_SKB_FREED_MAGIC) {
		seam_counters.double_free++;	/* named and skipped */
		return;
	}
	free(slot->head);
	slot->head = NULL;
	skb->_gen = LKPI_SKB_FREED_MAGIC;
	skb->head = NULL;
	skb->data = NULL;
	seam_counters.n_free++;
	if (seam_counters.skb_used > 0)
		seam_counters.skb_used--;
	seam_counters.data_used = seam_counters.skb_used;
}

void lkpi_skb_pool_stats(struct lkpi_skb_pool_stats *out)
{
	if (out != NULL)
		*out = seam_counters;
}

void lkpi_skb_uaf_hit(struct sk_buff *skb)
{
	(void) skb;
	seam_counters.uaf_write++;
}

void lkpi_skb_gen_report(void)
{
	lkpi_skb_diag_dump();
}

void lkpi_skb_diag_dump(void)
{
	fprintf(stderr, "seam: alloc=%u free=%u double=%u stray=%u uaf=%u "
	    "fence=%u fail=%u\n", seam_counters.n_alloc, seam_counters.n_free,
	    seam_counters.double_free, seam_counters.stray_free,
	    seam_counters.uaf_write, seam_counters.fence_bad,
	    seam_counters.alloc_fail);
}

void lkpi_skb_fence_scan(void)
{
	unsigned i;

	for (i = 0; i < SEAM_SLOTS; i++) {
		struct sk_buff *skb = seam_slots[i].skb;

		if (skb == NULL)
			continue;
		if (skb->_gen == LKPI_SKB_FREED_MAGIC) {
			if (seam_slots[i].head != NULL ||
			    skb->head != NULL || skb->data != NULL)
				seam_counters.fence_bad++;
			continue;
		}
		if (skb->head == NULL || skb->data == NULL ||
		    skb->tail > skb->end)
			seam_counters.fence_bad++;
	}
}

/* ------------------------------------------------------------------ */
/* the seam as the driver substitute uses it                           */
/* ------------------------------------------------------------------ */

struct sk_buff *wlan_seam_skb_alloc(unsigned int size)
{
	return lkpi_dev_alloc_skb(size, 0);
}

void wlan_seam_skb_free(struct sk_buff *skb)
{
	lkpi_skb_free(skb);
}

void wlan_seam_stats(struct lkpi_skb_pool_stats *out)
{
	lkpi_skb_pool_stats(out);
}

void wlan_seam_reset_stats(void)
{
	uint32_t high_skb = seam_counters.skb_high;
	uint32_t high_data = seam_counters.data_high;

	memset(&seam_counters, 0, sizeof(seam_counters));
	seam_counters.skb_high = high_skb;
	seam_counters.data_high = high_data;
}

uint8_t *wlan_seam_data(struct sk_buff *skb)
{
	return (uint8_t *) skb->data;
}

unsigned int wlan_seam_capacity(void)
{
	return CONFIG_LKPI_SKB_DATA_BUF_SZ;
}

int wlan_seam_misuse_probe(void)
{
	struct sk_buff *skb;
	struct lkpi_skb_pool_stats stats;
	uint8_t stray_area[8];
	int ok = 1;

	/* a double free is named and skipped, the slot stays usable */
	skb = wlan_seam_skb_alloc(64);
	if (skb == NULL)
		return 0;
	wlan_seam_skb_free(skb);
	wlan_seam_skb_free(skb);
	wlan_seam_stats(&stats);
	ok = ok && stats.double_free == 1 && stats.n_free == 1;

	/* a write through a released slot is named, never propagated */
	lkpi_skb_uaf_hit(skb);
	wlan_seam_stats(&stats);
	ok = ok && stats.uaf_write == 1;

	/* a pointer that never came from the seam is named and skipped */
	lkpi_skb_free((struct sk_buff *) (void *) stray_area);
	wlan_seam_stats(&stats);
	ok = ok && stats.stray_free == 1;

	/* the free-slot scan finds nothing broken */
	lkpi_skb_fence_scan();
	wlan_seam_stats(&stats);
	ok = ok && stats.fence_bad == 0;

	/* and the slot is still usable afterwards */
	skb = wlan_seam_skb_alloc(64);
	ok = ok && skb != NULL;
	wlan_seam_skb_free(skb);

	return ok;
}
