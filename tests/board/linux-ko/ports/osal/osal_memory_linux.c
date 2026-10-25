/*
 * @file
 * @brief The net80211 port's allocation seam on the Linux kernel heap.
 *
 * One seam behind every allocation name (malloc(9)/kmem(9) and the mbuf
 * clusters all arrive here through osal_cmsis2.c).  The target file
 * (osal_memory_target.c) takes blocks from the OS memory service; this backend
 * takes them from kmalloc/kzalloc and keeps the same small prefix, so free()
 * can account by pointer and the live-block/live-byte counters keep the same
 * meaning for the port's contracts.
 *
 * Context: the kernelmod line's net80211 world runs from the driver's worker and from
 * the bridge's own calls, never from an interrupt.  GFP_KERNEL is therefore
 * the right class; a caller that ever moves into atomic context must be
 * deferred first (the same rule the port states for its blocking services).
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <stddef.h>
#include <string.h>

#include "osal/wlan_osal.h"

struct block_hdr {
	size_t size;
	size_t pad;
};

static unsigned long live_blocks;
static unsigned long live_bytes;

void *wlan_osal_alloc(size_t size)
{
	struct block_hdr *h;

	h = kmalloc(sizeof(*h) + size, GFP_KERNEL);
	if (h == NULL) {
		return NULL;
	}
	h->size = size;
	h->pad = 0;
	live_blocks++;
	live_bytes += size;
	return (void *)(h + 1);
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
	h = (struct block_hdr *)p - 1;
	if (live_blocks > 0) {
		live_blocks--;
	}
	if (live_bytes >= h->size) {
		live_bytes -= h->size;
	} else {
		live_bytes = 0;
	}
	kfree(h);
}

unsigned long wlan_osal_live_blocks(void)
{
	return live_blocks;
}

unsigned long wlan_osal_live_bytes(void)
{
	return live_bytes;
}
