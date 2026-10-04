/*
 * @file   main.c
 * @brief  S5b W1 entry: the wlan8851 bring-up main under ThreadX
 *         (specs/rtl8851bu-vendor-driver/SPEC.md v3.2 §4/W1).
 *
 * ThreadX form of app/cherryusb_enum/main.c with the acceptance-driver
 * registrations swapped for the rtl8851bu one: wlan8851_register() runs
 * usb_register (the [W1-0002] export shell) BEFORE usbh_platform_start()
 * - the hub thread claims through the class hook the moment a device
 * shows up.  V5 shape: no modeswitch tier - the dongle enumerates
 * directly as 0bda:b851 (W0-B board fact) and the wlan class hook is the
 * only consumer of that persona; the composite card's BT interfaces
 * bounce off the class guard in the hook.
 *
 * The shell (`wlan8851 up|down|scan|bss`) is the acceptance toolbox, the
 * same discipline as cherryusb_enum's `usbh` set.
 */

#include <stddef.h>
#include <stdint.h>

#ifndef THREADX_BUILD
#error app/wlan8851 builds only under CONFIG_OS=threadx
#endif

#include "cmsis_os2.h"
#include "gicv3.h"
#include "log.h"
#include "uart.h"

#ifdef CONFIG_SHELL
#include "cherrysh_adapter.h"
#endif

#include "usbh_platform.h"

#ifdef CONFIG_NIC_RTL8851BU
/* port/adapters/rtl8851bu (the adapter interface, no vendor headers) */
extern int wlan8851_register(void);
#endif

#ifdef CONFIG_LWIP
#include "lwip/init.h"
#include "lwip/tcpip.h"
#endif

#ifndef CONFIG_CPU_BOOST_MHZ
#define CONFIG_CPU_BOOST_MHZ 0
#endif
/* N7 R5a board-proven chain: raises the cluster off the bootloader's
 * 816 MHz (voltage over i2c0 first, then APLL relock); probe-only at 0.
 * W6: the iperf data plane needs the headroom - at 816 MHz the vendor's
 * DYNAMIC_CHK command alone costs 1.5-3.1 s per pass under load. */
extern int rk3568_cpu_boost(unsigned int mhz);

#ifdef CONFIG_SHELL
/*
 * The shell boot task: verbatim from app/cherryusb_enum/main.c (S4 §3.3.2
 * 启动时机 - cherrysh_init() must run from task context after the
 * scheduler is up; its last act arms the RX interrupt).
 */
static void thr_shellboot(void *arg)
{
	(void)arg;

	board_log("shell: init enter");
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
#ifdef CONFIG_LWIP
	/* W5: the wlan netif bridge lives in the tcpip thread (netif_add /
	 * dhcp via tcpip_callback from the ASSOC indication, much later).
	 * Init before the driver so the first indication can never land on
	 * an uninitialized stack. */
	tcpip_init(NULL, NULL);
	board_log("lwip: v%s init NO_SYS=%d (wlan netif bridge armed)\n",
		  LWIP_VERSION_STRING, (int)NO_SYS);
#endif
	board_log("usb: platform start\n");
#ifdef CONFIG_NIC_RTL8851BU
	if (wlan8851_register() != 0) {
		board_log("wlan8851: driver register FAIL\n");
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

	/* W6: CPU boost before any thread exists (the lwip_lan-proven slot) */
	(void)rk3568_cpu_boost(CONFIG_CPU_BOOST_MHZ);

	(void)osKernelInitialize();

	{
		osThreadId_t tu = osThreadNew(thr_usbboot, NULL,
		    &(osThreadAttr_t){ .name = "usbboot",
				       .priority = osPriorityNormal });
		osThreadId_t ts = NULL;

#ifdef CONFIG_SHELL
		ts = osThreadNew(thr_shellboot, NULL,
		    &(osThreadAttr_t){ .name = "shellboot",
				       .priority = osPriorityNormal });
#endif
		board_log("usb: threads created usbboot=%p shellboot=%p\n",
		    (void *)tu, (void *)ts);
	}
	(void)osKernelStart();
	return 0;	/* unreachable */
}
