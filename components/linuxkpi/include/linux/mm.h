/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/mm.h
 * @brief   The page-record and page-allocation vocabulary a driver's receive and
 *          firmware-download paths name.
 *
 * @design declaration-only compatibility face. The interface's mm header owns the
 *         page record and the allocation/free entry points; this port's allocator
 *         is the sk_buff/heap seam rather than a buddy allocator, so what a driver
 *         reaches here is the page record as an opaque handle plus the entry
 *         points the compiled set names. The bodies belong to the kernel's memory
 *         management and have no counterpart on this port yet, so the signatures
 *         are stated as the interface states them (alloc_pages and alloc_page are
 *         the interface's macro over the noprof form) and a caller reads
 *         unchanged. The page-size constants (PAGE_SIZE, PAGE_SHIFT) and PAGE_ALIGN
 *         are part of the core contract. Reference for the face shape:
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

/* [LNX] include/linux/mm.h:3104 */
void *page_address(const struct page *page);

/* [LNX] include/linux/mm.h: the reference the interface takes on a page it is
 * about to hand to another owner. This port's page records are opaque handles
 * with no reference count yet, so the take has nothing to increment; it is stated
 * as the interface's void shape so a caller reads unchanged. */
static __always_inline __unused void
get_page(struct page *page)
{
	(void)page;
}

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
