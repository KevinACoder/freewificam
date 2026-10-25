/*
 * @file   regs.h
 * @brief  MMIO register access helpers (8/32/64-bit).
 *
 * Every peripheral frame this image touches (DW-APB UART, GIC-600) is on a
 * 32-bit bus: accesses are 32-bit reads/writes at byte offsets. The GIC's
 * per-interrupt priority registers are BYTE-granular and the SGI routing
 * register is 64-bit, so the smaller/larger widths exist too. Volatile is
 * what keeps the compiler from merging or reordering register traffic;
 * -mstrict-align keeps every access naturally aligned.
 */

#ifndef FREEWIFICAM_REGS_H
#define FREEWIFICAM_REGS_H

#include <stdint.h>

static inline uint8_t reg_rd8(uintptr_t addr)
{
	return *(volatile uint8_t *)addr;
}

static inline void reg_wr8(uintptr_t addr, uint8_t value)
{
	*(volatile uint8_t *)addr = value;
}

static inline uint32_t reg_rd32(uintptr_t addr)
{
	return *(volatile uint32_t *)addr;
}

static inline void reg_wr32(uintptr_t addr, uint32_t value)
{
	*(volatile uint32_t *)addr = value;
}

static inline uint64_t reg_rd64(uintptr_t addr)
{
	return *(volatile uint64_t *)addr;
}

static inline void reg_wr64(uintptr_t addr, uint64_t value)
{
	*(volatile uint64_t *)addr = value;
}

#endif /* FREEWIFICAM_REGS_H */
