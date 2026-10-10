/**
 * @file    main.c
 * @brief   ThreadX board rtw89 image: base + shell + USB host stack + the WLAN
 *          bring-up chain + the case table.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  facade consumer - the application talks to the kernel only through
 *          the CMSIS-RTOS2 facade (os/cmsis2/cmsis2.h); the shape is the WLAN
 *          image's (tests/board/threadx-rk3568-wlan/main.c) with this image's
 *          driver and firmware named instead.
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
int lkpi_fw_rtw8851b_fw_register(void);
int wlan_seam_adapter_init(void);
void lkpi_log_sink_register(void (*sink)(const char *line));
void lkpi_core_init(void);
void lkpi_usb_force_sync_set(bool on);

/* the WPA world's wiring: the supplicant backend instance and the message
 * observer (both scalars-only faces) */
int wlan_wpa_vendor_init(void);
void wlan_wpa_obs_init(void);
int wlan_radio_start(void);

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
		/* The line is terminated here: the driver's own dev_* format
		 * strings do not all end with a newline, and the console only
		 * splits a line at its length cap - an unterminated print glues
		 * onto whatever is written next, and the split then cuts a line
		 * (a judged anchor included) in half. */
		board_log("lkpi: %s\n", buf);
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

	uart_early_puts( "threadx-rk3568-rtw89 bring-up " );
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
	board_log( "wlan: core services up\n" );
	lkpi_usb_cherryusb_init();
	board_log( "wlan: usb backend registered\n" );
	lkpi_usb_init();
	/*
	 * The transfer model the driver needs: rtw89 rides completion callbacks
	 * (its RX urbs resubmit themselves from their completion and its TX
	 * completions release the sender's urb), so submissions must be
	 * asynchronous. The engine's default is the blocking lane (the
	 * storage-shaped bring-up), which consumes each transfer inline and
	 * never runs the driver's callback - measured on this board with the
	 * other driver: the sweep transmitted fine and received nothing (rx=0
	 * with zero irq-backed completions). Same switch the host body suite
	 * sets before driving a callback-based driver.
	 */
	lkpi_usb_force_sync_set(false);
	board_log( "wlan: driver registry walked (async transfer model)\n" );
	if( lkpi_fw_rtw8851b_fw_register() != 0 ) {
		board_log( "wlan: firmware registration failed\n" );
	} else {
		board_log( "wlan: firmware blob registered\n" );
	}
	wlan_seam_adapter_init();
	board_log( "wlan: chain wired (backend + registry + firmware + bridge)" );

	/*
	 * The supplicant chain (each step the same idempotent shape as above):
	 * the message observer latches the association anchors the join case
	 * and the judge read, the backend instance registers the nine vendor
	 * operations against the bridge, and the radio's dwell pump takes over
	 * the channel walk for every sweep - the supplicant's own included.
	 */
	wlan_wpa_obs_init();
	if( wlan_wpa_vendor_init() != 0 ) {
		board_log( "wlan: wpa backend registration failed" );
	} else {
		board_log( "wlan: wpa backend registered" );
	}
	if( wlan_radio_start() != 0 ) {
		board_log( "wlan: radio pump start failed" );
	}

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
