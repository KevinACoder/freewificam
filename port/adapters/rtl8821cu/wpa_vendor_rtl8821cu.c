/*
 * @file   wpa_vendor_rtl8821cu.c
 * @brief  The wpa_vendor_port backend for the rtl8821cu vendor line
 *         (S5a-W4, [S7a] FR-8 contract instantiated on the feat line).
 *
 * Every op maps onto the vendor driver's NDIS face (core/rtw_ioctl_set.c
 * - all first-class APIs that sit inside the compile set); the sequence
 * mirrors cfg80211_rtw_connect (os_dep/linux/ioctl_cfg80211.c:3036, NOT
 * compiled - it is the authoritative reference, not a linked unit):
 *
 *   connect: ps_deny(PS_DENY_JOIN) + wakeup -> scan_abort ->
 *            security state (supplicant_ie stash + auth/privacy) ->
 *            rtw_set_802_11_authentication_mode -> rtw_set_802_11_connect
 *   keys:    NDIS_802_11_KEY -> rtw_set_802_11_add_key (pairwise rides
 *            setstakey_cmd, group rides rtw_set_key -> write_cam; the
 *            hardware CAM owns WPA2 encryption/decryption after that)
 *   EAPOL:   Ethernet(dst|src|0x888E) skb -> ndo_start_xmit (rtw_xmit
 *            special-cases EAPOL: unencrypted, high priority)
 *
 * Events: the seam's three indication hooks (assoc/disassoc/scan done)
 * call rtl8821cu_wpa_event(); EAPOL RX comes through the netif_rx sink
 * registered here (single-sink seam: the W5 lwip bridge will compose
 * both consumers - EAPOL handoff stays in this file's filter).
 *
 * Context rules ([S7a] FR-7): the event callback runs in driver/USB
 * worker contexts and only ever queues (the skeleton's pump does
 * wpa_send_event); ensure_up may block on USB workers and is only
 * called from the shell thread.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <drv_types.h>
#include <hal_data.h>

#include "rtl8821cu_port.h"
#include "wpa_vendor_port.h"

/* drv_entry.c */
extern int rtl8821cu_up(void);

/* the registered event pump target (skeleton init2 hands it over) */
static void (*wpa_vp_event)(int event, const uint8_t *addr,
			    const uint8_t *buf, size_t len, void *arg);
static void *wpa_vp_event_arg;

/* EAPOL ethertype (802.1X); the vendor tree spells it only inside
 * compiled-out units */
#define RTL_WPA_ETHERTYPE_EAPOL 0x888e

/* the beacon fixed-part size the vendor scan entries carry ahead of the
 * information elements inside WLAN_BSSID_EX.IEs (timestamp 8 + beacon
 * interval 2 + capability 2, collect_bss_info copies the frame body) */
#define RTL_BCN_FIXED_IE_LEN 12

_adapter *
rtl8821cu_wpa_adapter(void)
{
	return (_adapter *)rtl8821cu_get_adapter();
}

void
rtl8821cu_wpa_event(int event, const uint8_t *addr, const uint8_t *buf,
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
	_adapter *a = rtl8821cu_wpa_adapter();

	if (a == NULL) {
		printf("rtl8821cu: wpa ensure_up, not attached\n");
		return -1;
	}
	/* idempotent: an up'd port (post-open, or post-IPS-wakeup from a
	 * prior op) must not run ndo_open a second time */
	if (rtw_is_hw_init_completed(a))
		return 0;
	return rtl8821cu_up();
}

static int
vp_get_hwaddr(uint8_t addr[6])
{
	_adapter *a = rtl8821cu_wpa_adapter();

	if (a == NULL || a->pnetdev == NULL)
		return -1;
	memcpy(addr, a->pnetdev->dev_addr, 6);
	return 0;
}

static int
vp_scan_start(const uint8_t *ssid, size_t len)
{

	/* NULL ssid = full-channel survey (drv_entry's wrapper carries the
	 * PS_DENY_SCAN + wakeup dance the W3 board round installed) */
	(void)ssid;
	(void)len;
	return rtl8821cu_scan();
}

static int
vp_scan_fetch(int idx, struct wpa_scan_res_ret *out)
{
	_adapter *a = rtl8821cu_wpa_adapter();
	_queue *queue;
	_list *plist, *phead;
	_irqL irqL;
	int n = 0;
	int ret = -1;

	if (a == NULL || idx < 0 || out == NULL)
		return -1;

	queue = &a->mlmepriv.scanned_queue;
	phead = get_list_head(queue);

	_enter_critical(&queue->lock, &irqL);
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
				u16 cap = 0;

				memcpy((u8 *)&cap,
				    rtw_get_capability_from_ie(bss->IEs), 2);
				out->caps = le16_to_cpu(cap);
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
			out->level = (int)bss->Rssi;
			ret = 0;
			break;
		}
		n++;
		plist = plist->next;
	}
	_exit_critical(&queue->lock, &irqL);
	return ret;
}

