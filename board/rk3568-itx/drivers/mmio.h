/*
 * @file   mmio.h
 * @brief  MMIO read/write, the board drivers' register plumbing.
 *
 * The whole image runs identity-mapped with the peripheral window in the
 * Device 1 GiB (arch/aarch64/mmu.c), so a plain volatile pointer is the
 * whole story. Strict ordering against nearby Normal-memory traffic comes
 * from the drivers' own dsb() barriers where the hardware needs one (the
 * Device attribute already orders Device-vs-Device).
 *
 * The 8- and 16-bit accessors exist for register files that mix widths in one
 * window: the SD host controller standard puts byte and halfword registers at
 * odd offsets, and a Device-memory access at an address that is not aligned to
 * its access size is a fault, not a truncation. A driver reads or writes such a
 * register at the width the register has, never wider.
 */

#ifndef FREEWIFICAM_MMIO_H
#define FREEWIFICAM_MMIO_H

#include <stdint.h>

static inline uint32_t mmio_rd32(uintptr_t addr)
{
	return *(volatile uint32_t *)addr;
}

static inline void mmio_wr32(uintptr_t addr, uint32_t value)
{
	*(volatile uint32_t *)addr = value;
}

static inline uint16_t mmio_rd16(uintptr_t addr)
{
	return *(volatile uint16_t *)addr;
}

static inline void mmio_wr16(uintptr_t addr, uint16_t value)
{
	*(volatile uint16_t *)addr = value;
}

static inline uint8_t mmio_rd8(uintptr_t addr)
{
	return *(volatile uint8_t *)addr;
}

static inline void mmio_wr8(uintptr_t addr, uint8_t value)
{
	*(volatile uint8_t *)addr = value;
}

#endif /* FREEWIFICAM_MMIO_H */
