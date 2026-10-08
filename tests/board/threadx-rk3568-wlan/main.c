/**
 * @file    main.c
 * @brief   ThreadX board WLAN image: base + shell + USB host stack + the WLAN
 *          bring-up chain + the case table.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  facade consumer - the application talks to the kernel only through
 *          the CMSIS-RTOS2 facade (os/cmsis2/cmsis2.h); the shape follows the
 *          regression image (tests/board/threadx-rk3568-regression/main.c:52-75)
 *          with the WLAN start-up order added before the runner starts.
 *
 * Boot chain: start.S has already normalised to EL1 and enabled the MMU; this
 * file brings the console up, initialises the kernel facade, wires the WLAN
 * chain (the linuxkpi USB backend, the driver registry walk, the firmware
 * blob, the bridge's upward hooks), creates the runner thread and starts the
 * scheduler.
 *
 * The WLAN wiring order is load bearing and runs before the scheduler: the
 * backend must be registered before any hubport walks a class table, the
 * driver registry must be walked before the vendor-specific class hook can
 * probe, and the firmware bytes must be findable before the driver's probe
 * asks for them. All three are idempotent, so a re-entry is harmless.
 */

#include <stdint.h>

#include "board_suite.h"
#include "os/cmsis2/cmsis2.h"

#include <stdbool.h>
#include <string.h>

#include "arch_asm.h"
#include "uart.h"
#include "log.h"

void lkpi_usb_cherryusb_init(void);
void lkpi_usb_init(void);
int lkpi_fw_rtl8188eufw_register(void);
int wlan_seam_adapter_init(void);
void lkpi_log_sink_register(void (*sink)(const char *line));
void lkpi_core_init(void);
void lkpi_usb_force_sync_set(bool on);

#ifndef FW_BUILD_TAG
#define FW_BUILD_TAG "local"
#endif

/*
 * The linuxkpi log outlet: every lkpi_printf (the driver's own dev_info/dev_err
 * lines included) lands on the board log, stamped and line-disciplined like the
 * rest of the image. Without a sink the linuxkpi world is silent and the
 * driver's probe trace - the evidence the probe case judges - is lost.
 */
static void wlan_lkpi_sink(const char *line)
{
	static char buf[200];
	size_t n = 0;

	if (line == NULL)
		return;
	while (line[n] != '\0' && n < sizeof(buf) - 1)
		n++;
	while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
		n--;
	memcpy(buf, line, n);
	buf[n] = '\0';
	if (n != 0)
		board_log("lkpi: %s", buf);
}

static const osThreadAttr_t runner_thread_attr = {
	.name = "regress",
	.priority = osPriorityNormal,
	.stack_size = 4096U,
};

/* Unrecoverable bring-up failure: report and park (a failure that cannot
 * speak is indistinguishable from a dead board). */
static void wlan_fatal( const char *what )
{
	uart_early_puts( "fatal: " );
	uart_early_puts( what );
	uart_early_puts( "\n" );
	uart_early_puts( "fatal: parked; power-cycle to recover\n" );

	ARCH_DAIF_SET( 0xf );
	for( ;; ) {
		ARCH_WFE();
	}
}

int main( void )
{
	uart_early_init();

	uart_early_puts( "threadx-rk3568-wlan bring-up " );
	uart_early_puts( FW_BUILD_TAG );
	uart_early_puts( "\n" );

	if( osKernelInitialize() != osOK ) {
		wlan_fatal( "kernel init failed" );
	}

	/*
	 * The WLAN chain's start-up order (each step is a hard prerequisite of
	 * the next; the board log line after each names its result, so a boot
	 * that stops early says where):
	 *
	 *   1. the linuxkpi core services - the system workqueue the driver's
	 *      delayed work (rate adaptation, calibration) and the engine's
	 *      deferred paths ride on; without it that work is silently dropped
	 *      (lkpi_queue_delayed_work returns false on a NULL queue).
	 *   2. the USB backend (the CherryUSB seam), before anything enumerates.
	 *   3. the driver registry walk (.lkpi_module), before any class hook
	 *      can probe.
	 *   4. the firmware blob, before the driver's probe asks for it.
	 *   5. the bridge's upward hooks.
	 */
	lkpi_log_sink_register(wlan_lkpi_sink);
	lkpi_core_init();
	board_log( "wlan: core services up" );
	lkpi_usb_cherryusb_init();
	board_log( "wlan: usb backend registered" );
	lkpi_usb_init();
	/*
	 * The transfer model the driver needs: rtl8xxxu rides completion
	 * callbacks (its RX urbs resubmit themselves from rtl8xxxu_rx_complete
	 * and its TX completions free sender state), so submissions must be
	 * asynchronous. The engine's default is the blocking lane (the
	 * storage-shaped bring-up), which consumes each transfer inline and
	 * never runs the driver's callback - measured on the board: the sweep
	 * transmitted fine and received nothing (rx=0 with zero irq-backed
	 * completions). Same switch the host body suite sets before driving a
	 * callback-based driver.
	 */
	lkpi_usb_force_sync_set(false);
	board_log( "wlan: driver registry walked (async transfer model)" );
	if( lkpi_fw_rtl8188eufw_register() != 0 ) {
		board_log( "wlan: firmware registration failed" );
	} else {
		board_log( "wlan: firmware blob registered" );
	}
	wlan_seam_adapter_init();
	board_log( "wlan: chain wired (backend + registry + firmware + bridge)" );

	if( osThreadNew( board_suite_run, 0, &runner_thread_attr ) == 0 ) {
		wlan_fatal( "runner thread create failed" );
	}

	if( osKernelStart() != osOK ) {
		wlan_fatal( "scheduler start failed" );
	}

	/* Not reached: the kernel does not return. */
	wlan_fatal( "scheduler returned" );
	return 0;
}
