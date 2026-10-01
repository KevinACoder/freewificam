/*
 * @file   main.c
 * @brief  P5-a entry: cherryusb xHCI single-instance bring-up under
 *         ThreadX (SPEC S5/cherryusb-xhci-port §4/P5-a).
 *
 * ThreadX form of app/hello_world/main.c (the skeleton is the T3/T4 shape
 * proof) with the app threads swapped for this milestone's two:
 *  - shellboot: unchanged (S4) - the `usbh` command set registers into the
 *    same FSymTab and is the acceptance toolbox (list/start/xdump/xreg);
 *  - usbboot: runs usbh_platform_start() in task context AFTER the
 *    scheduler is up - the domain sequence (usb_domain.c) must run in
 *    task context and before any hub thread exists (SPEC §3.2.2), and
 *    usbh_initialize() spawns those hub threads itself. The enumeration
 *    anchors ("usbh: bus2 hc init", "claimed ... (high)", the GSNPSID
 *    line from the glue) come out of usbh_bus_event / usbh_glue via the
 *    stamped console.
 */

#include <stddef.h>
#include <stdint.h>

#ifndef THREADX_BUILD
#error app/cherryusb_enum builds only under CONFIG_OS=threadx
#endif

#include "cmsis_os2.h"
#include "gicv3.h"
#include "log.h"
#include "uart.h"

#ifdef CONFIG_SHELL
#include "cherrysh_adapter.h"
#endif

#include "usbh_platform.h"

#ifdef CONFIG_SHELL
/*
 * The shell boot task: verbatim from app/hello_world/main.c (S4 §3.3.2
 * 启动时机 - cherrysh_init() must run from task context after the
 * scheduler is up; its last act arms the RX interrupt).
 */
static void thr_shellboot(void *arg)
{
	(void)arg;

	if (cherrysh_init() == 0) {
		board_log("shell: READY\n");
	} else {
		board_log("shell: FAIL\n");
	}
	for (;;) {
		(void)osDelay(0x7fffffffU);
	}
}
#endif

/*
 * The USB bring-up task: usbh_platform_start() owns the OSAL init, the
 * shared domain sequences and the (single-instance, P5-a) bus start. A
 * short delay first so the shell prompt is on the wire before the
 * enumeration chatter starts - the console is one line assembler, the
 * acceptance transcript reads better for it.
 */
static void thr_usbboot(void *arg)
{
	(void)arg;

	(void)osDelay(500U);
	board_log("usb: platform start\n");
	if (usbh_platform_start() == 0) {
		board_log("usb: platform READY\n");
	} else {
		board_log("usb: platform FAIL\n");
	}
	for (;;) {
		(void)osDelay(0x7fffffffU);
	}
}

int main(void)
{
	uart_early_init();
	gicv3_init();

	(void)osKernelInitialize();

	(void)osThreadNew(thr_usbboot, NULL,
			  &(osThreadAttr_t){ .name = "usbboot",
					     .priority = osPriorityNormal });

#ifdef CONFIG_SHELL
	(void)osThreadNew(thr_shellboot, NULL,
			  &(osThreadAttr_t){ .name = "shellboot",
					     .priority = osPriorityNormal });
#endif

	board_log("usb: threads created, starting kernel\n");
	(void)osKernelStart();
	return 0;	/* unreachable */
}
