/*
 * @file    tx_glue.c
 * @brief   The ONE place the ThreadX kernel meets this board's base layer.
 * @author  zhugengyu
 * @date    07.10.2026
 * @design  adapter - two existing APIs meet here and neither is modified:
 *          the cortex_a55 port's supply face (what the port deliberately
 *          does NOT do: vector table / VBAR, timer programming, IRQ
 *          dispatch, EOI - all outside the port files) and the board
 *          layer's seams (OS_Tick_* / irq_register / board_log).
 *
 * The kernel side calls two symbols owned here: tx_application_define()
 * (vector-table installation, tick arming, anchors) and tx_fault_park()
 * (fatal landing from tx_vectors.S). Everything the kernel needs from the
 * machine arrives through the base layer's own seams - the OS_Tick_*
 * surface (os/baremetal/tick.h), the GIC handler table (irq_register),
 * board_log - so swapping the kernel again touches only os/threadx*.
 *
 * arch/board/os-baremetal files carry no tx_* include: the tx headers stop
 * at this directory.
 *
 * SPSel model: from tx_kernel_enter_spsel0 (tx_vectors.S) on, everything
 * runs at EL1 with SPSel=0. tx_application_define runs on the boot stack
 * with interrupts masked, still inside _tx_initialize_kernel_enter.
 *
 * EOI OWNERSHIP: the ThreadX entry (tx_vectors.S) reads ICC_IAR1_EL1,
 * dispatches and writes ICC_EOIR1_EL1 itself - the port does not, and
 * neither does gic_irq_dispatch. Nothing here may end an interrupt.
 */

#include <stdint.h>

#include "tx_api.h"
#include "tx_thread.h"

#include "arch_asm.h"
#include "board_conf.h"
#include "log.h"
#include "tick.h"

/* The port's tick dispatch entry (port-owned, not in the common headers). */
extern void _tx_timer_interrupt(void);

/* The application's object-creation hook: the only window ThreadX allows
 * for pre-scheduler object creation is tx_application_define itself, so the
 * image's threads are created here. The smoke supplies it; a CMSIS-RTOS2
 * board image supplies it from the adapter's deferred-creation replay. */
extern void tx_board_app_define(void);

/* The port version this image was built against (tx_port.h's _tx_version_id
 * carries the same string; the pin is locked so the literal is stable).
 * Anchor line evidence. */
#define TX_GLUE_KERNEL_VERSION	"6.5.1.202602a"

/* --- runtime vector table --------------------------------------------------- */

/* Swap VBAR_EL1 from start.S's boot_vectors to the ThreadX table. Called
 * from tx_application_define with interrupts masked (kernel init context),
 * so the switch window has no interrupt traffic. From this point
 * boot_vectors is unreachable - every exception goes through the port's
 * save/dispatch/restore machinery. */
static void vbar_install(void)
{
	__asm__ __volatile__(
		"ldr	x0, =_threadx_vector_table\n"
		"msr	vbar_el1, x0\n"
		"isb\n"
		::: "x0");
}

/* --- tick ------------------------------------------------------------------- */

/* Kernel tick work, run from tick.c's internal ISR via the user-handler
 * slot: expirations, time slice and the system time bump happen inside
 * _tx_timer_interrupt; tick.c reloads TVAL right after this returns, which
 * deasserts the level line BEFORE the entry's EOI - the rearm before EOI
 * order is the anti-tick-storm discipline.
 *
 * The slot (not an irq_register replacement) is the board-proven wiring:
 * the stamped log sink reads os_tick_get_count32() once the stamp switches,
 * so tick.c's count must keep advancing or every post-switch stamp freezes;
 * driving the kernel through the slot keeps the count and the kernel clock
 * in lockstep with zero base-layer change. */
static void tx_tick_kernel_work(void)
{
	_tx_timer_interrupt();
}

/* Stack-checking notify (tx_user.h turns TX_ENABLE_STACK_CHECKING on): name
 * the culprit and park - the serial line is the evidence. The emergency
 * outlet: this fires with a blown stack and possibly during someone else's
 * print; board_log would risk deadlocking on the print lock and turning the
 * one diagnostic we get into silence. */
static void tx_stack_error_handler(TX_THREAD *thread_ptr)
{
	board_log_emergency("tx: stack error '%s' sp=%p stack=%p..%p (%lu bytes)",
			    thread_ptr->tx_thread_name,
			    thread_ptr->tx_thread_stack_ptr,
			    thread_ptr->tx_thread_stack_start,
			    thread_ptr->tx_thread_stack_end,
			    (unsigned long)thread_ptr->tx_thread_stack_size);
	for (;;) {
		ARCH_WFE();
	}
}

