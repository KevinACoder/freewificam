/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/dmapool.h
 * @brief   The coherent-DMA allocation pool a driver's bus-command tables are
 *          carved out of.
 *
 * @design compatibility face over the engine's coherent allocator. The interface
 *         declares a fixed-size coherent-DMA pool (include/linux/dmapool.h:26-46)
 *         with a device-managed creation form; the pool record is carried as an
 *         opaque handle because no compiled unit reads a field of it. What the
 *         compiled set names is the device-managed create, the allocate and the
 *         free; the bodies belong to the kernel's DMA layer, so this port states
 *         them over its one coherent allocator (dma_alloc_coherent /
 *         dma_free_coherent in components/linuxkpi/pci/linuxkpi_pci.c), which is
 *         the mechanism the pool's blocks would be carved from. The interface's
 *         auto-release-on-detach half of dmam_pool_create has no counterpart here
 *         (this port has no devres). The first consumer of these three is the gen2 transmit byte-count table, which the
 *         AC 7260's family never selects (mac_cfg->gen2 is false for the 7000
 *         family), so the path is stated but not taken on this part.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
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
