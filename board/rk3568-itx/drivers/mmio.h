/*
 * @file   mmio.h
 * @brief  32-bit MMIO read/write, the board drivers' register plumbing.
 *
 * The whole image runs identity-mapped with the peripheral window in the
 * Device 1 GiB (arch/aarch64/mmu.c), so a plain volatile pointer is the
 * whole story. Strict ordering against nearby Normal-memory traffic comes
 * from the drivers' own dsb() barriers where the hardware needs one (the
 * Device attribute already orders Device-vs-Device).
 */

#ifndef FREEWEBCAM_MMIO_H
#define FREEWEBCAM_MMIO_H

#include <stdint.h>

static inline uint32_t mmio_rd32(uintptr_t addr)
{
	return *(volatile uint32_t *)addr;
}

static inline void mmio_wr32(uintptr_t addr, uint32_t value)
{
	*(volatile uint32_t *)addr = value;
}

#endif /* FREEWEBCAM_MMIO_H */
