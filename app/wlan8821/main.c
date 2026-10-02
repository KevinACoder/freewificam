/*
 * @file   main.c
 * @brief  S5a W1 entry: the wlan8821 bring-up main under ThreadX
 *         (specs/rtl8821cu-vendor-driver/SPEC.md v2 §4/W1).
 *
 * ThreadX form of app/cherryusb_enum/main.c with the acceptance-driver
 * registrations swapped for the rtl8821cu one: wlan8821_register() seeds
 * the prealloc-skb pool and runs usb_register (the [W1-0002] export
 * shell) BEFORE usbh_platform_start() - the hub thread claims through
 * the class hook the moment a device shows up.  The modeswitch hook
 * (port/adapters/rtl8821cu/usbh_modeswitch.c) rides the same class-info
 * section and ejects the 0bda:1a2b fake CD-ROM on sight; the WiFi
 * personality (0bda:c820) joins the claim path in W2.
 *
 * The shell (`wlan8821 up|down|scan|bss`) is the acceptance toolbox, the
 * same discipline as cherryusb_enum's `usbh` set.
 */

#include <stddef.h>
#include <stdint.h>

#ifndef THREADX_BUILD
#error app/wlan8821 builds only under CONFIG_OS=threadx
#endif

#include "cmsis_os2.h"
#include "gicv3.h"
#include "log.h"
#include "uart.h"

#ifdef CONFIG_SHELL
#include "cherrysh_adapter.h"
#endif

#include "usbh_platform.h"

#ifdef CONFIG_NIC_RTL8821CU
/* port/adapters/rtl8821cu (the adapter interface, no vendor headers) */
extern int wlan8821_register(void);
#endif

#ifdef CONFIG_SHELL
/*
 * The shell boot task: verbatim from app/cherryusb_enum/main.c (S4 §3.3.2
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
 * The USB bring-up task: register the wlan driver first (usb_register
 * must precede the hub thread's first claim walk), then hand the domain
 * sequence to usbh_platform_start().
 */
static void thr_usbboot(void *arg)
{
	(void)arg;

	(void)osDelay(500U);
	board_log("usb: platform start\n");
#ifdef CONFIG_NIC_RTL8821CU
	if (wlan8821_register() != 0) {
		board_log("wlan8821: driver register FAIL\n");
	}
#endif
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
