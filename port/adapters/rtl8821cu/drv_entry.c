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

/* the vendor's power-save knobs (include/rtw_pwrctrl.h: PS_MODE_ACTIVE /
 * IPS_NONE).  KI-027: the board's Linux baseline only reaches usable TCP
 * throughput with rtw_power_mgnt=0 / rtw_lps_level=0 / rtw_ips_mode=0 -
 * default LPS/IPS leaves the stream in sub-Mbit bursts; the RTOS build
 * carries CONFIG_POWER_SAVING (PS_MODE_MAX + IPS_NORMAL defaults), so the
 * same two requests are issued right after ndo_open. */
extern int rtw_pm_set_lps(_adapter *padapter, u8 mode);
extern int rtw_pm_set_ips(_adapter *padapter, u8 mode);

/* the [W1-0002] export shells (os_dep/linux/usb_intf.c) and the mem
 * seeding (core/rtw_mem.c; module_init territory on Linux) */
extern int rtl8821cu_rtos_init(void);
extern void rtl8821cu_rtos_deinit(void);
extern int rtl8821cu_mem_init_rtos(void);

/* the single landed adapter */
static _adapter *rtl8821cu_adapter;
static int rtl8821cu_registered;

void
rtl8821cu_adapter_bind(struct usb_interface *intf)
{
	struct dvobj_priv *dvobj;

	/* [FWC] drv_entry.c:82-83 shape: usb_dvobj_init left the dvobj on
	 * the interface (usb_set_intfdata); the primary adapter hangs off
	 * it.  Called by the wlan class hook after probe success. */
	dvobj = (struct dvobj_priv *)usb_get_intfdata(intf);
	rtl8821cu_adapter = (dvobj != NULL) ? dvobj_get_primary_adapter(dvobj) : NULL;
	printf("rtl8821cu: attached (adapter %p)\n",
	       (void *)rtl8821cu_adapter);
#ifdef CONFIG_NIC_WPA_BACKEND
	/* the supplicant's backend rides the attach: ops table registered
	 * before any `wpa start` can run, and the netif_rx EAPOL sink
	 * mounted ahead of the first RX frame */
	rtl8821cu_wpa_backend_init();
#endif
}

void
rtl8821cu_adapter_unbind(void)
{

	rtl8821cu_adapter = NULL;
}

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
	printf("rtl8821cu: registered (0bda:1a2b ejects into 0bda:c820|b851; "
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
	int r;

	if (a == NULL) {
		printf("rtl8821cu: not attached\n");
		return -1;
	}
	ndev = a->pnetdev;
	if (ndev == NULL || ndev->netdev_ops == NULL ||
	    ndev->netdev_ops->ndo_open == NULL) {
		return -1;
	}
	r = ndev->netdev_ops->ndo_open(ndev);
	if (r == 0) {
		/* KI-027 power-save off (see the externs above): LPS idle
		 * 2s + IPS re-arms cost ~0.6s per wake and collapse TCP
		 * into sub-Mbit bursts (board 2026-10-03 iperf: 32M/0.5M
		 * alternating seconds). */
		(void)rtw_pm_set_lps(a, 0 /* PS_MODE_ACTIVE */);
		(void)rtw_pm_set_ips(a, 0 /* IPS_NONE */);
		printf("rtl8821cu: lps off, ips off (KI-027)\n");
	}
	return r;
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
	u8 status;
	int ret;

	if (a == NULL) {
		printf("rtl8821cu: not attached\n");
		return -1;
	}
	/* the wext SIOCSIWSCAN path minus the ioctl (ioctl_linux.c:2428-2589):
	 * site survey through the vendor's own MLME, but FIRST the power-save
	 * dance the ioctl wrapper does around it - an unassociated up'd port
	 * enters no-linked IPS ~2s after open (ips_enter -> hal_deinit), and
	 * rtw_set_802_11_bssid_list_scan refuses on hw_init_completed alone.
	 * rtw_pwr_wakeup leaves IPS (chip re-init + fw re-download) and the
	 * deny gate keeps the processor from re-entering while the survey is
	 * issued; completion lands in the indication seam. */
	rtw_ps_deny(a, PS_DENY_SCAN);
	if (_FAIL == rtw_pwr_wakeup(a)) {
		rtw_ps_deny_cancel(a, PS_DENY_SCAN);
		printf("rtl8821cu: scan wakeup failed\n");
		return -1;
	}
	status = rtw_set_802_11_bssid_list_scan(a, NULL, 0, NULL, 0);
	ret = (status == _TRUE) ? 0 : -1;
	rtw_ps_deny_cancel(a, PS_DENY_SCAN);
	return ret;
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
