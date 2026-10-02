/*
 * @file   drv_entry.c
 * @brief  Registration and up/down/scan wrappers for the rtl8821cu vendor
 *         driver consumed through linuxkpi (S5a W1).
 *
 * Shape change vs the [FWC] ancestor: this port does NOT synthesize a
 * usb_device or call the probe by hand.  wlan8821_register() seeds the
 * prealloc-skb pool and runs rtl8821cu_rtos_init() - the patched export
 * shell around rtw_drv_entry - so usb_register() hands the vendor's own
 * id_table (0bda:c820 family) to the linuxkpi claim loop.  When the dongle
 * ejects and re-enumerates, the vendor probe (rtw_drv_init) runs from the
 * CherryUSB hub thread through lkpi_usb_probe_drivers(), byte-identical to
 * the Linux load path.
 *
 * The device stays DOWN after probe (a registered-but-not-ifconfig'd
 * netdev); `wlan8821 up` drives ndo_open, which pulls efuse -> HALMAC init
 * -> embedded firmware -> chip ready.
 *
 * One adapter per image (the D-1 guard lineage): a second registration is
 * refused.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <drv_types.h>

#include "rtl8821cu_port.h"

/* the [W1-0002] export shells (os_dep/linux/usb_intf.c) and the mem
 * seeding (core/rtw_mem.c; module_init territory on Linux) */
extern int rtl8821cu_rtos_init(void);
extern void rtl8821cu_rtos_deinit(void);
extern int rtl8821cu_mem_init_rtos(void);

/* the single landed adapter */
static _adapter *rtl8821cu_adapter;
static int rtl8821cu_registered;

void *
rtl8821cu_get_adapter(void)
{

	return rtl8821cu_adapter;
}

int
wlan8821_register(void)
{
	int ret;

	if (rtl8821cu_registered != 0) {
		printf("rtl8821cu: refusing second registration "
		       "(one driver per image)\n");
		return -1;
	}
	rtl8821cu_registered = 1;

	/* the RX prealloc pool must exist before any probe can submit */
	ret = rtl8821cu_mem_init_rtos();
	if (ret != 0) {
		printf("rtl8821cu: mem seeding failed (%d)\n", ret);
		return ret;
	}

	ret = rtl8821cu_rtos_init();
	if (ret != 0) {
		printf("rtl8821cu: usb_register failed (%d)\n", ret);
		return ret;
	}
	printf("rtl8821cu: registered (0bda:1a2b ejects into 0bda:c820; "
	       "claim hook attaches)\n");
	return 0;
}

/* ------------------------------------------------------------------ */
/* the up/down/scan hooks the shell commands drive                     */

int
rtl8821cu_up(void)
{
	_adapter *a = rtl8821cu_adapter;
	struct net_device *ndev;

	if (a == NULL) {
		printf("rtl8821cu: not attached\n");
		return -1;
	}
	ndev = a->pnetdev;
	if (ndev == NULL || ndev->netdev_ops == NULL ||
	    ndev->netdev_ops->ndo_open == NULL) {
		return -1;
	}
	return ndev->netdev_ops->ndo_open(ndev);
}

int
rtl8821cu_down(void)
{
	_adapter *a = rtl8821cu_adapter;
	struct net_device *ndev;

	if (a == NULL) {
		return -1;
	}
	ndev = a->pnetdev;
	if (ndev == NULL || ndev->netdev_ops == NULL ||
	    ndev->netdev_ops->ndo_stop == NULL) {
		return -1;
	}
	return ndev->netdev_ops->ndo_stop(ndev);
}

int
rtl8821cu_scan(void)
{
	_adapter *a = rtl8821cu_adapter;

	if (a == NULL) {
		printf("rtl8821cu: not attached\n");
		return -1;
	}
	/* the wext SIOCSIWSCAN path minus the ioctl: site survey through
	 * the vendor's own MLME; completion lands in the indication seam */
	return (rtw_set_802_11_bssid_list_scan(a, NULL, 0, NULL, 0) == _TRUE) ? 0 : -1;
}

int
rtl8821cu_scan_dump(void)
{
	_adapter *a = rtl8821cu_adapter;
	_queue *queue;
	_list *plist, *phead;
	_irqL irqL;
	int n = 0;

	if (a == NULL) {
		printf("rtl8821cu: not attached\n");
		return -1;
	}

	queue = &a->mlmepriv.scanned_queue;
	phead = get_list_head(queue);

	_enter_critical(&queue->lock, &irqL);
	plist = phead->next;
	while (rtw_end_of_queue_search(phead, plist) == _FALSE) {
		struct wlan_network *pnetwork = LIST_CONTAINOR(plist,
		    struct wlan_network, list);

		printf("rtl8821cu: bss[%d] " MAC_FMT " ch=%d rssi=%d ssid=%s\n",
		       n,
		       MAC_ARG(pnetwork->network.MacAddress),
		       (int)pnetwork->network.Configuration.DSConfig,
		       (int)pnetwork->network.Rssi,
		       (pnetwork->network.Ssid.SsidLength != 0u) ?
			(char *)pnetwork->network.Ssid.Ssid : "");
		n++;
		plist = plist->next;
	}
	_exit_critical(&queue->lock, &irqL);

	printf("rtl8821cu: %d bss in scan table\n", n);
	return n;
}