/* the IE the supplicant's blob carries decides PSK vs open: the blob
 * may ALSO hold non-security elements (the fork appends an extended
 * capabilities IE even for open networks - board evidence 2026-10-03,
 * a 13-byte wpa_ie on midea_ca_0040).  PSK is the presence of an RSN
 * (48) or WPA (221, OUI 00:50:f2, type 1) element - same parse the
 * cfg80211 reference does in rtw_cfg80211_set_wpa_ie. */
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

/*
 * The supplicant's RSN/WPA IE goes into securitypriv.supplicant_ie
 * verbatim - the stash the assoc-request builder consumes (the cfg80211
 * reference stashes exactly the parsed element, not the whole blob).
 * The PSK-only build negotiates CCMP only (wpa_do_connect pins
 * pairwise/group CCMP), so the cipher mapping below is the fixed
 * PSK-CCMP row of the cfg80211 switch.
 */
static void
vp_security_setup(_adapter *a, const struct wpa_vendor_conn *p)
{
	struct security_priv *sec = &a->securitypriv;
	const u8 *rsn = NULL;

	memset(sec->supplicant_ie, 0, sizeof(sec->supplicant_ie));
	if (p->wpa_ie != NULL && p->wpa_ie_len > 0)
		rsn = vp_find_rsn_wpa_ie(p->wpa_ie, p->wpa_ie_len);

	if (rsn != NULL) {
		size_t n = (size_t)rsn[1] + 2u;

		if (n > sizeof(sec->supplicant_ie))
			n = sizeof(sec->supplicant_ie);
		memcpy(sec->supplicant_ie, rsn, n);
		sec->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
		sec->ndisauthtype = Ndis802_11AuthModeWPA2PSK;
		sec->dot11PrivacyAlgrthm = _AES_;
		sec->dot118021XGrpPrivacy = _AES_;
		sec->ndisencryptstatus = Ndis802_11Encryption3Enabled;
		printf("rtl8821cu: wpa connect psk ie_len=%u\n",
		       (unsigned)n);
	} else {
		sec->ndisencryptstatus = Ndis802_11EncryptionDisabled;
		sec->dot11PrivacyAlgrthm = _NO_PRIVACY_;
		sec->dot118021XGrpPrivacy = _NO_PRIVACY_;
		sec->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
		sec->ndisauthtype = Ndis802_11AuthModeOpen;
		printf("rtl8821cu: wpa connect open\n");
	}
}

static int
vp_connect(const struct wpa_vendor_conn *p)
{
	_adapter *a = rtl8821cu_wpa_adapter();
	NDIS_802_11_SSID ndis_ssid;
	u8 status;
	int ret = -1;

	if (a == NULL || p == NULL || p->ssid == NULL || p->ssid_len == 0 ||
	    p->ssid_len > sizeof(ndis_ssid.Ssid)) {
		printf("rtl8821cu: wpa connect bad args\n");
		return -1;
	}

	/* the cfg80211_rtw_connect sequence (:3094-3260) */
	rtw_ps_deny(a, PS_DENY_JOIN);
	if (_FAIL == rtw_pwr_wakeup(a)) {
		rtw_ps_deny_cancel(a, PS_DENY_JOIN);
		printf("rtl8821cu: wpa connect wakeup failed\n");
		return -1;
	}
	rtw_mi_scan_abort(a, true);

	memset(&ndis_ssid, 0, sizeof(ndis_ssid));
	ndis_ssid.SsidLength = (u32)p->ssid_len;
	memcpy(ndis_ssid.Ssid, p->ssid, p->ssid_len);

	vp_security_setup(a, p);
	rtw_set_802_11_authentication_mode(a,
	    (NDIS_802_11_AUTHENTICATION_MODE)a->securitypriv.ndisauthtype);

	status = rtw_set_802_11_connect(a,
	    p->bssid_set ? (u8 *)p->bssid : NULL, &ndis_ssid);
	ret = (status == _TRUE) ? 0 : -1;

	rtw_ps_deny_cancel(a, PS_DENY_JOIN);
	printf("rtl8821cu: wpa connect \"%s\" -> %d\n",
	       (const char *)p->ssid, ret);
	return ret;
}

static int
vp_disconnect(uint16_t reason)
{
	_adapter *a = rtl8821cu_wpa_adapter();

	(void)reason;
	if (a == NULL)
		return -1;
	return (rtw_set_802_11_disassociate(a) == _TRUE) ? 0 : -1;
}

