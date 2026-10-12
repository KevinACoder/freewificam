/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/scatterlist.h
 * @brief   The scatter/gather list a driver's transmit and firmware-dump paths
 *          build and walk.
 *
 * @design compatibility face, grown per consumer. The interface
 *         (include/linux/scatterlist.h) owns the list record, the chain and
 *         page-attachment helpers, the DMA views of an entry and the iteration
 *         macros. The record is transcribed field for field in the interface's
 *         CONFIG_NEED_SG_DMA_LENGTH-off shape (this port's entries carry one
 *         address, not a separate mapped length), the small accessors are the
 *         interface's inline shapes, and the two entry points whose bodies belong
 *         to the kernel's library (sg_init_table, sg_pcopy_from_buffer) are
 *         declared with the interface's signatures. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_SCATTERLIST_H_
#define _LINUXKPI_LINUX_SCATTERLIST_H_

#include "../../linuxkpi_core.h"

struct page;

/* [LNX] include/linux/scatterlist.h:67-68,75 */
#define	SG_CHAIN		0x01UL
#define	SG_END			0x02UL
#define	SG_PAGE_LINK_MASK	(SG_CHAIN | SG_END)

/* [LNX] include/linux/scatterlist.h:11-22, in the CONFIG_NEED_SG_DMA_LENGTH-off
 * shape. */
struct scatterlist {
	unsigned long	page_link;
	unsigned int	offset;
	unsigned int	length;
	dma_addr_t	dma_address;
};

/* [LNX] include/linux/scatterlist.h:41-45 */
struct sg_table {
	struct scatterlist *sgl;	/* the list */
	unsigned int	nents;		/* number of mapped entries */
	unsigned int	orig_nents;	/* original size of list */
};

/* [LNX] include/linux/scatterlist.h:31,36 */
#define	sg_dma_address(sg)	((sg)->dma_address)
#define	sg_dma_len(sg)		((sg)->length)

/* [LNX] include/linux/scatterlist.h:87,92 */
static __always_inline __unused bool
sg_is_chain(struct scatterlist *sg)
{
	return sg->page_link & SG_CHAIN;
}

static __always_inline __unused bool
sg_is_last(struct scatterlist *sg)
{
	return sg->page_link & SG_END;
}

/* [LNX] include/linux/scatterlist.h:82-85 */
static __always_inline __unused struct scatterlist *
sg_chain_ptr(struct scatterlist *sg)
{
	return (struct scatterlist *)(sg->page_link & ~SG_PAGE_LINK_MASK);
}

/* [LNX] include/linux/scatterlist.h:276-281 */
static __always_inline __unused void
sg_mark_end(struct scatterlist *sg)
{
	sg->page_link |= SG_END;
	sg->page_link &= ~SG_CHAIN;
}

/* [LNX] include/linux/scatterlist.h:194-197 */
static __always_inline __unused struct page *
sg_page(struct scatterlist *sg)
{
	return (struct page *)(sg->page_link & ~SG_PAGE_LINK_MASK);
}

/* [LNX] include/linux/scatterlist.h:134-143 */
static __always_inline __unused void
sg_assign_page(struct scatterlist *sg, struct page *page)
{
	unsigned long page_link = sg->page_link & (SG_CHAIN | SG_END);

	sg->page_link = page_link | (unsigned long)page;
}

/* [LNX] include/linux/scatterlist.h: sg_set_page() attaches a page with a length
 * and an offset. The interface's range assertion is a debug check; the port
 * states the assignment. */
static __always_inline __unused void
sg_set_page(struct scatterlist *sg, struct page *page, unsigned int len,
	    unsigned int offset)
{
	sg_assign_page(sg, page);
	sg->offset = offset;
	sg->length = len;
}

/* [LNX] include/linux/scatterlist.h:176-189 */
static __always_inline __unused void
__sg_chain(struct scatterlist *chain_sg, struct scatterlist *sgl)
{
	chain_sg->offset = 0;
	chain_sg->length = 0;
	chain_sg->page_link = ((unsigned long)sgl | SG_CHAIN) & ~SG_END;
}

/* [LNX] include/linux/scatterlist.h:192-196 */
static __always_inline __unused void
sg_chain(struct scatterlist *prv, unsigned int prv_nents, struct scatterlist *sgl)
{
	__sg_chain(&prv[prv_nents - 1], sgl);
}

/* [LNX] include/linux/scatterlist.h:107-117 */
static __always_inline __unused struct scatterlist *
sg_next(struct scatterlist *sg)
{
	if (sg_is_last(sg))
		return NULL;

	sg++;
	if (sg_is_chain(sg))
		sg = sg_chain_ptr(sg);

	return sg;
}

/* [LNX] include/linux/scatterlist.h:218-220 */
#define	for_each_sg(sglist, sg, nr, __i)	\
	for ((__i) = 0, (sg) = (sglist); (__i) < (nr); (__i)++, (sg) = sg_next(sg))

/* [LNX] include/linux/scatterlist.h:224,232 */
#define	for_each_sgtable_sg(sgt, sg, i)		\
	for_each_sg((sgt)->sgl, sg, (sgt)->orig_nents, i)
#define	for_each_sgtable_dma_sg(sgt, sg, i)	\
	for_each_sg((sgt)->sgl, sg, (sgt)->nents, i)

/* [LNX] include/linux/scatterlist.h:447,524 */
void sg_init_table(struct scatterlist *, unsigned int);
size_t sg_pcopy_from_buffer(struct scatterlist *sgl, unsigned int nents,
			    const void *buf, size_t buflen, loff_t skip);

#endif /* _LINUXKPI_LINUX_SCATTERLIST_H_ */
