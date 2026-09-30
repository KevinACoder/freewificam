/*
 * @file   tick.h
 * @brief  Generic-timer tick in the CMSIS OS_Tick_* shape (os interface).
 *
 * The OS_Tick_* interface shape is the ThreadX-round seam (SPEC v3 §3.10):
 * a kernel port drives the tick through this surface and the board layer
 * carries no kernel dependency - swapping the kernel in must not require
 * editing this file.
 */

#ifndef FREEWEBCAM_TICK_H
#define FREEWEBCAM_TICK_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*os_tick_user_handler_t)(void);

enum os_tick_source {
	OS_TICK_SOURCE_VIRT = 0,	/* EL1 virtual timer CNTV, PPI 27 */
	OS_TICK_SOURCE_PHYS_NS = 1,	/* EL1 ns physical timer CNTP, PPI 30 */
};

struct os_tick_config {
	uint32_t freq_hz;	/* tick rate (default 100 = 10 ms period) */
	uint32_t intid;		/* PPI INTID delivering the tick */
	enum os_tick_source source;
};

/* The board's tick instance (board/rk3568-itx/param.c). */
extern const struct os_tick_config board_tick_config;

/* Program and start the tick. handler may be NULL (the internal ISR only
 * advances the tick count). Returns 0 on success. */
int32_t OS_Tick_Setup(uint32_t freq_hz, os_tick_user_handler_t handler);

void OS_Tick_Enable(void);
void OS_Tick_Disable(void);

/* Reload the period (the level line clears when TVAL is rewritten). Called
 * by the internal ISR; a kernel port's own ISR would call it too. */
void OS_Tick_AcknowledgeIRQ(void);

int32_t OS_Tick_GetIRQn(void);
uint32_t OS_Tick_GetClock(void);	/* generic counter frequency */
uint32_t OS_Tick_GetInterval(void);	/* reload value, counter units */
uint32_t OS_Tick_GetCount(void);	/* time left in the current period */
uint32_t OS_Tick_GetOverflow(void);	/* read-and-clear overflow count */

/* Elapsed ticks since OS_Tick_Setup (64-bit internal; 32-bit wrap view for
 * the CMSIS-semantics consumers of a later round). */
uint64_t os_tick_get_count64(void);
uint32_t os_tick_get_count32(void);

/* Configured tick rate (for stamp rendering). */
uint32_t os_tick_freq_hz(void);

#endif /* FREEWEBCAM_TICK_H */
