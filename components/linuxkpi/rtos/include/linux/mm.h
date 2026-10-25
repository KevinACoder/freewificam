/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/mm.h
 * @brief   The page-record and page-allocation vocabulary a driver's receive and
 *          firmware-download paths name.
 *
 * @design declaration-only compatibility face. The interface's mm header owns the
 *         page record and the allocation/free entry points; this port's allocator
 *         is the heap, not a buddy allocator, so what a driver reaches here is the
 *         page record as an opaque handle plus the entry points the compiled set
 *         names. The bodies (linuxkpi_compat.c) are the heap under the interface's
 *         names: a "page" on this port is the buffer address itself (no compiled
 *         caller reads a field of struct page - callers only pass it to
 *         page_address() or back to the frees), so alloc_pages() hands out
 *         PAGE_SIZE << order bytes and page_address() returns that address. The
 *         signatures are stated as the interface states them (alloc_pages and
 *         alloc_page are the interface's macro over the noprof form) and a caller
 *         reads unchanged. The page-size constants (PAGE_SIZE, PAGE_SHIFT) and
 *         PAGE_ALIGN are part of the core contract. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_MM_H_
#define _LINUXKPI_LINUX_MM_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/mm.h includes <linux/slab.h>; the face keeps the interface's
 * own include, which is how the named-cache vocabulary reaches a unit that takes
 * this header. */
#include <linux/slab.h>

/* [LNX] include/linux/mm_types.h: the page record, carried as an opaque handle:
 * no compiled unit of this port reads a field of it, it is only stored and passed
 * back to the entry points below. */
struct page;

/* [LNX] include/linux/gfp.h:302-307 */
struct page *alloc_pages(gfp_t gfp_mask, unsigned int order);
#define	alloc_page(gfp_mask)	alloc_pages((gfp_mask), 0)

/* [LNX] include/linux/gfp.h:342-346 */
void __free_pages(struct page *page, unsigned int order);
void free_pages(unsigned long addr, unsigned int order);
#define	__free_page(page)	__free_pages((page), 0)

/* [LNX] include/linux/mm.h:3104: `void *page_address(const struct page *page)`.
 * The interface's body is a real function because its page record lives in a
 * mem_map array; this port's page handle IS the buffer's address (see the record's
 * note above), so the accessor is the cast and is carried inline. Inline rather
 * than out of line on purpose: a translation unit that reaches the page API only
 * through this accessor - the skbuff pool's fragment path does - must not have to
 * link the allocator bodies to get it. */
static __always_inline __unused void *
page_address(const struct page *page)
{
	return (void *)(uintptr_t)page;
}

/* [LNX] include/linux/mm.h: the reference the interface takes on a page it is
 * about to hand to another owner. This port's page record carries a reference
 * count (in the header the allocator carves in front of the page,
 * linuxkpi_compat.c), and the take is the increment; the release below returns
 * the block only when the last reference goes. Out of line because the count
 * lives in the allocator's private header rather than in this face. */
void	lkpi_page_get(struct page *page);

static __always_inline __unused void
get_page(struct page *page)
{
	lkpi_page_get(page);
}

/* [LNX] include/linux/mm.h: `put_page()` drops one reference and returns the
 * block on the last. The interface reads the order back from its page record;
 * this port's allocation header kept it, so the release needs no argument. */
void	put_page(struct page *page);

/* [LNX] include/asm-generic/getorder.h:29: the generic inline body, stated here
 * because the interface's own home for it is an asm-generic header this face does
 * not carry; the arithmetic is the interface's. */
static __always_inline __unused int
get_order(unsigned long size)
{
	if (__builtin_constant_p(size)) {
		if (!size)
			return BITS_PER_LONG - PAGE_SHIFT;
		if (size < (1UL << PAGE_SHIFT))
			return 0;
		return (int)(ilog2(size - 1) - PAGE_SHIFT + 1);
	}

	size--;
	size >>= PAGE_SHIFT;
	if (size == 0)
		return 0;
	return (int)(64 - __builtin_clzll((unsigned long long)size));
}

#endif /* _LINUXKPI_LINUX_MM_H_ */
