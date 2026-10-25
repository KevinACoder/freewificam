/*
 * @file
 * @brief The port allocation seam, host backend.
 *
 * Every allocation in this port - malloc(9)/kmem(9) callers and the mbuf
 * clusters - lands here. The host build backs the seam with the C library; a
 * target build replaces this one file (the memory subsystem owns the pool, see
 * the port DIVERGENCES ledger). Accounting is kept because the host contracts
 * assert that a completed round trip returns the counters to zero.
 *
 * @design ports & adapters, allocation seam: one allocation path for the whole
 * port. Precedent for a single-seam allocator under an imported stack: the
 * component's own port/osal/freertos/osal_freertos.c:72-135 (one heap for
 * kmalloc/kmem/sysmalloc), re-derived here without the RTOS-heap dependency.
 */

#include <stdlib.h>
#include <string.h>

#include "../wlan_osal.h"

static unsigned long live_blocks;
static unsigned long live_bytes;

/* The size of every block is recorded in a small prefix so free() can account
 * without a size argument (the imported API frees by pointer only). The second
 * word keeps the payload aligned. */
struct block_hdr {
	size_t size;
	size_t pad;
};

void *wlan_osal_alloc(size_t size)
{
	struct block_hdr *h;

	h = malloc(sizeof(*h) + size);
	if (h == NULL) {
		return NULL;
	}
	h->size = size;
	h->pad = 0;
	live_blocks++;
	live_bytes += size;
	return (void *) (h + 1);
}

void *wlan_osal_zalloc(size_t size)
{
	void *p = wlan_osal_alloc(size);

	if (p != NULL) {
		memset(p, 0, size);
	}
	return p;
}

void wlan_osal_free(void *p)
{
	struct block_hdr *h;

	if (p == NULL) {
		return;
	}
	h = (struct block_hdr *) p - 1;
	if (live_blocks > 0) {
		live_blocks--;
	}
	if (live_bytes >= h->size) {
		live_bytes -= h->size;
	} else {
		live_bytes = 0;
	}
	free(h);
}

unsigned long wlan_osal_live_blocks(void)
{
	return live_blocks;
}

unsigned long wlan_osal_live_bytes(void)
{
	return live_bytes;
}
