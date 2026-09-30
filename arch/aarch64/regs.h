/*
 * @file   regs.h
 * @brief  MMIO register access helpers (32-bit).
 *
 * Every peripheral frame this image touches (DW-APB UART, GIC-600) is on a
 * 32-bit bus: accesses are 32-bit reads/writes at byte offsets. Volatile is
 * what keeps the compiler from merging or reordering register traffic;
 * -mstrict-align keeps every access aligned.
 */

#ifndef FREEWEBCAM_REGS_H
#define FREEWEBCAM_REGS_H

#include <stdint.h>

static inline uint32_t reg_rd32(uintptr_t addr)
{
	return *(volatile uint32_t *)addr;
}

static inline void reg_wr32(uintptr_t addr, uint32_t value)
{
	*(volatile uint32_t *)addr = value;
}

#endif /* FREEWEBCAM_REGS_H */
