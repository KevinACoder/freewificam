/*
 * @file   wpa_vendor_rtl8851bu.c
 * @brief  The wpa_vendor_port backend for the rtl8851bu vendor line
 *         (S5b-W4, [S7a] FR-8 contract instantiated on feat/wlan-8851bu*r3).
 *
 * The sibling shape (wpa_vendor_rtl8821cu.c, S5a) with the V5 key path:
 * the 8821 tree's rtw_set_802_11_add_key does not exist in the V5 (PHL)
 * tree - the authoritative reference is cfg80211_rtw_add_key ->
 * rtw_cfg80211_set_encryption (os_dep/linux/ioctl_cfg80211.c, NOT a
 * linked unit), which programs:
 *
 *   pairwise CCMP: psta->dot118021x_UncstKey + PN seed (dot11txpn/rxpn
 *                  + per-TID rxcache IV) -> rtw_setstakey_cmd(UNICAST_KEY)
 *   group CCMP:    link securitypriv dot118021XGrpKey[idx] + iv_seq ->
 *                  rtw_set_key(link, idx, set_tx=1)
 *   both:          clear psta/pbcmc ieee8021x_blocked ("don't disable
 *                  while clearing key" - only for real key installs)
 *
 * Everything else mirrors the sibling verbatim: connect rides the
 * drv_entry wrapper (rtl8851bu_connect: scan abort + security stash +
 * rtw_set_802_11_connect), the EAPOL carrier rides ndo_start_xmit, and
 * the single netif_rx sink routes EAPOL to the supplicant pump and the
 * rest into the lwip bridge (CONFIG_WLAN_NETIF).
 *
 * Context rules ([S7a] FR-7): the event pump runs in driver/USB worker
 * contexts and only queues; ensure_up may block on USB workers and is
 * only called from the shell thread.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <drv_types.h>

#include "rtl8851bu_port.h"
#include "wpa_vendor_port.h"

/* drv_entry.c (rtl8851bu_up via the port header) */
extern int rtl8851bu_up(void);

/* the registered event pump target (skeleton init2 hands it over) */
static void (*wpa_vp_event)(int event, const uint8_t *addr,
			    const uint8_t *buf, size_t len, void *arg);
static void *wpa_vp_event_arg;

/* EAPOL ethertype (802.1X); the vendor tree spells it only inside
 * compiled-out units */
#define RTL_WPA_ETHERTYPE_EAPOL 0x888e

/* the beacon fixed-part size the vendor scan entries carry ahead of the
 * information elements inside WLAN_BSSID_EX.IEs (timestamp 8 + beacon
 * interval 2 + capability 2; collect_bss_info copies the frame body) */
#define RTL_BCN_FIXED_IE_LEN 12

_adapter *
rtl8851bu_wpa_adapter(void)
{
	return (_adapter *)rtl8851bu_get_adapter();
}

void
rtl8851bu_wpa_event(int event, const uint8_t *addr, const uint8_t *buf,
		    size_t len)
{

	if (wpa_vp_event != NULL)
		wpa_vp_event(event, addr, buf, len, wpa_vp_event_arg);
}

/* ------------------------------------------------------------------ */
/* ops                                                                 */

static int
vp_ensure_up(void)
{
	_adapter *a = rtl8851bu_wpa_adapter();

	if (a == NULL) {
		printf("rtl8851bu: wpa ensure_up, not attached\n");
		return -1;
	}
	/* idempotent: an up'd port must not run ndo_open a second time */
	if (rtw_hw_is_init_completed(adapter_to_dvobj(a)))
		return 0;
	return rtl8851bu_up();
}

static int
vp_get_hwaddr(uint8_t addr[6])
{
	_adapter *a = rtl8851bu_wpa_adapter();

	if (a == NULL || a->pnetdev == NULL)
		return -1;
	memcpy(addr, a->pnetdev->dev_addr, 6);
	return 0;
}

static int
vp_scan_start(const uint8_t *ssid, size_t len)
{

	/* NULL ssid = full-channel survey (drv_entry's wrapper carries the
	 * PHL site-survey dance the W3 board round installed) */
	(void)ssid;
	(void)len;
	return rtl8851bu_scan();
}

