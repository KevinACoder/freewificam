/*
 * @file   mon.c
 * @brief  [S5b-W3 forensics] the stall monitor: a lock-free window into a
 *         silently wedged world.
 *
 * The wlan8851 up-stall (runs/20261004-rtl8851bu-w3-rtos/serial-r1.log):
 * console goes silent right after the cts2self handler print and stays
 * silent for minutes.  When "who is stuck, where" is the question, the
 * console from the stuck threads themselves is gone - so this monitor
 * runs at osPriorityRealtime (tx 7: above the usbkpi drain 9, the vendor
 * kthreads 20 and the shell 19, below the CherryUSB hub threads 0) and,
 * every period, walks the Kernel's created-object lists:
 *
 *  - every thread: state / run_count / priority / saved stack pointer.
 *    run_count deltas across dumps separate "everyone blocked" from "one
 *    thread spinning"; a frozen shell run_count localizes the waiter.
 *  - every mutex with waiters: name + owner + waiter count.  A ThreadX
 *    mutex suspension list names the blocked threads, so "shell waits on
 *    mutex X owned by Y" comes out directly - the deadlock answer.
 *  - every semaphore with waiters: name + count + waiter count (the
 *    cherryusb ring waitsems surface here; the waiting thread names the
 *    ring through its endpoint context in the same dump).
 *
 * The dump goes through board_log_emergency (no lock, no line assembler),
 * so it survives a wedged console-lock too; each line masks IRQs only
 * for its own UART burst.  The tick it prints proves whether the kernel
 * clock still moves.
 *
 * Raw forensics, not a product surface: started only when the app calls
 * wlan_mon_start() (wlan8851.config CONFIG_WLAN_MON), off everywhere else.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tx_api.h"

#include "board_conf.h"
#include "cmsis_os2.h"
#include "gicv3.h"
#include "log.h"

/* ThreadX's created-object list heads (tx_*_initialize.c); declared extern
 * for client TUs by the headers.  The monitor deliberately lives inside
 * the threadx adapt layer - one that could not see the real kernel tables
 * would be guessing. */
extern TX_THREAD *_tx_thread_created_ptr;
extern TX_MUTEX *_tx_mutex_created_ptr;
extern ULONG _tx_mutex_created_count;
extern TX_SEMAPHORE *_tx_semaphore_created_ptr;
extern ULONG _tx_semaphore_created_count;

/* Optional extra line(s) from the linuxkpi layer (control-transfer trail);
 * weak so non-wlan images never need the symbol. */
void wlan_mon_extra_probe(void) __attribute__((weak));

/* One dump every 10 s.  Each line is an independent emergency print, so
 * IRQs are masked only for the per-line UART burst (~5 ms), never for the
 * whole dump. */
#define MON_PERIOD_MS 10000U
#define MON_MAX_OBJECTS 64U

static const char *thread_name(const TX_THREAD *t)
{

	return (t != TX_NULL && t->tx_thread_name != TX_NULL) ?
	       t->tx_thread_name : "?";
}

static void mon_dump_threads(void)
{
	TX_THREAD *t = _tx_thread_created_ptr;
	TX_THREAD *self = tx_thread_identify();
	unsigned int n = 0U;

	if (t == TX_NULL) {
		board_log_emergency("MON no threads");
		return;
	}
	do {
		if (n >= MON_MAX_OBJECTS) {
			board_log_emergency("MON thread list truncated");
			break;
		}
		board_log_emergency("MON th %s st=%u run=%lu p=%u sp=%p%s",
				    thread_name(t),
				    (unsigned int)t->tx_thread_state,
				    (unsigned long)t->tx_thread_run_count,
				    (unsigned int)t->tx_thread_priority,
				    t->tx_thread_stack_ptr,
				    (t == self) ? " *" : "");
		n++;
		t = t->tx_thread_created_next;
	} while (t != TX_NULL && t != _tx_thread_created_ptr);
}

/* Mutexes with waiters: the deadlock face.  A mutex's suspension list runs
 * head = most recent waiter; the FIRST waiter's name plus the owner's
 * name is the whole story ("shell waits on 'usbh_mutex' owned by
 * 'usbh_hub2'"). */
static void mon_dump_mutexes(void)
{
	TX_MUTEX *m = _tx_mutex_created_ptr;
	unsigned int n = 0U;

	if (m == TX_NULL) {
		return;
	}
	do {
		if (n >= MON_MAX_OBJECTS) {
			break;
		}
		if (m->tx_mutex_suspended_count != 0U) {
			board_log_emergency(
			    "MON mx %s own=%s wait=%u susp=%s",
			    (m->tx_mutex_name != TX_NULL) ?
			    m->tx_mutex_name : "?",
			    (m->tx_mutex_owner != TX_NULL) ?
			    thread_name(m->tx_mutex_owner) : "-",
			    (unsigned int)m->tx_mutex_suspended_count,
			    (m->tx_mutex_suspension_list != TX_NULL) ?
			    thread_name(m->tx_mutex_suspension_list) : "-");
		}
		n++;
		m = m->tx_mutex_created_next;
	} while (m != TX_NULL && m != _tx_mutex_created_ptr);
}

