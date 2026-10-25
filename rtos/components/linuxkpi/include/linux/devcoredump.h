/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/devcoredump.h
 * @brief   The device-core-dump entry points under the names a driver includes them.
 *
 * @design declaration-only compatibility face, and a pure forward for the
 *         value-form entry point: that one is already stated in the contract header
 *         (components/linuxkpi/linuxkpi_core.h, whose device-coredump section is the
 *         single definition - a statement-form macro that names its arguments, which
 *         is what a caller that transfers the buffer's ownership needs to see). The
 *         interface keeps this header for the same names; this face keeps one
 *         mechanism, so the interface's name reaches the contract header and the
 *         value-form entry point is not restated here.
 *
 *         The scatter/gather form has no counterpart in the contract header, so it
 *         is carried here as the interface's own !IS_ENABLED(CONFIG_DEV_COREDUMP)
 *         arm: this port has no coredump core and no userspace to read a dump, so
 *         dev_coredumpsg() frees the table the driver built (the interface's
 *         no-core behaviour) and _devcd_free_sgtable() is the interface's own
 *         free walk. The one driver call site is the firmware-error dump path
 *         (fw/dbg.c), which this line does not run (the AC 7260's dump path needs a
 *         coredump consumer); the free is carried whole so the driver's error path
 *         is still exact.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_DEVCOREDUMP_H_
#define _LINUXKPI_LINUX_DEVCOREDUMP_H_

#include "../../linuxkpi_core.h"
/* [LNX] include/linux/devcoredump.h:12 includes <linux/scatterlist.h>; the face
 * keeps the interface's own include, which is how the scatter/gather record a
 * firmware-dump caller hands to the entry point reaches a unit through it. */
#include <linux/scatterlist.h>
/* [LNX] include/linux/devcoredump.h:13 includes <linux/slab.h> and reaches
 * __free_page()/free() through the allocator headers; this face's page-release
 * entry point is <linux/mm.h>'s __free_page(), which the free walk below needs. */
#include <linux/mm.h>

/*
 * [LNX] include/linux/devcoredump.h:25-57: `_devcd_free_sgtable(struct scatterlist
 * *table)` frees the pages and the scatterlist instances of a firmware-dump table
 * (the caller built both for a coredump). Transcribed from the interface: the pages
 * are released first, then the chained tables. The interface's page walk is
 * `for_each_sg(table, iter, sg_nents(table), i)`; sg_nents() is not part of this
 * face's scatterlist vocabulary, so the equivalent walk is stated over the port's
 * sg_next(), which visits the same set of entries (every non-chain entry, across
 * the chained tables).
 */
static __always_inline __unused void
_devcd_free_sgtable(struct scatterlist *table)
{
	struct page *page;
	struct scatterlist *iter;
	struct scatterlist *delete_iter;

	/* free pages */
	for (iter = table; iter != NULL; iter = sg_next(iter)) {
		page = sg_page(iter);
		if (page)
			__free_page(page);
	}

	/* then free all chained tables */
	iter = table;
	delete_iter = table;	/* always points on a head of a table */
	while (!sg_is_last(iter)) {
		iter++;
		if (sg_is_chain(iter)) {
			iter = sg_chain_ptr(iter);
			kfree(delete_iter);
			delete_iter = iter;
		}
	}

	/* free the last table */
	kfree(delete_iter);
}

/*
 * [LNX] include/linux/devcoredump.h:68,91-94: the scatter/gather entry point. The
 * interface's !IS_ENABLED(CONFIG_DEV_COREDUMP) arm releases the table; this port
 * has no coredump core, so that arm is carried. Unreachable on this line: the one
 * caller is the firmware-error dump (fw/dbg.c:1907), which needs a coredump
 * consumer this port does not have.
 */
static __always_inline __unused void
dev_coredumpsg(struct device *dev, struct scatterlist *table, size_t datalen,
	       gfp_t gfp)
{
	(void)dev;
	(void)datalen;
	(void)gfp;
	_devcd_free_sgtable(table);
}

#endif /* _LINUXKPI_LINUX_DEVCOREDUMP_H_ */
