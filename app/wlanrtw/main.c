/*
 * @file   main.c
 * @brief  S9/W2 entry: rtw88 board bring-up under ThreadX
 *         (specs/rtw88-adapt/SPEC.md §4/W2).
 *
 * app/cherryusb_enum shape (P5-a) with the acceptance-driver
 * registrations swapped for the rtw88 line: modeswitch (1a2b eject) +
 * wlan class hook (c820 claim) live in the .usbh_class_info section,
 * the usb_driver id_table registers through rtw88_rtos_register()
 * BEFORE usbh_platform_start() - the hub thread claims the moment the
 * dongle re-appears after eject.
 *
 * W2 anchor chain (spec W2 判据 1-3):
 *   usb: RTL8821CU fake CD-ROM ... ejecting      (modeswitch)
 *   usbh: rtw88u claiming 0bda:c820              (class hook)
 *   usbkpi: usb driver 'rtw88_8821cu' registered (early, at boot)
 *   rtw88 fw + register_hw + ops->start lines    (driver bring-up)
 */

#include <stddef.h>
#include <stdint.h>

#ifndef THREADX_BUILD
#error app/wlanrtw builds only under CONFIG_OS=threadx
#endif

#include "cmsis_os2.h"
#include "gicv3.h"
#include "log.h"
#include "uart.h"

#ifdef CONFIG_SHELL
#include "cherrysh_adapter.h"
#endif

#include "usbh_platform.h"

extern int rtw88_rtos_register(void);

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
 * The USB bring-up task: register the rtw88 driver (fw blob + id_table)
 * first, then start the platform - enumeration begins the moment the
 * hub thread is up and the dongle's 1a2b personality hits the
 * modeswitch hook.
 */
static void thr_usbboot(void *arg)
{
	(void)arg;

	(void)osDelay(500U);
	board_log("usb: platform start\n");
	if (rtw88_rtos_register() != 0) {
		board_log("rtw88: driver register FAIL\n");
	}
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
			       .stack_size = 0x4000,
			       .priority = osPriorityNormal });
#ifdef CONFIG_SHELL
	(void)osThreadNew(thr_shellboot, NULL,
	    &(osThreadAttr_t){ .name = "shellboot",
			       .stack_size = 0x2000,
			       .priority = osPriorityNormal });
#endif

	board_log("usb: threads created, starting kernel\n");
	(void)osKernelStart();
	return 0;	/* unreachable */
}
