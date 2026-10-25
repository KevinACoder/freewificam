/*
 * @file
 * @brief The port allocation seam, target backend.
 *
 * Every allocation in this port - malloc(9)/kmem(9) callers and the mbuf
 * clusters - lands here. On the target the port does not carry an allocator of
 * its own: it goes through the OS's own memory service (osMemoryAlloc/Free),
 * which on this board is the TLSF window the linker reserved (the board heap
 * the kernel and every component share). One seam, one heap - a second pool
 * behind the imported stack would fragment the image's memory on a fixed
 * budget and hide whose accounting is whose.
 *
 * The host build keeps its C-library-backed osal_memory.c; this file replaces
 * it in the target link (the file is a sibling so both can coexist in the
 * tree, and exactly one is on the board image's source list).
 *
 * @design ports & adapters, allocation seam: the same single-seam shape as the
 * host backend (osal_memory.c), with the C library's role taken by the OS
 * memory service. Precedent for a single-seam allocator under an imported
 * stack: the component's own port/osal/freertos/osal_freertos.c:72-135 (one
 * heap for kmalloc/kmem/sysmalloc).
 */

#include <stddef.h>
#include <string.h>

#include <cmsis_os2.h>

#include "../wlan_osal.h"

/* The size of every block is recorded in a small prefix so free() can account
 * without a size argument (the imported API frees by pointer only). The second
 * word keeps the payload 16-byte aligned - 16 is AArch64's largest natural
 * alignment, and the pool's data slots want 64-byte alignment of their own
 * (they carry it themselves). */
struct block_hdr {
	size_t size;
	size_t pad;
};

static unsigned long live_blocks;
static unsigned long live_bytes;

void *wlan_osal_alloc(size_t size)
{
	struct block_hdr *h;

	h = osMemoryAlloc(sizeof(*h) + size);
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
	osMemoryFree(h);
}

unsigned long wlan_osal_live_blocks(void)
{
	return live_blocks;
}

unsigned long wlan_osal_live_bytes(void)
{
	return live_bytes;
}
