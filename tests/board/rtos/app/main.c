/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   main.c
 * @brief  Board entry: console, kernel facade, the runner thread, scheduler.
 *
 * Boot chain: start.S has normalised to EL1 and enabled the MMU; this file
 * brings the polled console up, prints the two banner lines the board runner
 * keys on, initialises the kernel through the CMSIS-RTOS2 facade, creates the
 * runner thread and starts the scheduler.  The backend is selected by the
 * build (CMSIS2_BACKEND_FREERTOS / _THREADX) and this file names no kernel
 * header: the same source is both images.
 */

#include <stdint.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"

#include "arch_asm.h"
#include "uart.h"

#if defined(RTOS_BOARD_AUTO_RESET)
#include "reset.h"
#endif

#include "cases.h"
#include "clock.h"
#include "runner.h"
#if defined(CONFIG_SHELL)
#include "shell_port.h"
#endif

#ifndef FW_BUILD_TAG
#define FW_BUILD_TAG "local"
#endif

#ifndef RTOS_BACKEND_NAME
#define RTOS_BACKEND_NAME "unknown"
#endif

#ifndef RTOS_KERNEL_NAME
#define RTOS_KERNEL_NAME "unknown"
#endif

/* Unrecoverable bring-up failure: report and recover.  A failure that cannot
 * speak is indistinguishable from a dead board; a spoken one resets the
 * bench (RTOS_BOARD_AUTO_RESET builds) or parks for the power button. */
static void fatal(const char *what)
{
	uart_early_puts("rtos: fatal ");
	uart_early_puts(what);
	uart_early_puts("\n");
#if defined(RTOS_BOARD_AUTO_RESET)
	uart_early_puts("rtos: resetting board\n");
	board_system_reset();
#endif
	uart_early_puts("rtos: parked; power-cycle to recover\n");

	ARCH_DAIF_SET(0xf);
	for (;;) {
		ARCH_WFE();
	}
}

#if defined(CONFIG_APP_WATCHDOG)
#include "log.h"

/*
 * The stack's core-lock witness (third-party/lwip/ports/sys_arch.c records the
 * owner and the take time): both read the record without taking the lock, so
 * the watchdog can name a wedged holder while everything that needs the lock
 * is stopped.
 */
extern const char *sys_arch_core_lock_owner(void);
extern unsigned long sys_arch_core_lock_held_ms(void);
/* the lwIP arm's mailbox heartbeat; weak so an image without the net case
 * still links the watchdog. */
extern unsigned long lkpi_net_tcpip_beat(void) __attribute__((weak));
#if defined(RTOS_CASE_RTW88_NETIO) || defined(RTOS_CASE_RTW89_NETIO)
void lkpi80211_rx_boundary_counters(uint32_t *in, uint32_t *refused,
				    uint32_t *drops, uint32_t *frames,
				    uint32_t *qlen);
#endif

/*
 * Liveness witness.  Every "the board went silent" report so far had two
 * readings: the work stopped, or the thing that prints stopped.  A thread at
 * the highest priority that writes the polled console on a fixed cadence
 * separates them: as long as the marker keeps coming, the tick, the scheduler
 * and the console path are alive and only the case's own thread is stuck;
 * when it stops too, the wedge is below the scheduler (interrupts off in a
 * critical section, or a fault).
 *
 * The counter line answers the next question without a second boot: whether
 * the receive boundary kept counting (the frame source is alive), whether its
 * queue backed up (the delivery worker is the stopped one), and who holds the
 * stack lock.  Reads only, no lock taken.
 */
static void watchdog_entry(void *arg)
{
	(void)arg;
	for (;;) {
#if defined(RTOS_CASE_RTW88_NETIO) || defined(RTOS_CASE_RTW89_NETIO)
		uint32_t in = 0, ref = 0, drop = 0, frames = 0, q = 0;

		lkpi80211_rx_boundary_counters(&in, &ref, &drop, &frames, &q);
		board_log_emergency("WD: alive rx_in=%lu rx_q=%lu lock=%s held=%lums beat=%lu\n",
		    (unsigned long)in, (unsigned long)q,
		    sys_arch_core_lock_owner(), sys_arch_core_lock_held_ms(),
		    (lkpi_net_tcpip_beat != NULL) ? lkpi_net_tcpip_beat() : 0ul);
#else
		board_log_emergency("WD: alive lock=%s held=%lums beat=%lu\n",
		    sys_arch_core_lock_owner(), sys_arch_core_lock_held_ms(),
		    (lkpi_net_tcpip_beat != NULL) ? lkpi_net_tcpip_beat() : 0ul);
#endif
		(void)osDelay(2000u);
	}
}

static int watchdog_start(void)
{
	osThreadAttr_t attr;

	memset(&attr, 0, sizeof(attr));
	attr.name = "wd";
	/* 4 KiB: the counter line formats through vsnprintf, and a 1 KiB
	 * witness stack died on its first formatted line (2026-11-04). */
	attr.stack_size = 4096u;
	attr.priority = osPriorityRealtime;
	return (osThreadNew(watchdog_entry, NULL, &attr) != NULL) ? 0 : -1;
}
#endif

int main(void)
{
	uart_early_init();
	rtos_clock_init();

	uart_early_puts("rtos: banner backend=" RTOS_BACKEND_NAME
			" kernel=" RTOS_KERNEL_NAME
			" build=" FW_BUILD_TAG "\n");

	if (osKernelInitialize() != osOK) {
		fatal("kernel init failed");
	}

#if defined(RTOS_CASE_WLAN)
	/* The vendor stack's linuxkpi services, created before the scheduler just
	 * as in the reference image; the case itself runs the module_init and the
	 * SDIO bring-up once the runner is scheduled. */
	case_wlan_prepare();
#endif

	uart_early_puts("rtos: kernel ready tick=");
	{
		uint32_t freq = osKernelGetTickFreq();
		char buf[12];
		int i = 0;

		if (freq == 0u) {
			uart_early_putc('0');
		}
		while ((freq != 0u) && (i < (int)sizeof(buf))) {
			buf[i++] = (char)('0' + (freq % 10u));
			freq /= 10u;
		}
		while (i > 0) {
			uart_early_putc(buf[--i]);
		}
	}
	uart_early_puts("Hz\n");

#if defined(CONFIG_APP_WATCHDOG)
	if (watchdog_start() != 0)
		fatal("watchdog thread create failed");
#endif

#if defined(CONFIG_SHELL)
	if (rtos_shell_start() != 0) {
		fatal("shell thread create failed");
	}
#else
	if (rtos_runner_start() != 0) {
		fatal("runner thread create failed");
	}
#endif

	if (osKernelStart() != osOK) {
		fatal("scheduler start failed");
	}

	fatal("scheduler returned");
	return 0;
}