static int
vp_scan_fetch(int idx, struct wpa_scan_res_ret *out)
{
	_adapter *a = rtl8851bu_wpa_adapter();
	_queue *queue;
	_list *plist, *phead;
	int n = 0;
	int ret = -1;

	if (a == NULL || idx < 0 || out == NULL)
		return -1;

	queue = &a->mlmepriv.scanned_queue;
	phead = get_list_head(queue);

	/* the V5 idiom (rtw_do_join / drv_entry scan_dump): bh spinlock
	 * pair, no _irqL face in the PHL osdep */
	_rtw_spinlock_bh(&queue->lock);
	plist = phead->next;
	while (rtw_end_of_queue_search(phead, plist) == _FALSE) {
		struct wlan_network *pnetwork = LIST_CONTAINOR(plist,
		    struct wlan_network, list);
		WLAN_BSSID_EX *bss = &pnetwork->network;
		int ie_len;

		if (n == idx) {
			memset(out, 0, sizeof(*out));
			memcpy(out->bssid, bss->MacAddress, 6);
			out->ssid_len = (bss->Ssid.SsidLength <= 32u) ?
			    bss->Ssid.SsidLength : 32u;
			memcpy(out->ssid, bss->Ssid.Ssid, out->ssid_len);
			out->freq = rtw_ch2freq(
			    (int)bss->Configuration.DSConfig);
			out->beacon_int = (uint16_t)
			    bss->Configuration.BeaconPeriod;
			if (bss->IELength >= RTL_BCN_FIXED_IE_LEN + 2u) {
				/* capability info is the LE u16 at IEs+10;
				 * assemble by hand (the wire field is LE) */
				const u8 *cap =
				    rtw_get_capability_from_ie(bss->IEs);

				out->caps = (uint16_t)cap[0] |
				    (uint16_t)((uint16_t)cap[1] << 8);
			}
			if (bss->IELength > RTL_BCN_FIXED_IE_LEN) {
				ie_len = (int)bss->IELength -
				    RTL_BCN_FIXED_IE_LEN;
				if (ie_len > (int)sizeof(bss->IEs) -
				    RTL_BCN_FIXED_IE_LEN)
					ie_len = (int)sizeof(bss->IEs) -
					    RTL_BCN_FIXED_IE_LEN;
				out->ies = bss->IEs + RTL_BCN_FIXED_IE_LEN;
				out->ies_len = (size_t)ie_len;
			}
			out->level = (int)bss->PhyInfo.rssi;
			ret = 0;
			break;
		}
		n++;
		plist = plist->next;
	}
	_rtw_spinunlock_bh(&queue->lock);
	return ret;
}

/* the IE the supplicant's blob carries decides PSK vs open: the blob may
 * ALSO hold non-security elements (the fork appends an extended
 * capabilities IE even for open networks - sibling board evidence
 * 2026-10-03, a 13-byte wpa_ie on midea_ca_0040).  PSK is the presence
 * of an RSN (48) or WPA (221, OUI 00:50:f2, type 1) element - same parse
 * the cfg80211 reference does in rtw_cfg80211_set_wpa_ie. */
static const u8 *
vp_find_rsn_wpa_ie(const u8 *ies, size_t len)
{
	const u8 *p = ies;
	size_t left = len;

	while (left >= 2u) {
		u8 id = p[0];
		u8 l = p[1];

		if (2u + (size_t)l > left)
			break;
		if (id == WLAN_EID_RSN) /* 48 */
			return p;
		if (id == WLAN_EID_VENDOR_SPECIFIC && l >= 6u &&
		    p[2] == 0x00 && p[3] == 0x50 && p[4] == 0xf2 &&
		    p[5] == 0x01)
			return p; /* WPA (221, 00:50:f2:1) */
		p += 2u + (size_t)l;
		left -= 2u + (size_t)l;
	}
	return NULL;
}

static int
vp_connect(const struct wpa_vendor_conn *p)
{
	_adapter *a = rtl8851bu_wpa_adapter();
	const u8 *rsn = NULL;
	int rsn_len = 0;
	int ret;

	if (a == NULL || p == NULL || p->ssid == NULL || p->ssid_len == 0 ||
	    p->ssid_len > 32u) {
		printf("rtl8851bu: wpa connect bad args\n");
		return -1;
	}

	/* PSK = an RSN/WPA element inside the blob (sibling parse); the
	 * driver wrapper stashes exactly this element into
	 * securitypriv.supplicant_ie + auth/privacy (the V5 security
	 * setup lives in rtl8851bu_connect - shared with the raw
	 * `wlan8851 connect` face). */
	if (p->wpa_ie != NULL && p->wpa_ie_len > 0)
		rsn = vp_find_rsn_wpa_ie(p->wpa_ie, p->wpa_ie_len);
	if (rsn != NULL) {
		rsn_len = (int)rsn[1] + 2;
		printf("rtl8851bu: wpa connect psk ie_len=%d\n", rsn_len);
	} else {
		printf("rtl8851bu: wpa connect open\n");
	}

	ret = rtl8851bu_connect(p->bssid_set ? (const u8 *)p->bssid : NULL,
	    (const char *)p->ssid, (int)p->ssid_len, rsn, rsn_len);
	return ret;
}

static int
vp_disconnect(uint16_t reason)
{

	(void)reason;
	return rtl8851bu_disconnect();
}

