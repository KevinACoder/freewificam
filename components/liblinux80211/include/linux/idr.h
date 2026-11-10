/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * include/linux/idr.h - the id-to-pointer service, used-surface face.
 *
 * The pinned interface builds this header on the radix tree and xarray layers.
 * This port carries neither implementation yet (the bodies are the S1c link
 * account), and the imported stack names only the seven functions below plus
 * struct idr itself.  Carrying the pinned header here would pull preempt.h,
 * xarray.h, local_lock.h and percpu.h into 35 translation units for no named
 * symbol; so this face states the used surface and nothing else.
 *
 * The struct is opaque to the stack: only idr_init() reads it in this round,
 * and the layout belongs to the implementation that lands with the first body.
 * The host bench keeps its explicit idr_rb skip until then - a silent empty
 * implementation would read as coverage it does not have.
 */
#ifndef __IDR_H__
#define __IDR_H__

#include <linux/gfp.h>
#include <linux/types.h>

struct idr {
	void		*idr_root;	/* implementation-owned */
	unsigned int	idr_base;
	unsigned int	idr_next;
};

/* [LNX] include/linux/idr.h:166-171: the interface's inline, over the record. */
static inline void idr_init(struct idr *idr)
{
	idr->idr_root = NULL;
	idr->idr_base = 0;
	idr->idr_next = 0;
}

/* [LNX] include/linux/idr.h:115-126: the declarations the compiled set names.
 * The bodies are the port's (S1c); a caller that reaches one before they land
 * gets an unresolved symbol rather than a stub. */
int idr_alloc(struct idr *idr, void *ptr, int start, int end, gfp_t gfp);
void *idr_remove(struct idr *idr, unsigned long id);
void *idr_find(const struct idr *idr, unsigned long id);
int idr_for_each(const struct idr *idr,
		 int (*fn)(int id, void *p, void *data), void *data);
void *idr_get_next(struct idr *idr, int *nextid);
void idr_destroy(struct idr *idr);

/* [LNX] include/linux/idr.h:204-205: the iteration macro is written over
 * idr_get_next(), exactly as the interface writes it. */
#define idr_for_each_entry(idr, entry, id)				\
	for ((id) = 0;							\
	     ((entry) = idr_get_next((idr), &(id))) != NULL;		\
	     (id) += 1U)

#endif /* __IDR_H__ */
