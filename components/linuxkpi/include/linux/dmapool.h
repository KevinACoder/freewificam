/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/dmapool.h
 * @brief   The coherent-DMA allocation pool a driver's bus-command tables are
 *          carved out of.
 *
 * @design declaration-only compatibility face. The interface declares a fixed-size
 *         coherent-DMA pool (include/linux/dmapool.h:26-46) with a device-managed
 *         creation form; the pool record is carried as an opaque handle because no
 *         compiled unit reads a field of it. What the compiled set names is the
 *         device-managed create, the allocate and the free; their bodies belong to
 *         the kernel's DMA layer and have no counterpart on this port yet, so the
 *         signatures are stated as the interface states them. Reference for the
 *         face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_DMAPOOL_H_
#define _LINUXKPI_LINUX_DMAPOOL_H_

#include "../../linuxkpi_core.h"

struct device;

/* [LNX] include/linux/dmapool.h:14: the pool record, carried opaque. */
struct dma_pool;

/* [LNX] include/linux/dmapool.h:42-43 */
struct dma_pool *dmam_pool_create(const char *name, struct device *dev,
				  size_t size, size_t align, size_t allocation);

/* [LNX] include/linux/dmapool.h:34-38 */
void *dma_pool_alloc(struct dma_pool *pool, gfp_t mem_flags,
		     dma_addr_t *handle);
void dma_pool_free(struct dma_pool *pool, void *vaddr, dma_addr_t addr);

#endif /* _LINUXKPI_LINUX_DMAPOOL_H_ */
