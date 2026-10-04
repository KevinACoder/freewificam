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
	/* [W3 forensics] the created-chain poison sampler: once a second,
	 * validate the chain and emergency-log the FIRST sight of a poison
	 * with the tick-second count - the boot log's phases carry the
	 * same tick-based stamps, so this pins the overwrite's time
	 * window without touching the vendor or the RTOS internals.
	 * [W6] the one-read judgment (issues/2026-10-04-...-sema-chain-
	 * poison-wpa-image): report the idx/node/next triple so the hit
	 * classifies itself - next inside the real heap span
	 * (__heap_start..__heap_end, the CMSIS control blocks come from
	 * pvPortMalloc deep past 0xb000000) means the hardcoded window is
	 * stale, not the chain. */
	{
		static unsigned int sampler_s;
		static unsigned int sampler_hit;

		if (sampler_hit == 0U) {
			sampler_s++;
				if (sampler_s >= 1000U) {
					unsigned int p_idx = 0;
					unsigned long p_node = 0;
					unsigned long p_next = 0;

					sampler_s = 0;
					if (tx_sema_chain_validate(&p_idx,
					    &p_node, &p_next) != 0) {
						sampler_hit = 1U;
						board_log_emergency(
		    "!!! sema chain POISON first seen at ~%u s idx=%u node=%lx next=%lx",
						    (unsigned)(tx_time_get() /
						       1000U),
						    p_idx, p_node, p_next);
					}
				}
		}
	}

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

/* [W3 forensics -> W6 fix] The created-chain walker's legal-address window.
 * Originally hardcoded 0xa000000..0xb000000 (image + the first ~5.6MB of the
 * heap as sized back then); the CMSIS control blocks come from pvPortMalloc
 * anywhere in the 44MB pool, so allocations past 0xb000000 (deep-heap wpa/lwip
 * objects, thread stacks at 0xd3..) tripped the guard without any real
 * corruption - the W4 "POISON" hits, 2/2 repro.  W6 closed it: two clean
 * boots with the sampler running and zero hits, and the one REAL fault this
 * round (timer-chain abort) parked with the sema chain fully coherent - the
 * stale window was the only "detector" that ever fired.  The bound is now
 * the linker's own heap end, covering every allocation the pool can hand
 * out. [S5b-W6] */
static unsigned long sema_chain_window_end(void)
{
	extern char __heap_end[];

	return (unsigned long)__heap_end;
}

/* [W3 forensics] Walk the ThreadX created-semaphore chain under a window
 * guard (image .. __heap_end).  The wlan-lane data aborts land inside
 * _txe_semaphore_create's dedup walk on a node whose created_next holds
 * deterministic garbage; this dump names the poison by position (its
 * neighbours on the chain) the moment the machine is parked.  Pointers
 * only: a name STRING could itself be garbage, and faulting inside the
 * terminal path is exactly what must never happen.  Shared with the
 * `usbkpi semchain` command for before/after chain evidence. */
void
tx_sema_chain_dump(void (*emit)(const char *fmt, ...))
{
	extern TX_SEMAPHORE *_tx_semaphore_created_ptr;
	extern UINT _tx_semaphore_created_count;
	TX_SEMAPHORE *n = _tx_semaphore_created_ptr;
	unsigned int i;

	emit("sema chain: head=%lx count=%u win_end=%lx",
	     (unsigned long)n, (unsigned)_tx_semaphore_created_count,
	     sema_chain_window_end());
	for (i = 0; n != NULL && i < 64; i++) {
		unsigned long nx =
		    (unsigned long)n->tx_semaphore_created_next;
		unsigned long pv =
		    (unsigned long)n->tx_semaphore_created_previous;

		emit("sema[%u] node=%lx name=%lx count=%lx susp=%lx scnt=%lx "
		     "next=%lx prev=%lx",
		     i, (unsigned long)n,
		     (unsigned long)n->tx_semaphore_name,
		     (unsigned long)n->tx_semaphore_count,
		     (unsigned long)n->tx_semaphore_suspension_list,
		     (unsigned long)n->tx_semaphore_suspended_count,
		     nx, pv);
		if (nx < 0xa000000UL || nx > sema_chain_window_end()) {
			emit("sema[%u] next out of window - POISON", i);
			break;
		}
		n = (TX_SEMAPHORE *)nx;
	}
}

/* [W3 forensics] Guarded validation pass over the created-semaphore chain:
 * returns -1 the first time a node's created_next leaves the image+heap
 * window (0xa000000..__heap_end), reporting the position; 0 when the whole
 * chain looks sane.  The completion-create path runs this before every
 * tx_semaphore_create so the boot log bisects WHICH create first saw the
 * poison - i.e. the exact window the overwrite happened in. */
int
tx_sema_chain_validate(unsigned int *idx_out, unsigned long *node_out,
    unsigned long *next_out)
{
	extern TX_SEMAPHORE *_tx_semaphore_created_ptr;
	TX_SEMAPHORE *n = _tx_semaphore_created_ptr;
	unsigned int i;

	for (i = 0; n != NULL && i < 256; i++) {
		unsigned long nx =
		    (unsigned long)n->tx_semaphore_created_next;

		if (nx != 0 && (nx < 0xa000000UL ||
		    nx > sema_chain_window_end())) {
			if (idx_out != NULL)
				*idx_out = i;
			if (node_out != NULL)
				*node_out = (unsigned long)n;
			if (next_out != NULL)
				*next_out = nx;
			return -1;
		}
		n = (TX_SEMAPHORE *)nx;
	}
	return 0;
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
	static volatile unsigned long rec_sp0;

	__asm__ __volatile__("mrs %0, esr_el1" : "=r"(rec_esr));
	__asm__ __volatile__("mrs %0, far_el1" : "=r"(rec_far));
	__asm__ __volatile__("mrs %0, elr_el1" : "=r"(rec_elr));
	__asm__ __volatile__("mov %0, x30" : "=r"(rec_lr));
	__asm__ __volatile__("mov %0, sp" : "=r"(rec_sp));
	/* Threads run on SP_EL0 (tx_irq_first_level's spsel switch); the
	 * same-EL exception entry forces SPSel=1, so `sp` below is the
	 * handler's EL1 frame, not the faulting thread's stack.  sp_el0
	 * is the crash-site SP for any fault taken from thread context -
	 * the W2 up-path round burned attribution time on the EL1
	 * artifact (= __stack_top - 0xb0, deterministic). */
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(rec_sp0));

	/* Emergency outlet (N7-stab): a fault can land while the print lock is
	 * held by an interrupted context - board_log here would spin in
	 * print_lock_take with IRQs masked and the system would freeze
	 * SILENTLY. The whole point of the park is to leave one line. */
	board_log_emergency("tx_fault_park kind=%u esr=%lx far=%lx elr=%lx lr=%lx sp=%lx sp0=%lx",
			    kind,
			    rec_esr & 0xffffffffUL,
			    rec_far,
			    rec_elr, rec_lr, rec_sp, rec_sp0);
	tx_sema_chain_dump(board_log_emergency);

	for (;;) {
		__asm__ __volatile__("wfe");
	}
}