/*
 * V5 key install (cfg80211_rtw_add_key -> rtw_cfg80211_set_encryption,
 * ioctl_cfg80211.c): pairwise rides rtw_setstakey_cmd (UNICAST_KEY), the
 * group key rides rtw_set_key after the link securitypriv stash.  The
 * 802.1X port open (ieee8021x_blocked = false on psta + bcmc) is what
 * lets non-EAPOL RX/TX through after the 4-way (the sibling's
 * board-proven lesson: without it DHCP/ARP never reach the stack).
 */
static int
vp_set_key(const struct wpa_vendor_key *k)
{
	_adapter *a = rtl8851bu_wpa_adapter();
	struct _ADAPTER_LINK *link;
	int idx;
	u8 n;

	if (a == NULL || k == NULL)
		return -1;
	if (k->alg != WPA_VENDOR_ALG_NONE && k->alg != WPA_VENDOR_ALG_CCMP) {
		printf("rtl8851bu: wpa set_key unsupported alg %d\n", k->alg);
		return -1;
	}
	if (k->alg == WPA_VENDOR_ALG_NONE) {
		/* the supplicant's clear-key calls (disconnect cleanup); the
		 * next connect re-stashes everything - no CAM teardown here */
		return 0;
	}

	link = GET_PRIMARY_LINK(a);
	idx = k->key_idx;

	if (k->addr != NULL && (k->addr[0] & 0x01) == 0) {
		/* unicast addr = pairwise key */
		struct sta_info *psta =
		    rtw_get_stainfo(&a->stapriv, (u8 *)k->addr);

		if (psta == NULL) {
			printf("rtl8851bu: wpa set_key, no stainfo for PTK\n");
			return -1;
		}
		n = (k->key_len > 16u) ? 16u : (u8)k->key_len;
		memcpy(psta->dot118021x_UncstKey.skey, k->key, n);
		if (k->seq != NULL && k->seq_len >= 8u) {
			u8 iv[8];
			u64 pn = RTW_GET_LE64(k->seq);
			int i;

			psta->dot11txpn.val = pn;
			psta->dot11rxpn.val = pn;
			if (rtw_pn_to_iv((u8 *)k->seq, iv, (u8)idx,
			    a->securitypriv.dot11PrivacyAlgrthm)) {
				for (i = 0; i < 16; i++)
					memcpy(psta->sta_recvpriv.rxcache.iv[i],
					    iv, 8);
			}
		}
		psta->bpairwise_key_installed = _TRUE;
		psta->dot118021XPrivacy = a->securitypriv.dot11PrivacyAlgrthm;
		psta->ieee8021x_blocked = _FALSE;
		rtw_setstakey_cmd(a, psta, UNICAST_KEY, _TRUE);
	} else {
		/* NULL addr / multicast bit = group key */
		struct link_security_priv *lsec = &link->securitypriv;

		if (idx < 0 || idx >= 4) {
			printf("rtl8851bu: wpa set_key bad idx %d\n", idx);
			return -1;
		}
		n = (k->key_len > 16u) ? 16u : (u8)k->key_len;
		memcpy(lsec->dot118021XGrpKey[idx].skey, k->key, n);
		if (k->seq != NULL && k->seq_len >= 8u)
			memcpy(lsec->iv_seq[idx], k->seq, 8);
		lsec->binstallGrpkey = _TRUE;
		lsec->dot118021XGrpKeyid = (u32)idx;
		if (rtw_set_key(a, link, idx, 1, _TRUE) == _FAIL)
			return -1;
	}

	/* both installs re-open the port (the cfg80211 reference clears
	 * the bcmc block unconditionally at each real key install) */
	{
		struct sta_info *pbcmc = rtw_get_bcmc_stainfo(a, link);

		if (pbcmc != NULL) {
			pbcmc->ieee8021x_blocked = _FALSE;
			pbcmc->dot118021XPrivacy =
			    a->securitypriv.dot11PrivacyAlgrthm;
		}
	}
	return 0;
}

static int
vp_eapol_xmit(const uint8_t dst[6], const uint8_t *buf, size_t len)
{
	_adapter *a = rtl8851bu_wpa_adapter();
	struct net_device *ndev;
	struct sk_buff *skb;
	uint8_t *frame;
	size_t frame_len = 2 * 6 + 2 + len;

	if (a == NULL || a->pnetdev == NULL)
		return -1;
	ndev = a->pnetdev;
	if (len == 0 || frame_len > 1600)
		return -1;

	skb = netdev_alloc_skb(ndev, (unsigned int)frame_len);
	if (skb == NULL)
		return -1;
	frame = skb_put(skb, (unsigned int)frame_len);
	memcpy(frame, dst, 6);
	memcpy(frame + 6, ndev->dev_addr, 6);
	frame[12] = (uint8_t)(RTL_WPA_ETHERTYPE_EAPOL >> 8);
	frame[13] = (uint8_t)(RTL_WPA_ETHERTYPE_EAPOL & 0xff);
	memcpy(frame + 14, buf, len);

	/* rtw_xmit_entry consumes the skb either way (the netdev contract);
	 * rtw_xmit special-cases the 0x888E type: unencrypted, elevated
	 * (the V5 update_attrib_sec_info drops non-EAPOL frames while the
	 * port is still ieee8021x_blocked - EAPOL passes) */
	(void)ndev->netdev_ops->ndo_start_xmit(skb, ndev);
	return (int)len;
}

