/*
 * @file   tx_glue.c
 * @brief  The ONE place the ThreadX kernel meets the board layer (SPEC S3
 *         threadx-up-boot FR-3).
 *
 * The kernel side calls two symbols owned here: tx_application_define()
 * (object creation, tick arming, vector-table installation) and
 * tx_fault_park() (fatal landing from tx_vectors.S). Everything the kernel
 * needs from the machine arrives through the base layer's own seams - the
 * OS_Tick_* surface (os/baremetal/tick.h), the GIC handler table
 * (irq_register), board_log - so swapping the kernel again touches only
 * os/threadx + os/cmsis2 (the "kernel replacement seam" shape).
 *
 * arch/board/os-baremetal files carry no tx_* include (FR-3): the tx
 * headers stop at this directory (and os/cmsis2 from T3 on).
 *
 * SPSel model (spec §3.2.3): from tx_kernel_enter_spsel0 (tx_vectors.S) on,
 * everything runs at EL1 with SPSel=0. tx_application_define runs on the
 * boot stack with interrupts masked, still inside
 * _tx_initialize_kernel_enter.
 */

#include <stdint.h>

#include "tx_api.h"
#include "tx_thread.h"

#include "board_conf.h"
#include "log.h"
#include "tick.h"

#ifdef CONFIG_GDB_STUB
#include "gdb_glue.h"
#include "gdb.h"
#include "uart.h"
#endif

/* The port's tick dispatch entry (port-owned, not in the common headers). */
extern void _tx_timer_interrupt(void);

/* The port version this image was built against (tx_port.h's _tx_version_id
 * carries the same string; the pin is locked so the literal is stable).
 * Anchor line, spec §4/T1 判据 3. */
#define TX_GLUE_KERNEL_VERSION	"6.5.1.202602a"

/* --- runtime vector table --------------------------------------------------- */

/* Swap VBAR_EL1 from start.S's boot_vectors to the ThreadX table. Called
 * from tx_application_define with interrupts masked (kernel init context),
 * so the switch window has no interrupt traffic. From this point
 * boot_vectors is unreachable - every exception goes through the port's
 * save/dispatch/restore machinery (spec §3.3.1 two-table lifecycle). */
static void vbar_install(void)
{
	__asm__ __volatile__(
		"ldr	x0, =_threadx_vector_table\n"
		"msr	vbar_el1, x0\n"
		"isb\n"
		::: "x0");
}

/* --- tick (T2) --------------------------------------------------------------- */

/* Kernel tick work, run from tick.c's internal ISR via the user-handler
 * slot: expirations, time slice and the system time bump happen inside
 * _tx_timer_interrupt; tick.c reloads TVAL right after this returns, which
 * deasserts the level line BEFORE gic_irq_dispatch's tail EOI - the rearm
 * before EOI order is the anti-tick-storm discipline (spec §3.3.4).
 *
 * Slot choice vs the spec's irq_register-replacement shape: the stamped
 * log sink (log.c) reads os_tick_get_count32() once the stamp switches, so
 * tick.c's count must keep advancing or every post-switch stamp freezes.
 * Driving the kernel through the user-handler slot keeps the count and the
 * kernel clock in lockstep with zero base-layer change; the kernel still
 * owns time (tx_time_get). Deviation recorded for the spec 回填. */
static void tx_tick_kernel_work(void)
{
#ifdef CONFIG_GDB_STUB
	/* T4 tick poll (the ThreadX world's gdb_poll site, spec §3.6): the
	 * base watcher gives the full running-target semantics - Ctrl-C
	 * break-in, '$' attach with a synthetic poll-site stop, chatter
	 * bytes dropped. Gated: dbgport in, no session owning the UART
	 * (the stub's own RSP traffic must not be eaten as bytes), and -
	 * since S4 - the shell's RX interrupt not armed (once it is, the
	 * ISR owns the console FIFO; a polled scan here would race it for
	 * the same bytes. The ISR routes 0x03/'$' to the stub itself via
	 * board_console_break_hook / gdb_isr_attach).
	 * From this IRQ both break-ins freeze the world inside the tick
	 * interrupt; continue ERETs back into the tick handler. The spec's
	 * "ISR 内不合成 poll-site" reservation is adjudicated here: the
	 * attach criterion has no other entry, and the synthetic stop is
	 * the baremetal board-proven shape. */
	if (gdb_dbgport_ready() != 0 && gdb_session_active == 0
#ifdef CONFIG_UART_RX_IRQ
	    && uart_console_rx_armed() == 0
#endif
	) {
		gdb_poll();
	}
#endif
	_tx_timer_interrupt();
}

