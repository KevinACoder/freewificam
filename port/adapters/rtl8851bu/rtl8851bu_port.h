/*
 * @file   rtl8851bu_port.h
 * @brief  The first-party adapter interface for the rtl8851bu vendor line
 *         (S5b W1, specs/rtl8851bu-vendor-driver/SPEC.md v3.2 §4/W1).
 *
 * The vendor tree sits behind this header: app/ sees only these
 * declarations (S5b W1 judgement 5 - no drv_types.h above the adapter
 * layer).  The attach path is NOT here - the driver registers through
 * usb_register (wlan8851_register) and the linuxkpi claim hook runs the
 * vendor probe when the dongle (0bda:b851 - the stick enumerates directly,
 * no modeswitch, W0-B board fact) shows up.  The W4 wpa backend and W5
 * lwip netif declarations join when those rounds land (接口宁少勿多).
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef RTL8851BU_PORT_H
#define RTL8851BU_PORT_H

#include <stdint.h>

/* driver registration: rtl8851bu_rtos_init() (the patched export shell -
 * usb_register happens inside, exactly like the Linux module load).  Call
 * once from the app main, before usbh_platform_start().  (The [S5a] W1
 * prealloc-skb mem seeding has no V5 counterpart - the PHL tree carries no
 * core/rtw_mem.c pool; the vendor allocates through its own rtw_mem.h
 * wrappers directly.) */
int wlan8851_register(void);

/* the single landed adapter (NULL until the vendor probe ran) */
void *rtl8851bu_get_adapter(void);

/* probe-success binding: the wlan class hook hands the claimed interface
 * over; the primary adapter is read from the interface's intfdata */
struct usb_interface;
void rtl8851bu_adapter_bind(struct usb_interface *intf);
void rtl8851bu_adapter_unbind(void);

/* up/down/scan hooks the shell commands drive (ndo_open/ndo_stop/the
 * vendor PHL site survey) */
int rtl8851bu_up(void);
int rtl8851bu_down(void);
int rtl8851bu_scan(void);
int rtl8851bu_scan_dump(void);

#endif /* RTL8851BU_PORT_H */
