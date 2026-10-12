/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/dma-mapping.h
 * @brief   The DMA mapping calls a PCI driver names: coherent allocation, a
 *          streaming map/unmap pair, and the mask a device declares.
 *
 * @design ports & adapters, the driver-facing side of the DMA face. Signatures
 *         and the direction enum are transcribed from the interface
 *         (include/linux/dma-mapping.h); the engine behind them
 *         (components/linuxkpi/pci/linuxkpi_pci.c) reaches the host through the
 *         seam's dma_alloc / dma_free / dma_map / dma_unmap operations, and the
 *         adapter below the seam owns the cache maintenance (the seam itself
 *         carries no cache vocabulary - see lkpi_pcie_backend.h).
 *
 *         Coherence model (a recorded decision): this port has one PCI host and
 *         one coherent pool, so the calls ignore the device argument and the
 *         coherent window is a single global non-cacheable region; a coherent
 *         buffer needs no explicit maintenance, and a streaming buffer is made
 *         visible by the map/unmap pair. The face therefore does not carry
 *         dma_sync_single_for_* yet: no compiled consumer names it, and inventing
 *         the operation without one would fix a seam shape the first consumer has
 *         not asked for.
 *
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_DMA_MAPPING_H_
#define _LINUXKPI_LINUX_DMA_MAPPING_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/dma-direction.h:6-11, values kept identical (a driver
 * compares a direction, and the adapter switches on it). */
enum dma_data_direction {
	DMA_BIDIRECTIONAL = 0,
	DMA_TO_DEVICE = 1,
	DMA_FROM_DEVICE = 2,
	DMA_NONE = 3,
};

/* [LNX] include/linux/dma-mapping.h:22: the all-ones handle a failed map returns. */
#define	DMA_MAPPING_ERROR	(~(dma_addr_t)0)

/* [LNX] include/linux/dma-mapping.h:30: the mask for n address bits. */
#define	DMA_BIT_MASK(n)	(((n) == 64) ? ~0ULL : ((1ULL << (n)) - 1))

void	*dma_alloc_coherent(struct device *dev, size_t size,
	    dma_addr_t *dma_handle, gfp_t flag);
void	dma_free_coherent(struct device *dev, size_t size, void *cpu_addr,
	    dma_addr_t dma_handle);

dma_addr_t	dma_map_single(struct device *dev, void *ptr, size_t size,
	    enum dma_data_direction dir);
void	dma_unmap_single(struct device *dev, dma_addr_t addr, size_t size,
	    enum dma_data_direction dir);

int	dma_set_mask_and_coherent(struct device *dev, u64 mask);

/* [LNX] include/linux/dma-mapping.h:75-78: a failed map is the all-ones handle. */
static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	(void)dev;
	return dma_addr == DMA_MAPPING_ERROR;
}

#endif /* _LINUXKPI_LINUX_DMA_MAPPING_H_ */