static void
vp_set_event_cb(void (*fn)(int event, const uint8_t *addr,
			   const uint8_t *buf, size_t len, void *arg),
		void *arg)
{

	wpa_vp_event = fn;
	wpa_vp_event_arg = arg;
}

static const struct wpa_vendor_port rtl8851bu_wpa_port = {
	.ensure_up = vp_ensure_up,
	.get_hwaddr = vp_get_hwaddr,
	.scan_start = vp_scan_start,
	.scan_fetch = vp_scan_fetch,
	.connect = vp_connect,
	.disconnect = vp_disconnect,
	.set_key = vp_set_key,
	.eapol_xmit = vp_eapol_xmit,
	.set_event_cb = vp_set_event_cb,
};

/* ------------------------------------------------------------------ */
/* EAPOL RX (the netif_rx sink; the wpa seam owns the single sink when  */
/* the backend is in the image - non-EAPOL frames fall through into the */
/* lwip bridge when CONFIG_WLAN_NETIF)                                  */

static unsigned long rtl8851bu_wpa_eapol_rx;
static unsigned long rtl8851bu_wpa_data_drop;

/*
 * The vendor indicates through eth_type_trans, which by Linux semantics
 * PULLS the 14-byte Ethernet header - netif_rx hands the sink an skb
 * whose data sits at the payload.  The frame itself is still intact at
 * data-14; un-pull it to read the header (then re-pull before handing
 * to the lwip bridge, whose input owns its own un-pull).
 */
static int
rtl8851bu_wpa_rx_sink(struct sk_buff *skb)
{
	if (skb == NULL)
		return 1; /* NET_RX_DROP */
	if (skb->len >= 16 && skb->data != NULL) {
		skb->data -= 14;
		skb->len += 14;
		skb->tail -= 14;
		if (skb->len >= 14 &&
		    skb->data[12] == (uint8_t)(RTL_WPA_ETHERTYPE_EAPOL >> 8) &&
		    skb->data[13] ==
			(uint8_t)(RTL_WPA_ETHERTYPE_EAPOL & 0xff)) {
			/* the 4-way EAPOL comes from the associated AP; the src
			 * the pump deep-copies ([S7a] FR-7) is read off the
			 * restored header.  Driver/USB worker context: queue
			 * only. */
			rtl8851bu_wpa_event(WPA_VENDOR_EVENT_EAPOL_RX,
					    skb->data + 6, skb->data + 14,
					    skb->len - 14);
			rtl8851bu_wpa_eapol_rx++;
			kfree_skb(skb);
			return 0; /* NET_RX_SUCCESS (consumed) */
		}
		skb->data += 14;
		skb->len -= 14;
		skb->tail += 14;
	}
#ifdef CONFIG_WLAN_NETIF
	/* non-EAPOL: the lwip bridge takes it (its own copy into a pbuf)
	 * or declines (its counter accounts the drop).  The vendor's own
	 * validate_recv_data_frame already filtered addressing before
	 * this sink (STA mode: to-us or multicast only). */
	if (wlan8851_wlan_netif_input(skb) == 0)
		return 0; /* consumed by the lwip bridge */
#endif
	rtl8851bu_wpa_data_drop++;
	kfree_skb(skb);
	return 1; /* NET_RX_DROP (counted) */
}

unsigned long
rtl8851bu_wpa_rx_counters(unsigned long *eapol, unsigned long *data_drop)
{

	if (eapol != NULL)
		*eapol = rtl8851bu_wpa_eapol_rx;
	if (data_drop != NULL)
		*data_drop = rtl8851bu_wpa_data_drop;
	return rtl8851bu_wpa_eapol_rx;
}

/* ------------------------------------------------------------------ */
/* backend registration (rtl8851bu_adapter_bind calls it post-attach)   */

void
rtl8851bu_wpa_backend_init(void)
{

	wpa_vp_event = NULL;
	wpa_vp_event_arg = NULL;
	(void)wpa_vendor_port_register(&rtl8851bu_wpa_port);
	/* the single-sink seam: EAPOL rides the filter, the rest into the
	 * lwip bridge when mounted */
	lkpi_netif_rx_sink_register(rtl8851bu_wpa_rx_sink);
	printf("rtl8851bu: wpa vendor backend registered\n");
}
