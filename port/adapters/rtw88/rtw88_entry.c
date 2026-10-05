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
#ifdef CONFIG_RTW88_WPA_BACKEND
/* port/adapters/rtw88/wpa_vendor_rtw88.c: the supplicant's vendor slot
 * must be registered before any `wpa` shell command runs - boot time is
 * the safe point (the WPA world comes up with the shell) */
extern void rtw88_wpa_backend_init(void);
#endif
#ifdef CONFIG_WLAN_NETIF
/* port/adapters/rtw88/wlanrtw_netif.c */
extern void wlanrtw_netif_bind(void);
#endif

#ifndef CONFIG_NET80211
/*
 * W1/W2 compile-face images (no net80211 world, see the config notes):
 * the shell TU calls the bridge API unconditionally, so weak no-op
 * defaults hold the position exactly like the mac80211-side weak
 * lkpi_n80211_* pair does.  A W3+ image links the real bridge and these
 * are overridden.  (Restored in R4: the W3 merge made rtw88-build fail
 * to link - the "弱符号默认顶位" the W1 note promised was never added.)
 * Prototypes spelled out (no net80211 include on this TU's flag set).
 */
#include <stddef.h>

__attribute__((weak)) int net80211_bridge_up(void) { return -1; }
__attribute__((weak)) int net80211_bridge_scan(const uint8_t *ssid, size_t len)
{ (void)ssid; (void)len; return -1; }
__attribute__((weak)) int net80211_bridge_connect(const uint8_t *ssid, size_t len)
{ (void)ssid; (void)len; return -1; }
__attribute__((weak)) int net80211_bridge_get_hwaddr(uint8_t addr[6])
{ (void)addr; return -1; }
__attribute__((weak)) void net80211_bridge_set_mac(const uint8_t addr[6])
{ (void)addr; }
__attribute__((weak)) void net80211_bridge_status(void) { }
__attribute__((weak)) void net80211_bridge_scan_dump(void) { }
#endif

int
rtw88_rtos_register(void)
{

	fw_rtw8821c_register();
#ifdef CONFIG_RTW88_WPA_BACKEND
	rtw88_wpa_backend_init();
#endif
#ifdef CONFIG_WLAN_NETIF
	wlanrtw_netif_bind();
#endif
	lkpi_usb_force_sync_set(false);
	return usb_register(&rtw_8821cu_driver);
}