/* Semaphores with waiters: the cherryusb ring waitsems and kernal event
 * sems surface here; a wedge stuck in a sync transfer shows its exact
 * waiting object. */
static void mon_dump_semaphores(void)
{
	TX_SEMAPHORE *s = _tx_semaphore_created_ptr;
	unsigned int n = 0U;

	if (s == TX_NULL) {
		return;
	}
	do {
		if (n >= MON_MAX_OBJECTS) {
			break;
		}
		if (s->tx_semaphore_suspended_count != 0U) {
			board_log_emergency(
			    "MON sm %s cnt=%lu wait=%u susp=%s",
			    (s->tx_semaphore_name != TX_NULL) ?
			    s->tx_semaphore_name : "?",
			    (unsigned long)s->tx_semaphore_count,
			    (unsigned int)s->tx_semaphore_suspended_count,
			    (s->tx_semaphore_suspension_list != TX_NULL) ?
			    thread_name(s->tx_semaphore_suspension_list) : "-");
		}
		n++;
		s = s->tx_semaphore_created_next;
	} while (s != TX_NULL && s != _tx_semaphore_created_ptr);
}

static void mon_dump(void)
{
	board_log_emergency("MON dump t=%lu", (unsigned long)tx_time_get());
	mon_dump_threads();
	mon_dump_mutexes();
	mon_dump_semaphores();
	if (wlan_mon_extra_probe != NULL) {
		wlan_mon_extra_probe();
	}
}

static void mon_task(void *arg)
{
	(void)arg;

	for (;;) {
		(void)osDelay(MON_PERIOD_MS);
		mon_dump();
	}
}

/* --- the IRQ-level watchdog ------------------------------------------------
 *
 * The thread monitor can only speak while the scheduler still runs.  The
 * N7-stab signature on this board was the opposite: an ISR at or above the
 * tick's level starving every thread (the monitor would print nothing and
 * the log would look identical to a wedged scheduler).  This CNTP ISR at
 * raw 0x60 (above every peripheral line, the KI recipe) fires every 2 s
 * and prints one IRQ-level line with the kernel tick and the interrupted
 * thread name; comparing its timestamps with the thread dumps separates
 * "IRQ world alive, threads wedged" from "IRQ world starved".
 *
 * CNTV is the tick source in this image (CONFIG_TICK_SOURCE=virt), so PPI
 * 30 is free for the watchdog. */
#define MON_IRQ_PERIOD_MS 2000U

extern TX_THREAD *_tx_thread_current_ptr;

static void mon_irq_isr(void *arg)
{
	uint64_t freq;
	uint32_t period;
	TX_THREAD *cur = _tx_thread_current_ptr;

	(void)arg;

	/* re-arm first: CNTP is a down-counter, the level line clears on the
	 * TVAL rewrite (the tick's own programming model).  Period math:
	 * freq is the counter's Hz; (freq/1000)*MS avoids the 1000/MS
	 * integer-truncation trap (2000 ms -> 1000/2000 = 0 -> tval 0 ->
	 * self-inflicted IRQ storm at prio 0x60, board 2026-10-04). */
	__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
	period = (uint32_t)((freq / 1000u) * MON_IRQ_PERIOD_MS);
	__asm__ __volatile__("msr cntp_tval_el0, %0" ::"r"(period));
	__asm__ __volatile__("isb" ::: "memory");

	board_log_emergency("MON irq t=%lu cur=%s",
			    (unsigned long)tx_time_get(),
			    (cur != TX_NULL && cur->tx_thread_name != TX_NULL) ?
			    cur->tx_thread_name : "none");
}

static void mon_irq_start(void)
{
	uint64_t freq;
	uint32_t period;

	__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
	period = (uint32_t)((freq / 1000u) * MON_IRQ_PERIOD_MS);

	(void)irq_register(BOARD_TICK_MON_INTID, mon_irq_isr, NULL);
	(void)irq_set_priority(BOARD_TICK_MON_INTID,
			       BOARD_TICK_MON_PRIORITY_RAW);
	__asm__ __volatile__("msr cntp_tval_el0, %0" ::"r"(period));
	__asm__ __volatile__("msr cntp_ctl_el0, %0" ::"r"(1u));
	__asm__ __volatile__("isb" ::: "memory");
	(void)irq_enable(BOARD_TICK_MON_INTID);
	board_log("MON: irq watchdog armed intid=%u prio=0x%02x %ums\n",
		  (unsigned)BOARD_TICK_MON_INTID,
		  (unsigned)BOARD_TICK_MON_PRIORITY_RAW,
		  (unsigned)MON_IRQ_PERIOD_MS);
}

void wlan_mon_dump_now(void)
{

	mon_dump();
}

void wlan_mon_start(void)
{
	osThreadId_t id;

	mon_irq_start();
	id = osThreadNew(mon_task, NULL,
			 &(osThreadAttr_t){ .name = "wlanmon",
					    .stack_size = 4096,
					    .priority = osPriorityRealtime });
	board_log("MON: stall monitor %s\n",
		  (id != NULL) ? "started" : "FAILED");
}
