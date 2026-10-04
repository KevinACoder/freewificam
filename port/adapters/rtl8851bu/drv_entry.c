/*
 * @file   drv_entry.c
 * @brief  Registration and up/down/scan wrappers for the rtl8851bu vendor
 *         driver consumed through linuxkpi (S5b W1).
 *
 * Shape change vs the rtl8821cu sibling (S5a): no mem-seeding step (the PHL
 * tree has no core/rtw_mem.c prealloc pool) and no KI-027 LPS/IPS toggles
 * (the V5 power-save world is PHL-side phl_ps, compiled out by
 * CONFIG_POWER_SAVE=n - the same config the W0-B Linux ground truth ran;
 * there is nothing left to disable at this tier).  wlan8851_register()
 * runs rtl8851bu_rtos_init() - the patched export shell around
 * rtw_drv_entry - so usb_register() hands the vendor's own id_table
 * (0bda:b851) to the linuxkpi claim loop; the probe (rtw_drv_init) runs
 * from the CherryUSB hub thread through lkpi_usb_probe_drivers(),
 * byte-identical to the Linux load path.
 *
 * The device stays DOWN after probe (a registered-but-not-ifconfig'd
 * netdev); `wlan8851 up` drives ndo_open, which pulls efuse -> PHL init
 * -> embedded firmware -> chip ready.
 *
 * One adapter per image (the D-1 guard lineage): a second registration is
 * refused.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <drv_types.h>

#include "rtl8851bu_port.h"

/* the [W1-0002] export shells (os_dep/linux/usb_intf.c) */
extern int rtl8851bu_rtos_init(void);
extern void rtl8851bu_rtos_deinit(void);

/* the single landed adapter */
static _adapter *rtl8851bu_adapter;
static int rtl8851bu_registered;

void
rtl8851bu_adapter_bind(struct usb_interface *intf)
{
	struct dvobj_priv *dvobj;

	/* the sibling shape: usb_dvobj_init left the dvobj on the interface
	 * (usb_set_intfdata); the primary adapter hangs off it.  Called by
	 * the wlan class hook after probe success. */
	dvobj = (struct dvobj_priv *)usb_get_intfdata(intf);
	rtl8851bu_adapter = (dvobj != NULL) ? dvobj_get_primary_adapter(dvobj) : NULL;
	printf("rtl8851bu: attached (adapter %p)\n", (void *)rtl8851bu_adapter);
}

void
rtl8851bu_adapter_unbind(void)
{

	rtl8851bu_adapter = NULL;
}

void *
rtl8851bu_get_adapter(void)
{

	return rtl8851bu_adapter;
}

int
wlan8851_register(void)
{
	int ret;

	if (rtl8851bu_registered != 0) {
		printf("rtl8851bu: refusing second registration "
		       "(one driver per image)\n");
		return -1;
	}
	rtl8851bu_registered = 1;

	ret = rtl8851bu_rtos_init();
	if (ret != 0) {
		printf("rtl8851bu: usb_register failed (%d)\n", ret);
		return ret;
	}
	printf("rtl8851bu: registered (0bda:b851 direct enumeration, "
	       "no modeswitch; claim hook attaches)\n");
	return 0;
}

/* ------------------------------------------------------------------ */
/* the up/down/scan hooks the shell commands drive                     */

int
rtl8851bu_up(void)
{
	_adapter *a = rtl8851bu_adapter;
	struct net_device *ndev;

	if (a == NULL) {
		printf("rtl8851bu: not attached\n");
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
rtl8851bu_down(void)
{
	_adapter *a = rtl8851bu_adapter;
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
rtl8851bu_scan(void)
{
	_adapter *a = rtl8851bu_adapter;
	struct sitesurvey_parm *parm;
	u8 ssc_chk;
	int ret = -1;

	if (a == NULL) {
		printf("rtl8851bu: not attached\n");
		return -1;
	}
	/* the wext SIOCSIWSCAN path minus the ioctl (ioctl_linux.c:1859+,
	 * not in the RTOS compile set): the condition gate, then a full
	 * all-channel survey through the vendor's own cmd dispatcher.
	 * rtw_sitesurvey_cmd completes the survey synchronously (the cmd
	 * waits for the PHL scan done); the scan-complete indication fires
	 * through the seam on the way. */
	ssc_chk = rtw_sitesurvey_condition_check(a, _FALSE);
	if (ssc_chk != SS_ALLOW) {
		printf("rtl8851bu: scan refused (ssc_chk=%d)\n", (int)ssc_chk);
		return -1;
	}
	parm = rtw_malloc(sizeof(*parm));
	if (parm == NULL) {
		printf("rtl8851bu: scan parm alloc failed\n");
		return -1;
	}
	rtw_init_sitesurvey_parm(a, parm);
	/* ssid_num stays 0: full survey (the SIOCSIWSCAN default) */
	if (rtw_sitesurvey_cmd(a, parm) == _SUCCESS)
		ret = 0;
	rtw_mfree((u8 *)parm, sizeof(*parm));
	return ret;
}

int
rtl8851bu_scan_dump(void)
{
	_adapter *a = rtl8851bu_adapter;
	_queue *queue;
	_list *plist, *phead;
	int n = 0;

	if (a == NULL) {
		printf("rtl8851bu: not attached\n");
		return -1;
	}

	queue = &a->mlmepriv.scanned_queue;
	phead = get_list_head(queue);

	/* the V5 idiom (core/rtw_ioctl_set.c rtw_do_join): bh spinlock pair,
	 * no _irqL face in the PHL osdep */
	_rtw_spinlock_bh(&queue->lock);
	plist = phead->next;
	while (rtw_end_of_queue_search(phead, plist) == _FALSE) {
		struct wlan_network *pnetwork = LIST_CONTAINOR(plist,
		    struct wlan_network, list);

		printf("rtl8851bu: bss[%d] " MAC_FMT " ch=%d rssi=%d ssid=%s\n",
		       n,
		       MAC_ARG(pnetwork->network.MacAddress),
		       (int)pnetwork->network.Configuration.DSConfig,
		       (int)pnetwork->network.PhyInfo.SignalStrength,
		       (pnetwork->network.Ssid.SsidLength != 0u) ?
			(char *)pnetwork->network.Ssid.Ssid : "");
		n++;
		plist = plist->next;
	}
	_rtw_spinunlock_bh(&queue->lock);

	printf("rtl8851bu: %d bss in scan table\n", n);
	return n;
}
