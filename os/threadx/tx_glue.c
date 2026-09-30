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

#include "log.h"

/* The port version this image was built against (tx_port.h's _tx_version_id
 * carries the same string; the pin is locked so the literal is stable).
 * Anchor line, spec §4/T1 判据 3. */
#define TX_GLUE_KERNEL_VERSION	"6.5.1.202602a"

/* Smoke thread: proves the scheduler actually dispatches (T1 判据 3, second
 * anchor). 4 KiB is the round's thread-stack default (spec §3.4.2: it must
 * also hold an ISR frame; TX_MINIMUM_STACK=200 is the port floor, not a
 * design value). Heap-backed stacks arrive with the T3 heap. */
#define TX_SMOKE_STACK_BYTES	4096U
#define TX_SMOKE_PRIORITY	16U

static TX_THREAD tx_smoke_thread;
static unsigned char tx_smoke_stack[TX_SMOKE_STACK_BYTES]
	__attribute__((aligned(16)));

static void tx_smoke_entry(ULONG input)
{
	(void)input;

	board_log("tx: first thread ran\n");

	/* One shot, then off the run list - the image idles silently after
	 * this (T1 判据 4). */
	(void)tx_thread_terminate(&tx_smoke_thread);
	for (;;) {
		__asm__ __volatile__("wfe");
	}
}

/* Stack-checking notify (tx_user.h turns TX_ENABLE_STACK_CHECKING on): name
 * the culprit and park - the serial line is the evidence. */
static void tx_stack_error_handler(TX_THREAD *thread_ptr)
{
	board_log("tx: stack error '%s' sp=%p stack=%p..%p (%lu bytes)\n",
		  thread_ptr->tx_thread_name,
		  thread_ptr->tx_thread_stack_ptr,
		  thread_ptr->tx_thread_stack_start,
		  thread_ptr->tx_thread_stack_end,
		  (unsigned long)thread_ptr->tx_thread_stack_size);
	for (;;) {
		__asm__ __volatile__("wfe");
	}
}

void tx_application_define(void *first_unused_memory)
{
	board_log("tx: kernel entered version=%s\n", TX_GLUE_KERNEL_VERSION);

	/* Open question 4 evidence: the port dereferenced __top_of_ram into
	 * _tx_initialize_unused_memory; this image never consumes the value
	 * (allocation is tlsf-based from T3), so printing the pointer is the
	 * whole proof that the read landed in mapped RAM. */
	board_log("tx: unused_mem=%p\n", first_unused_memory);

	tx_thread_stack_error_notify(tx_stack_error_handler);

	if (tx_thread_create(&tx_smoke_thread, "tx_smoke", tx_smoke_entry, 0,
			     tx_smoke_stack, TX_SMOKE_STACK_BYTES,
			     TX_SMOKE_PRIORITY, TX_SMOKE_PRIORITY,
			     TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS) {
		board_log("tx: smoke thread create FAIL\n");
	}
	/* T2 adds: vbar_install() + tick arm (OS_Tick_Setup 1000 Hz +
	 * irq_register swap + enable) + stamp switch. T3 adds: CMSIS deferred
	 * creations. */
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

	board_log("tx_fault_park kind=%u esr=%lx far=%lx elr=%lx lr=%lx sp=%lx\n",
		  kind,
		  rec_esr & 0xffffffffUL,
		  rec_far & 0xffffffffUL,
		  rec_elr, rec_lr, rec_sp);

	for (;;) {
		__asm__ __volatile__("wfe");
	}
}
