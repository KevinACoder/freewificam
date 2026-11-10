/*
 * @file   tick.c
 * @brief  Tick from an EL1 ARM generic timer, CMSIS OS_Tick_* shape.
 *
 * Which timer, and why (board facts, established by experiment on this
 * board):
 *
 *  - DEFAULT: the EL1 *virtual* timer (CNTV, INTID 27). Measured on this
 *    board: writing CNTV_TVAL raises GICR_ISPENDR0 bit 27 and INTID 27
 *    arrives.
 *
 *  - ALTERNATIVE (TICK_SOURCE_PHYS_NS): the EL1 *non-secure physical*
 *    timer (CNTP, INTID 30). PPI 29 is a different line entirely - the
 *    SECURE physical timer (CNTPS) that OP-TEE holds as Group 0. EL1 access
 *    to the non-secure physical timer is legal: start.S's EL2 descent sets
 *    CNTHCTL_EL2.EL1PCTEN|EL1PCEN before any EL1 code runs.
 *
 * Either way: down-counter TVAL, control ENABLE bit 0 / IMASK bit 1, and a
 * LEVEL interrupt cleared by rewriting TVAL. Same programming model, only
 * the register bank differs. Register access goes through generic_timer.h -
 * the raw cntv/cntp pokes live in the arch module alone.
 */

#include <stdbool.h>
#include <stdint.h>

#include "board_conf.h"
#include "generic_timer.h"
#include "gicv3.h"
#include "tick.h"

static uint32_t tick_freq_hz;
static uint32_t tick_load;
static uint32_t tick_irq;
static volatile uint32_t tick_overflow_count;
static volatile uint64_t tick_count;
static volatile bool tick_enabled;
static os_tick_user_handler_t tick_user_handler;

#ifdef TICK_SOURCE_PHYS_NS

static inline void tick_timer_write_tval(uint64_t value)
{
	gtimer_ptimer_tval_set((uint32_t)value);
}

static inline uint32_t tick_timer_read_tval(void)
{
	return gtimer_ptimer_tval_get();
}

static inline void tick_timer_write_ctl(uint64_t value)
{
	gtimer_ptimer_ctl_set((uint32_t)value);
}

#else

static inline void tick_timer_write_tval(uint64_t value)
{
	gtimer_vtimer_tval_set((uint32_t)value);
}

static inline uint32_t tick_timer_read_tval(void)
{
	return gtimer_vtimer_tval_get();
}

static inline void tick_timer_write_ctl(uint64_t value)
{
	gtimer_vtimer_ctl_set((uint32_t)value);
}

#endif

/* The internal ISR: count, optional user work, then reload the period.
 * The rearm deasserts the level line and must happen before the exception
 * entry's EOI - the anti-tick-storm order. Runs at the tick's GIC priority;
 * no nesting (IRQs stay masked inside the exception). */
static void tick_isr(void *arg)
{
	(void)arg;

	tick_count++;
	if (tick_user_handler != 0) {
		tick_user_handler();
	}
	OS_Tick_AcknowledgeIRQ();
}

int32_t OS_Tick_Setup(uint32_t freq_hz, os_tick_user_handler_t handler)
{
	if (freq_hz == 0u) {
		return -1;
	}

	tick_freq_hz = freq_hz;
	tick_load = gtimer_freq_get() / freq_hz;
	if (tick_load == 0u) {
		return -1;
	}
	tick_irq = board_tick_config.intid;
	tick_user_handler = handler;

	gicv3_init();

	/* Stop the timer while it is reprogrammed: ctl=0 both stops the
	 * counter and masks the interrupt - the only safe state to program
	 * from. */
	tick_timer_write_ctl(0);
	tick_timer_write_tval((uint64_t)tick_load);

	(void)irq_disable(tick_irq);
	(void)irq_clear_pending(tick_irq);
	(void)irq_register(tick_irq, tick_isr, 0);
	(void)irq_set_mode_level(tick_irq);
	/* Raw hardware value, the board's lowest usable level. */
	(void)irq_set_priority(tick_irq, (uint8_t)BOARD_TICK_PRIORITY_RAW);
	(void)irq_enable(tick_irq);

	/* Enable with the interrupt unmasked (bit 0 set, bit 1 clear). */
	tick_timer_write_ctl(0x1);
	tick_enabled = true;
	tick_count = 0;
	tick_overflow_count = 0u;

	return 0;
}

void OS_Tick_Enable(void)
{
	if (!tick_enabled) {
		return;
	}
	tick_timer_write_tval((uint64_t)tick_load);
	tick_timer_write_ctl(0x1);
}

void OS_Tick_Disable(void)
{
	tick_enabled = false;
	tick_timer_write_ctl(0x0);
}

void OS_Tick_AcknowledgeIRQ(void)
{
	/* The interrupt is level-triggered and clears when the timer is
	 * rearmed. The EOI is the exception entry's business (tx_vectors.S). */
	tick_timer_write_tval((uint64_t)tick_load);
}

int32_t OS_Tick_GetIRQn(void)
{
	return (int32_t)tick_irq;
}

uint32_t OS_Tick_GetClock(void)
{
	return gtimer_freq_get();
}

uint32_t OS_Tick_GetInterval(void)
{
	return tick_load;
}

uint32_t OS_Tick_GetCount(void)
{
	/* Time left in the current period (count-down). */
	return tick_timer_read_tval();
}

uint32_t OS_Tick_GetOverflow(void)
{
	uint32_t count = tick_overflow_count;

	tick_overflow_count = 0u;
	return count;
}

uint64_t os_tick_get_count64(void)
{
	return tick_count;
}

uint32_t os_tick_get_count32(void)
{
	return (uint32_t)tick_count;
}

uint32_t os_tick_freq_hz(void)
{
	return tick_freq_hz;
}
