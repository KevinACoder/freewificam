/*
 * @file   rtl8821cu_port.h
 * @brief  The first-party adapter interface for the rtl8821cu vendor line
 *         (S5a W1, specs/rtl8821cu-vendor-driver/SPEC.md v2 §3.2.3).
 *
 * The vendor tree sits behind this header: app/ sees only these
 * declarations (S5a W1 judgement 5 - no drv_types.h above the adapter
 * layer).  The attach path is NOT here - the driver registers through
 * usb_register (wlan8821_register) and the linuxkpi claim hook runs the
 * vendor probe when the ejected dongle (0bda:c820) re-enumerates; the
 * class hook that matches the WiFi personality joins in W2.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef RTL8821CU_PORT_H
#define RTL8821CU_PORT_H

#include <stdint.h>

/* driver registration: prealloc-skb pool seeding + rtl8821cu_rtos_init()
 * (the patched export shell - usb_register happens inside, exactly like
 * the Linux module load).  Call once from the app main, before
 * usbh_platform_start(). */
int wlan8821_register(void);

/* the single landed adapter (NULL until the vendor probe ran) */
void *rtl8821cu_get_adapter(void);

/* probe-success binding: the wlan class hook hands the claimed interface
 * over; the primary adapter is read from the interface's intfdata */
struct usb_interface;
void rtl8821cu_adapter_bind(struct usb_interface *intf);
void rtl8821cu_adapter_unbind(void);

/* up/down/scan hooks the shell commands drive (ndo_open/ndo_stop/the
 * vendor MLME site survey) */
int rtl8821cu_up(void);
int rtl8821cu_down(void);
int rtl8821cu_scan(void);
int rtl8821cu_scan_dump(void);

/* data-plane counters (netif_rx sink accounting) */
unsigned long rtl8821cu_rx_stats(unsigned long *bytes);

/* W4 (S5a A-case, CONFIG_NIC_WPA_BACKEND builds): the wpa vendor
 * backend. The seam's indication hooks and the netif_rx EAPOL filter
 * pump events into the supplicant through here. */
void rtl8821cu_wpa_backend_init(void);
void rtl8821cu_wpa_event(int event, const unsigned char *addr,
			 const unsigned char *buf, unsigned long len);
unsigned long rtl8821cu_wpa_rx_counters(unsigned long *eapol,
					unsigned long *data_drop);

/* W5 (S5a data plane, CONFIG_WLAN_NETIF builds): the lwIP netif bridge
 * (wlan8821_netif.c).  ASSOC/DISASSOC drive netif up/down + DHCP via
 * tcpip_callback; the RX sink hands non-EAPOL frames to _input. */
void rtl8821cu_wlan_netif_on_assoc(void);
void rtl8821cu_wlan_netif_on_disassoc(void);
struct sk_buff;
int rtl8821cu_wlan_netif_input(struct sk_buff *skb);
void rtl8821cu_wlan_netif_stats(unsigned long *rx_fed, unsigned long *rx_drop,
				unsigned long *tx_frames, unsigned long *tx_err);
unsigned long rtl8821cu_wlan_netif_last_rx_ms(void);

#endif /* RTL8821CU_PORT_H */
