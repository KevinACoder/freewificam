/*
 * @file    main.c
 * @brief   ThreadX RK3568 bring-up smoke: two threads at different
 *          priorities, a one-second tick ladder and a slower liveness count;
 *          resident after PASS (no reset command in the image - the board
 *          session ends through the lab's power procedure).
 * @author  zhugengyu
 * @date    07.10.2026
 * @design  builder - the boot chain is a fixed sequence, each stage owned by
 *          one file: start.S (EL2->EL1, boot vectors, BSS) -> main() here
 *          (console, SPSel hand-over) -> the kernel's init chain ->
 *          tx_application_define (tx_glue.c: VBAR install, tick arming,
 *          anchors) -> tx_board_app_define (this file: the two threads, the
 *          kernel's only pre-scheduler object-creation window).
 *
 * The thread stacks and control blocks are static: the kernel does not need
 * a heap for this image, and the acceptance criterion says so.
 *
 * Anchor lines (format is contract): the glue prints the kernel/tick/stamp
 * anchors; this file prints the ladder ("tick: t=<n>", one beat per second
 * off tx_time_get), the PASS token after the third beat, and the lo
 * thread's count - the "second thread is scheduled" evidence.
 */

#include <stddef.h>
#include <stdint.h>

#include "tx_api.h"

#include "log.h"
#include "uart.h"

/* The SPSel hand-over stub (tx_vectors.S): enters the kernel with SP_EL0 =
 * the boot stack. Never returns. */
extern void tx_kernel_enter_spsel0(void);

#define SMOKE_STACK_BYTES 4096U
#define HI_PRIORITY 5U
#define LO_PRIORITY 10U

static TX_THREAD hi_thread;
static TX_THREAD lo_thread;
static ULONG hi_stack[SMOKE_STACK_BYTES / sizeof(ULONG)];
static ULONG lo_stack[SMOKE_STACK_BYTES / sizeof(ULONG)];

/* The hi thread: one ladder beat per second off the kernel clock, PASS after
 * the third beat, then keeps beating - the resident heartbeat the stability
 * window reads.
 *
 * The first wait aligns to the next whole-second boundary instead of sleeping
 * a flat 1000: the tick armed during kernel init is already pending when the
 * scheduler unmasks interrupts, so the clock reads 1 (not 0) by the time the
 * first thread runs - a flat sleep would put every beat one tick late
 * (t=1001/2002/...) and the ladder's second boundaries would drift off the
 * stamps. Measured on the first board run. */
static void thr_hi(ULONG id)
{
	unsigned beats = 0U;
	ULONG now;

	(void)id;

	now = tx_time_get();
	(void)tx_thread_sleep(1000U - (now % 1000U));

	for (;;) {
		beats++;
		board_log("tick: t=%lu\n", (unsigned long)tx_time_get());
		if (beats == 3U) {
			board_log("threadx: smoke PASS\n");
		}
		(void)tx_thread_sleep(1000U);
	}
}

/* The lo thread: a slower periodic count. Its line proves a second thread
 * at a different priority is being scheduled. */
static void thr_lo(ULONG id)
{
	unsigned n = 0U;

	(void)id;
	for (;;) {
		(void)tx_thread_sleep(2000U);
		board_log("thr[lo]: n=%u\n", ++n);
	}
}

/* Called from tx_glue.c's tx_application_define: the only window ThreadX
 * allows pre-scheduler object creation. */
void tx_board_app_define(void)
{
	(void)tx_thread_create(&hi_thread, "hi", thr_hi, 0,
			       hi_stack, sizeof(hi_stack),
			       HI_PRIORITY, HI_PRIORITY,
			       TX_NO_TIME_SLICE, TX_AUTO_START);
	(void)tx_thread_create(&lo_thread, "lo", thr_lo, 0,
			       lo_stack, sizeof(lo_stack),
			       LO_PRIORITY, LO_PRIORITY,
			       TX_NO_TIME_SLICE, TX_AUTO_START);
}

int main(void)
{
	/* The polled console speaks only after a full re-program. */
	uart_early_init();

	/* Everything else (VBAR switch, tick, threads) happens inside the
	 * kernel's init chain. Never returns. */
	tx_kernel_enter_spsel0();

	return 0;	/* unreachable */
}