void tx_application_define(void *first_unused_memory)
{
	vbar_install();

	LOG_INFO("tx: kernel entered version=%s\n", TX_GLUE_KERNEL_VERSION);

	/* The port dereferenced __top_of_ram into _tx_initialize_unused_memory;
	 * this image never consumes the value, so printing the pointer is the
	 * whole proof that the read landed in mapped RAM. Spelled as explicit
	 * hex rather than %p so the line reads the same under any formatter the
	 * image links. */
	LOG_ANCHOR("tx: unused_mem=0x%08x\n", (unsigned)(uintptr_t)first_unused_memory);

	tx_thread_stack_error_notify(tx_stack_error_handler);

#if defined(TX_EXECUTION_PROFILE_ENABLE)
	/* Per-thread execution attribution (EPK=1 builds): the counters start at
	 * the scheduler's first switch; the console's `gmac etime` reads totals
	 * plus the ISR and idle accumulators. Anchored so the round can prove the
	 * kit actually armed rather than reading zeros as "no thread ran". */
	LOG_ANCHOR("tx: execution profile enabled\n");
#endif

	/* Arm the tick through the OS_Tick_* seam (the board layer carries no
	 * kernel dependency). Interrupts stay masked until _tx_thread_schedule
	 * unmasks them; the timer pends meanwhile. */
	if (OS_Tick_Setup((uint32_t)CONFIG_TX_TICKS_PER_SECOND,
			  tx_tick_kernel_work) == 0) {
		LOG_INFO("tick: source=%s irq=%d freq=%uHz\n",
			  board_tick_config.source == OS_TICK_SOURCE_VIRT ?
				  "virt" : "phys_ns",
			  OS_Tick_GetIRQn(),
			  (unsigned)(OS_Tick_GetClock() /
				     OS_Tick_GetInterval()));
		board_log_stamp_use_tick();
		LOG_ANCHOR("stamp: switched to tick\n");
	} else {
		LOG_ANCHOR("tick: SETUP FAILED\n");
	}

	tx_board_app_define();
}

/* Fatal landing from tx_vectors.S (kind: 1 = sync, 2 = fiq, 3 = serror).
 * Record the machine state BEFORE printing - a fault inside the print path
 * must not erase the first fault's evidence - then emit one line and park.
 * There is no recovery: the anchor name is the fail-trigger token. */
void tx_fault_park(unsigned int kind)
{
	static volatile unsigned long rec_esr;
	static volatile unsigned long rec_far;
	static volatile unsigned long rec_elr;
	static volatile unsigned long rec_lr;
	static volatile unsigned long rec_sp;
	static volatile unsigned long rec_sp_el0;

	ARCH_MRS(esr_el1, rec_esr);
	ARCH_MRS(far_el1, rec_far);
	ARCH_MRS(elr_el1, rec_elr);
	ARCH_MRS(sp_el0, rec_sp_el0);
	__asm__ __volatile__("mov %0, x30" : "=r"(rec_lr));
	__asm__ __volatile__("mov %0, sp" : "=r"(rec_sp));

	/* Emergency outlet: a fault can land while the print lock is held by
	 * an interrupted context - board_log here would spin in
	 * print_lock_take with IRQs masked and the system would freeze
	 * SILENTLY. The whole point of the park is to leave one line. Every
	 * register goes out at full width: the faulting address decides which
	 * address space was reached, and a truncated value reads as a
	 * different address than the one the CPU actually used. */
	board_log_emergency("tx_fault_park kind=%u esr=%lx far=%lx elr=%lx lr=%lx sp=%lx sp_el0=%lx",
			    kind, rec_esr, rec_far, rec_elr, rec_lr, rec_sp,
			    rec_sp_el0);

	/*
	 * The register set names the instruction that faulted, not the chain
	 * that got there: a fault inside a leaf helper (a semaphore release, a
	 * pool operation) is one line with no owner. The thread identity and a
	 * window of the faulting stack close that gap - the addresses resolve
	 * offline against the build's ELF. Four words a line, deliberately
	 * compact: the console's ring drops bursts, so a dump has to be small
	 * enough to survive it.
	 */
	{
		TX_THREAD *thread = tx_thread_identify();
		const char *name = (thread != NULL && thread->tx_thread_name != NULL) ?
		    thread->tx_thread_name : "?";

		board_log_emergency("tx_fault_park thread=%s fault_sp=%lx",
				    name, rec_sp);
	}

	{
		const unsigned long *stk =
		    (const unsigned long *)(rec_sp & ~(unsigned long)0xf);
		unsigned int i;

		for (i = 0; i < 32U; i += 4U) {
			board_log_emergency("fault_stack+%02x: %lx %lx %lx %lx",
					    i * (unsigned int)sizeof(unsigned long),
					    stk[i], stk[i + 1U], stk[i + 2U],
					    stk[i + 3U]);
		}
	}

	for (;;) {
		ARCH_WFE();
	}
}
