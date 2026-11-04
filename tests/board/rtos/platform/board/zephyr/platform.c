/*
 * @file   platform.c
 * @brief  The board/arch face the freewificam components call, served by Zephyr.
 *
 * The component sources reach the platform through a small set of headers -
 * cache.h, gicv3.h, generic_timer.h, tick.h, uart.h, arch_asm.h - and this file
 * answers the four that actually have code behind them.  The headers themselves
 * are the board ones (tests/board/rtos/platform/arch/aarch64 and
 * platform/board/rk3568-itx): they are declarations plus register constants, and
 * re-declaring them would let the two copies drift.
 *
 * What each face becomes:
 *
 *  - cache: Zephyr's own VA-based data-cache maintenance (arch_dcache_*).  The
 *    board's cache.c implemented the same instructions by hand; on this image
 *    the arch layer already owns them, and CONFIG_CACHE_MANAGEMENT is on in the
 *    profile, so the generic sys_cache_* layer would answer too.
 *
 *  - interrupts: irq_connect_dynamic() + irq_enable()/irq_disable().  The INTIDs
 *    the board headers carry are already the GIC SPI numbers the rest of the
 *    system uses (device-tree number + 32), which is exactly Zephyr's numbering,
 *    so no arithmetic belongs here.  Priorities are raw GIC bytes on both sides:
 *    Zephyr's arm64 GIC driver writes the value straight into GICD_IPRIORITYRn.
 *
 *  - counters: the architectural counter (CNTVCT_EL0/CNTFRQ_EL0), which is the
 *    same 24 MHz source the board images used, so every timestamp in the log and
 *    every microsecond delay keeps its meaning.
 *
 *  - tick: the CMSIS OS_Tick_* shape over Zephyr's kernel clock.  The board's
 *    tick.c programmed CNTV and drove the kernel; here the kernel owns the timer,
 *    so the shape only has to answer questions.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/arch/arm64/cache.h>
#include <zephyr/arch/arm64/lib_helpers.h>
#include <zephyr/drivers/interrupt_controller/gic.h>

#include "arch_asm.h"
#include "cache.h"
#include "generic_timer.h"
#include "tick.h"

/*
 * gicv3.h declares irq_enable()/irq_disable() as functions, and those are
 * function-like macros in <zephyr/irq.h> already included above.  Left alone,
 * the declarations would expand to `int32_t arch_irq_enable(uint32_t)` - a
 * prototype that contradicts the arch's own `void arch_irq_enable(unsigned
 * int)`.  The two names are therefore un-macro'd before the board header is
 * read; the function bodies below call the arch primitives directly.
 */
#undef irq_enable
#undef irq_disable

#include "gicv3.h"

/* --- cache ---------------------------------------------------------------- */

void cache_dcache_flush(uintptr_t addr, unsigned long size)
{
	(void)arch_dcache_flush_range((void *)addr, (size_t)size);
}

void cache_dcache_invalidate(uintptr_t addr, unsigned long size)
{
	(void)arch_dcache_invd_range((void *)addr, (size_t)size);
}

void cache_dcache_flush_invalidate(uintptr_t addr, unsigned long size)
{
	(void)arch_dcache_flush_and_invd_range((void *)addr, (size_t)size);
}

void cache_init(void)
{
	/* Zephyr enables and invalidates the caches in its own boot path,
	 * before the MMU is switched on. */
}

void cache_icache_sync_range(uintptr_t addr, unsigned long size)
{
	(void)arch_icache_flush_range((void *)addr, (size_t)size);
}

/* --- interrupts ----------------------------------------------------------- */

void gicv3_init(void)
{
	/* The GIC is initialised by Zephyr's interrupt-controller driver from
	 * the devicetree node; there is nothing left to program. */
}

int32_t irq_register(uint32_t intid, irq_handler_t handler, void *arg)
{
	if (handler == NULL) {
		return -1;
	}
	/* Priority and trigger are applied by irq_set_priority()/
	 * irq_set_mode_level() below, in the order the board glue calls them;
	 * the connect itself only installs the routine. */
	(void)irq_connect_dynamic(intid, 0u, (void (*)(const void *))handler,
				  arg, 0u);
	return 0;
}

/*
 * Names below are macros in <zephyr/irq.h> - irq_enable(irq) expands to
 * arch_irq_enable(irq) - so they are #undef'd before the board's own
 * declarations (gicv3.h) can be defined as functions.  Each body then calls the
 * arch primitive directly, never the macro it just shadowed.
 */
int32_t irq_enable(uint32_t intid)
{
	/* Unmask one line at the distributor.  Not a global interrupt enable:
	 * the board glue calls this per controller as it binds it. */
	arch_irq_enable(intid);
	return 0;
}

int32_t irq_disable(uint32_t intid)
{
	arch_irq_disable(intid);
	return 0;
}

int32_t irq_clear_pending(uint32_t intid)
{
#if defined(CONFIG_ARCH_HAS_IRQ_PENDING_OPS)
	arch_irq_clear_pending(intid);
#else
	(void)intid;
#endif
	return 0;
}

