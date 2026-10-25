/*
 * @file   dwc_mshc_dma.h
 * @brief  The SDMA boundary arithmetic, kept apart from the driver so it can be
 *         exercised without a controller.
 *
 * The controller's SDMA engine does not run a transfer across a boundary-sized
 * address: it stops when the next byte would cross one, raises the DMA
 * interrupt, and waits to be told where to continue. How far it got is
 * therefore a fact about the STARTING address, not a multiple of the boundary:
 * an engine started at 0x0001_0000 transfers up to 0x0008_0000 and stops there.
 * The continuation address is the first boundary strictly after the address the
 * engine was last given - which for an already boundary-aligned address is one
 * whole boundary further on, because the engine runs from that address to the
 * next boundary.
 *
 * This is the one piece of the data path where a plausible-looking rewrite
 * ("continue at start + n * boundary") is wrong in a way that only shows up on
 * transfers whose buffer does not happen to be boundary-aligned: the arithmetic
 * is stated once, here, and asserted in the host suite.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 freewificam contributors
 */

#ifndef FREEWIFICAM_DWC_MSHC_DMA_H
#define FREEWIFICAM_DWC_MSHC_DMA_H

#include <stdint.h>

/* The boundary the driver programs into BLOCK_SIZE[14:12] (7 = 512 Kbytes). */
#define DWC_MSHC_SDMA_BOUNDARY		(512U * 1024U)

/* The address the engine continues at after it stopped at a boundary: the first
 * boundary strictly after `prev`, the address it was last given. */
static inline uintptr_t dwc_mshc_sdma_continue(uintptr_t prev)
{
	return ((prev & ~(uintptr_t)(DWC_MSHC_SDMA_BOUNDARY - 1U)) +
		(uintptr_t)DWC_MSHC_SDMA_BOUNDARY);
}

#endif /* FREEWIFICAM_DWC_MSHC_DMA_H */