static int
vp_set_key(const struct wpa_vendor_key *k)
{
	_adapter *a = rtl8821cu_wpa_adapter();
	NDIS_802_11_KEY ndis_key;
	uint64_t pn = 0;
	int i;

	if (a == NULL || k == NULL)
		return -1;
	if (k->alg != WPA_VENDOR_ALG_NONE && k->alg != WPA_VENDOR_ALG_CCMP) {
		printf("rtl8821cu: wpa set_key unsupported alg %d\n", k->alg);
		return -1;
	}

	memset(&ndis_key, 0, sizeof(ndis_key));
	ndis_key.Length = (ULONG)sizeof(ndis_key);
	ndis_key.KeyIndex = (ULONG)k->key_idx;
	if (k->set_tx)
		ndis_key.KeyIndex |= 0x80000000UL;
	if (k->addr != NULL && (k->addr[0] & 0x01) == 0)
		memcpy(&ndis_key.BSSID, k->addr, 6);
	if (k->key != NULL && k->key_len <= sizeof(ndis_key.KeyMaterial)) {
		ndis_key.KeyLength = (ULONG)k->key_len;
		memcpy(ndis_key.KeyMaterial, k->key, k->key_len);
	}
	/* the PN arrives big-endian; KeyRSC is the LE u64 the CAM reads */
	for (i = (int)k->seq_len - 1; i >= 0; i--)
		pn = (pn << 8) | k->seq[i];
	memcpy(&ndis_key.KeyRSC, &pn, sizeof(pn));

	return (rtw_set_802_11_add_key(a, &ndis_key) == _SUCCESS) ? 0 : -1;
}

static int
vp_eapol_xmit(const uint8_t dst[6], const uint8_t *buf, size_t len)
{
	_adapter *a = rtl8821cu_wpa_adapter();
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
	 * rtw_xmit special-cases the 0x888E type: unencrypted, elevated */
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

static const struct wpa_vendor_port rtl8821cu_wpa_port = {
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
/* EAPOL RX (the netif_rx sink; single-sink seam until W5 composes)     */

static unsigned long rtl8821cu_wpa_eapol_rx;
static unsigned long rtl8821cu_wpa_data_drop;

/*
 * The vendor indicates through eth_type_trans (recv_linux.c:510), which
 * by Linux semantics PULLS the 14-byte Ethernet header - netif_rx hands
 * the sink an skb whose data sits at the payload and whose protocol
 * carries the wire ethertype (the LE read of the big-endian field).
 * The frame itself is still intact at data-14; un-pull it to read the
 * header (the same restore the W5 lwip bridge will need for full
 * Ethernet frames).
 */
static void
rtl8821cu_wpa_skb_unpull(struct sk_buff *skb)
{

	skb->data -= 14;
	skb->len += 14;
	skb->tail -= 14;
}

static int
rtl8821cu_wpa_rx_sink(struct sk_buff *skb)
{
	if (skb == NULL)
		return 1; /* NET_RX_DROP */
	/* restore the header first, then match the wire bytes: the
	 * linuxkpi htons is an identity macro (constant-expression
	 * service) so skb->protocol holds the LE read of the big-endian
	 * field - byte comparison sidesteps the whole endianness mess */
	if (skb->len >= 16 && skb->data != NULL) {
		rtl8821cu_wpa_skb_unpull(skb);
		if (skb->len >= 14 &&
		    skb->data[12] == (uint8_t)(RTL_WPA_ETHERTYPE_EAPOL >> 8) &&
		    skb->data[13] ==
			(uint8_t)(RTL_WPA_ETHERTYPE_EAPOL & 0xff)) {
		/* the 4-way EAPOL comes from the associated AP; the src
		 * the event queue deep-copies ([S7a] FR-7) is read off
		 * the restored header.  Driver/USB worker context: the
		 * pump only queues. */
		rtl8821cu_wpa_event(WPA_VENDOR_EVENT_EAPOL_RX,
				    skb->data + 6, skb->data + 14,
				    skb->len - 14);
			rtl8821cu_wpa_eapol_rx++;
			kfree_skb(skb);
			return 0; /* NET_RX_SUCCESS (consumed) */
		}
	}
	rtl8821cu_wpa_data_drop++;
	kfree_skb(skb);
	return 1; /* NET_RX_DROP (counted) */
}

unsigned long
rtl8821cu_wpa_rx_counters(unsigned long *eapol, unsigned long *data_drop)
{

	if (eapol != NULL)
		*eapol = rtl8821cu_wpa_eapol_rx;
	if (data_drop != NULL)
		*data_drop = rtl8821cu_wpa_data_drop;
	return rtl8821cu_wpa_eapol_rx;
}

/* ------------------------------------------------------------------ */
/* backend registration (rtl8821cu_adapter_bind calls it post-attach)   */

void
rtl8821cu_wpa_backend_init(void)
{

	wpa_vp_event = NULL;
	wpa_vp_event_arg = NULL;
	(void)wpa_vendor_port_register(&rtl8821cu_wpa_port);
	/* the single-sink seam: EAPOL rides the filter, the rest counts
	 * as dropped until the W5 lwip bridge composes the consumers */
	lkpi_netif_rx_sink_register(rtl8821cu_wpa_rx_sink);
	printf("rtl8821cu: wpa vendor backend registered\n");
}
