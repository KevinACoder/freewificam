/*
 * rtw88_entry.c - the rtw88 line's registration and status face
 * (S9/rtw88-adapt W2).
 *
 * rtw88 registers through module_usb_driver(), which on this tree drops
 * the driver pointer into the (unwalked) .lkpi_module section; the app
 * main calls rtw88_rtos_register() BEFORE usbh_platform_start() so the
 * id_table is live when the hub thread's first claim walk runs (the
 * same discipline as lkpi_udisk_rtos_register). The firmware blob joins
 * the registry in the same call: rtw88 is the fw registry's first real
 * consumer.
 */

#include <stdint.h>

#include "linuxkpi_core.h"
#include "linuxkpi_usb.h"
#include <linux/usb.h>

/* rtw88's RX pump parks one self-rearming bulk IN urb per rx endpoint
 * until data arrives - the same single-urb contract the int exemption
 * carries (P6-b async vetoes were BOT multi-urb patterns, not this
 * shape). The wlan line therefore drops the global sync-only discipline
 * BEFORE the first submit; the done-list + drain delivery is the
 * [EXPL] prior line's board-proven shape. */
extern void lkpi_usb_force_sync_set(bool on);

/* components/rtw88/rtw8821cu.c (DIVERGENCES D-1: static removed) */
extern struct usb_driver rtw_8821cu_driver;
/* components/rtw88/fw_rtw8821c.c */
extern void fw_rtw8821c_register(void);

int
rtw88_rtos_register(void)
{

	fw_rtw8821c_register();
	lkpi_usb_force_sync_set(false);
	return usb_register(&rtw_8821cu_driver);
}