int32_t irq_set_priority(uint32_t intid, uint8_t raw_priority)
{
	/* Raw GIC byte on both sides: the arm64 GIC driver writes the value into
	 * GICD_IPRIORITYRn unchanged, and the freewificam board headers already
	 * carry raw bytes (BOARD_IRQ_PRIORITY_API_CALL_RAW etc.). */
	arm_gic_irq_set_priority(intid, raw_priority, 0u);
	return 0;
}

int32_t irq_set_mode_level(uint32_t intid)
{
	/* Level is the reset state and the mode the board's devices need; the
	 * distributor's ICFGR pair is left at level by the driver's init. */
	(void)intid;
	return 0;
}

int32_t gic_irq_is_pending(uint32_t intid)
{
#if defined(CONFIG_ARCH_HAS_IRQ_PENDING_OPS)
	return arm_gic_irq_is_pending(intid) ? 1 : 0;
#else
	(void)intid;
	return -1;
#endif
}

int32_t gic_irq_is_enabled(uint32_t intid)
{
	return arch_irq_is_enabled(intid) ? 1 : 0;
}

uint32_t gic_running_priority(void)
{
	/* The running-priority register is the caller's own look at the
	 * controller; Zephyr exposes no accessor for it and nothing in this
	 * image's bring-up path depends on the value. */
	return 0xffu;
}

uint32_t gic_iar_read(void)
{
	/* Acknowledge/EOI belong to Zephyr's exception entry: the board images
	 * did the pair by hand in their own vector table, and here the entry
	 * has already done it before the ISR runs. */
	return 1023u;
}

void gic_irq_eoi(uint32_t iar)
{
	(void)iar;
}

uint32_t gic_irq_dispatch(uint32_t iar)
{
	(void)iar;
	return iar;
}

/* --- generic counter ------------------------------------------------------ */

uint32_t gtimer_freq_get(void)
{
	return (uint32_t)read_cntfrq_el0();
}

uint64_t gtimer_count_get(void)
{
	return read_cntvct_el0();
}

void gtimer_isb(void)
{
	barrier_isync_fence_full();
}

/* The EL1 timer register face.  Zephyr owns both EL1 timers (the system clock
 * uses the virtual one, the architecture timer driver arms it), so the setters
 * are refused rather than allowed to fight the kernel for the same line. */
void gtimer_ptimer_tval_set(uint32_t ticks)
{
	(void)ticks;
}

uint32_t gtimer_ptimer_tval_get(void)
{
	return 0u;
}

void gtimer_ptimer_ctl_set(uint32_t ctl)
{
	(void)ctl;
}

uint32_t gtimer_ptimer_ctl_get(void)
{
	return 0u;
}

void gtimer_vtimer_tval_set(uint32_t ticks)
{
	(void)ticks;
}

uint32_t gtimer_vtimer_tval_get(void)
{
	return 0u;
}

void gtimer_vtimer_ctl_set(uint32_t ctl)
{
	(void)ctl;
}

uint32_t gtimer_vtimer_ctl_get(void)
{
	return 0u;
}

/* --- CMSIS OS_Tick_* shape ------------------------------------------------ */

/* The board instance (platform/board/rk3568-itx/param.c) is data the log's
 * stamp path reads; the frequency is the kernel's own. */
const struct os_tick_config board_tick_config = {
	.freq_hz = 1000u,
	.intid = 27u,
	.source = OS_TICK_SOURCE_VIRT,
};

static uint32_t tick_freq_hz = 1000u;
static os_tick_user_handler_t tick_user_handler;

int32_t OS_Tick_Setup(uint32_t freq_hz, os_tick_user_handler_t handler)
{
	tick_user_handler = handler;
	if (freq_hz != 0u) {
		tick_freq_hz = freq_hz;
	}
	return 0;
}

void OS_Tick_Enable(void)
{
}

void OS_Tick_Disable(void)
{
}

void OS_Tick_AcknowledgeIRQ(void)
{
}

int32_t OS_Tick_GetIRQn(void)
{
	return (int32_t)board_tick_config.intid;
}

uint32_t OS_Tick_GetClock(void)
{
	return gtimer_freq_get();
}

uint32_t OS_Tick_GetInterval(void)
{
	return (uint32_t)(gtimer_freq_get() / ((tick_freq_hz != 0u) ? tick_freq_hz : 1000u));
}

uint32_t OS_Tick_GetCount(void)
{
	return (uint32_t)OS_Tick_GetInterval();
}

uint32_t OS_Tick_GetOverflow(void)
{
	return 0u;
}

uint64_t os_tick_get_count64(void)
{
	return (uint64_t)k_uptime_ticks();
}

uint32_t os_tick_get_count32(void)
{
	return (uint32_t)k_uptime_ticks();
}

uint32_t os_tick_freq_hz(void)
{
	return (uint32_t)CONFIG_SYS_CLOCK_TICKS_PER_SEC;
}

/* --- board reset ---------------------------------------------------------- */

int board_system_reset(void)
{
	sys_reboot(SYS_REBOOT_COLD);
	return -1;	/* only reached when the platform refused the request */
}