#ifdef CONFIG_GDB_STUB
/* Strong answer to log.c's weak break seam (S4 spec §3.6.2): the shell's
 * RX path hands every 0x03 to the stub first. gdb_break() raises the real
 * BRK - the sync vector carves the trapframe and the stub owns the CPU
 * until continue; from the RX interrupt that freezes the machine exactly
 * like the tick-path break-in (board-proven T4 shape). Return 1 = the
 * byte was consumed (the adapter must not ring it); 0 never happens with
 * the stub compiled in. */
int board_console_break_hook(void)
{
	gdb_break();
	return 1;
}
#endif

/* Stack-checking notify (tx_user.h turns TX_ENABLE_STACK_CHECKING on): name
 * the culprit and park - the serial line is the evidence. The emergency
 * outlet: this fires with a blown stack and possibly during someone else's
 * print; board_log would risk deadlocking on the print lock and turning the
 * one diagnostic we get into silence (N7-stab round). */
static void tx_stack_error_handler(TX_THREAD *thread_ptr)
{
	board_log_emergency("tx: stack error '%s' sp=%p stack=%p..%p (%lu bytes)",
			    thread_ptr->tx_thread_name,
			    thread_ptr->tx_thread_stack_ptr,
			    thread_ptr->tx_thread_stack_start,
			    thread_ptr->tx_thread_stack_end,
			    (unsigned long)thread_ptr->tx_thread_stack_size);
	for (;;) {
		__asm__ __volatile__("wfe");
	}
}

/* CMSIS adapter's deferred-creation replay (os/cmsis2/cmsis2_threadx.c):
 * threads claimed during the Initialize..Start window are created here,
 * the only window ThreadX allows pre-start object creation. */
extern void cmsis2_tx_application_define(void);

void tx_application_define(void *first_unused_memory)
{
	vbar_install();

	board_log("tx: kernel entered version=%s\n", TX_GLUE_KERNEL_VERSION);

	/* Open question 4 evidence: the port dereferenced __top_of_ram into
	 * _tx_initialize_unused_memory; this image never consumes the value
	 * (allocation is tlsf-based from T3), so printing the pointer is the
	 * whole proof that the read landed in mapped RAM. */
	board_log("tx: unused_mem=%p\n", first_unused_memory);

	tx_thread_stack_error_notify(tx_stack_error_handler);

	/* Arm the tick through the OS_Tick_* seam (board layer carries no
	 * kernel dependency), announce the stamp-source switch. Interrupts
	 * stay masked until _tx_thread_schedule unmasks them; the timer
	 * pends meanwhile. */
	if (OS_Tick_Setup((uint32_t)CONFIG_TX_TICKS_PER_SECOND,
			  tx_tick_kernel_work) == 0) {
		board_log("tick: source=%s irq=%d freq=%uHz\n",
			  board_tick_config.source == OS_TICK_SOURCE_VIRT ?
				  "virt" : "phys_ns",
			  OS_Tick_GetIRQn(),
			  (unsigned)(OS_Tick_GetClock() /
				     OS_Tick_GetInterval()));
		board_log_stamp_use_tick();
		board_log("stamp: switched to tick\n");
	} else {
		board_log("tick: SETUP FAILED\n");
	}

	cmsis2_tx_application_define();
}

/* Fatal landing from tx_vectors.S (kind: 1 = sync, 3 = serror). Record the
 * machine state BEFORE printing - a fault inside the print path must not
 * erase the first fault's evidence - then emit one line and park. There is
 * no recovery: the anchor name is the fail-trigger token of spec §4/T1. */
void tx_fault_park(unsigned int kind)
{
	static volatile unsigned long rec_esr;
	static volatile unsigned long rec_far;
	static volatile unsigned long rec_elr;
	static volatile unsigned long rec_lr;
	static volatile unsigned long rec_sp;

	__asm__ __volatile__("mrs %0, esr_el1" : "=r"(rec_esr));
	__asm__ __volatile__("mrs %0, far_el1" : "=r"(rec_far));
	__asm__ __volatile__("mrs %0, elr_el1" : "=r"(rec_elr));
	__asm__ __volatile__("mov %0, x30" : "=r"(rec_lr));
	__asm__ __volatile__("mov %0, sp" : "=r"(rec_sp));

	/* Emergency outlet (N7-stab): a fault can land while the print lock is
	 * held by an interrupted context - board_log here would spin in
	 * print_lock_take with IRQs masked and the system would freeze
	 * SILENTLY. The whole point of the park is to leave one line. */
	board_log_emergency("tx_fault_park kind=%u esr=%lx far=%lx elr=%lx lr=%lx sp=%lx",
			    kind,
			    rec_esr & 0xffffffffUL,
			    rec_far & 0xffffffffUL,
			    rec_elr, rec_lr, rec_sp);

	for (;;) {
		__asm__ __volatile__("wfe");
	}
}
