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

#ifdef CONFIG_WLAN_NETIF
/* port/adapters/rtl8851bu/wlan8851_netif.c */
extern void wlan8851_wlan_netif_bind(void);
extern void wlan8851_wlan_netif_stats(unsigned long *rx_fed,
    unsigned long *rx_drop, unsigned long *tx_frames, unsigned long *tx_err);
extern unsigned long wlan8851_wlan_netif_last_rx_ms(void);
#endif

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
#ifdef CONFIG_NIC_WPA_BACKEND
	/* [S5b W4] the supplicant's backend rides the attach: ops table
	 * registered before any `wpa start` can run, and the netif_rx
	 * EAPOL sink mounted ahead of the first RX frame.  The wpa sink
	 * composes the lwip bridge (non-EAPOL falls through), so the
	 * bridge must NOT register a second sink (single-sink seam). */
	rtl8851bu_wpa_backend_init();
#elif defined(CONFIG_WLAN_NETIF)
	/* [S5b W4] netif-only image: the lwip bridge owns the RX sink */
	wlan8851_wlan_netif_bind();
#endif
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

/* ------------------------------------------------------------------ */
/* [S5b W4] connect / link face                                        */
/* ------------------------------------------------------------------ */

/*
 * The cfg80211_rtw_connect sequence (ioctl_cfg80211.c:4416+, authoritative
 * reference, not a linked unit) minus the pieces this lane has no home
 * for: the wdev block/atomic checks, the disconnect-before-connect, and
 * anything that needs the supplicant.  The supplicant's RSN/WPA IE is a
 * parameter (the same stash the sibling line's wrapper carries into
 * securitypriv.supplicant_ie, which rtw_restruct_sec_ie (:4502) copies
 * into the assoc request).
 *
 *   PS deny/wakeup are absent from the V5 tree by construction
 *   (CONFIG_POWER_SAVE=n; the W0-B Linux ground truth ran the same way -
 *   no IPS/LPS to defuse), so the sequence starts at scan-abort.
 */
int
rtl8851bu_connect(const u8 *bssid, const char *ssid, int ssid_len,
		  const u8 *rsn_ie, int rsn_ie_len)
{
	_adapter *a = rtl8851bu_adapter;
	NDIS_802_11_SSID ndis_ssid;
	u8 status;

	if (a == NULL) {
		printf("rtl8851bu: not attached\n");
		return -1;
	}
	if (ssid == NULL || ssid_len <= 0 ||
	    ssid_len > (int)sizeof(ndis_ssid.Ssid)) {
		printf("rtl8851bu: connect bad ssid args\n");
		return -1;
	}

	/* the cfg80211_rtw_connect preamble: an ongoing survey joins when it
	 * ends (to_join), but the scan must be abortable first */
	rtw_mi_scan_abort(a, _TRUE);
	rtw_join_abort_timeout(a, 300);

	memset(&ndis_ssid, 0, sizeof(ndis_ssid));
	ndis_ssid.SsidLength = (u32)ssid_len;
	memcpy(ndis_ssid.Ssid, ssid, (size_t)ssid_len);

	/* security state: the supplicant IE stash + auth/privacy (the
	 * cfg80211 ternary this lane pins: RSN present => WPA2-PSK/CCMP,
	 * absent => open) */
	{
		struct security_priv *sec = &a->securitypriv;

		memset(sec->supplicant_ie, 0, sizeof(sec->supplicant_ie));
		if (rsn_ie != NULL && rsn_ie_len > 0) {
			int n = rsn_ie_len;

			if (n > (int)sizeof(sec->supplicant_ie))
				n = (int)sizeof(sec->supplicant_ie);
			memcpy(sec->supplicant_ie, rsn_ie, (size_t)n);
			sec->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
			sec->ndisauthtype = Ndis802_11AuthModeWPA2PSK;
			sec->dot11PrivacyAlgrthm = _AES_;
			sec->dot118021XGrpPrivacy = _AES_;
			sec->ndisencryptstatus = Ndis802_11Encryption3Enabled;
		} else {
			sec->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
			sec->ndisauthtype = Ndis802_11AuthModeOpen;
			sec->dot11PrivacyAlgrthm = _NO_PRIVACY_;
			sec->dot118021XGrpPrivacy = _NO_PRIVACY_;
			sec->ndisencryptstatus = Ndis802_11EncryptionDisabled;
		}
		rtw_set_802_11_authentication_mode(a,
		    (NDIS_802_11_AUTHENTICATION_MODE)sec->ndisauthtype);
	}

	status = rtw_set_802_11_connect(a,
	    (bssid != NULL) ? bssid : NULL, &ndis_ssid, 0, BAND_MAX);

	printf("rtl8851bu: connect \"%s\" (%s) -> %d\n", ssid,
	       (rsn_ie != NULL && rsn_ie_len > 0) ? "psk/ccmp" : "open",
	       (int)status);
	return (status == _TRUE) ? 0 : -1;
}

int
rtl8851bu_disconnect(void)
{
	_adapter *a = rtl8851bu_adapter;

	if (a == NULL)
		return -1;
	if (rtw_set_802_11_disassociate(a) != _TRUE)
		return -1;
	printf("rtl8851bu: disconnect issued\n");
	return 0;
}

/* one-line link state: fw_state + the joined BSSID/SSID when linked */
int
rtl8851bu_link_status(void)
{
	_adapter *a = rtl8851bu_adapter;
	struct mlme_priv *pmlmepriv;
	struct wlan_network *cur;
	unsigned long st;
	int linked;

	if (a == NULL) {
		printf("rtl8851bu: not attached\n");
		return -1;
	}
	pmlmepriv = &a->mlmepriv;
	st = (unsigned long)get_fwstate(pmlmepriv);
	linked = (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE);
	cur = &pmlmepriv->dev_cur_network;
	printf("rtl8851bu: link %s fw_state=0x%lx bssid=" MAC_FMT "\n",
	       linked ? "UP" : "down", st,
	       MAC_ARG(cur->network.MacAddress));
	printf("rtl8851bu: cur ssid=%s infr=%d\n",
	       (cur->network.Ssid.SsidLength != 0u) ?
		(char *)cur->network.Ssid.Ssid : "",
	       (int)cur->network.InfrastructureMode);
#ifdef CONFIG_WLAN_NETIF
	{
		unsigned long rx_fed = 0, rx_drop = 0, tx = 0, tx_err = 0;

		wlan8851_wlan_netif_stats(&rx_fed, &rx_drop, &tx, &tx_err);
		printf("rtl8851bu: netif rx_fed=%lu rx_drop=%lu tx=%lu tx_err=%lu last_rx_ms=%lu\n",
		       rx_fed, rx_drop, tx, tx_err,
		       wlan8851_wlan_netif_last_rx_ms());
	}
#endif
	return linked ? 0 : -1;
}
