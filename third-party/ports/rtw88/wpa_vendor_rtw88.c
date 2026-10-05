/*
 * wpa_vendor_rtw88.c - the rtw88/net80211 line's wpa_supplicant backend
 * (S9 W4, [S7a] FR-8 nine-op contract instantiated for the net80211
 * bridge world).
 *
 * Shape: the supplicant owns MLME decisions and the net80211 bridge is
 * put into MANUAL roaming (the prior project's board-proven shape on
 * the same vendored net80211 tree, plus the 8851bu line's event-pump
 * contract):
 *   - scan_start      -> net80211_bridge_scan (probe sweep, MANUAL: no
 *                        auto-join; end_scan only reports)
 *   - scan_fetch      -> pull-model over the bridge's scan table
 *   - connect         -> net80211_bridge_wpa_join: RSN IE into ic_opt_ie,
 *                        F_PRIVACY/F_WPA2, AUTH_8021X on the bss node,
 *                        direct ieee80211_sta_join
 *   - set_key         -> net80211 key bookkeeping + rtw88 CAM write
 *   - eapol_xmit      -> build the 0x888E ethernet frame and inject it
 *                        into net80211's data path (S8 FR-12 one channel)
 *   - events          <- net80211 compat seams: EAPOL via the
 *                        if_percpuq_enqueue PAE branch (whole ethernet
 *                        frame), ASSOC/DISASSOC/SCAN_DONE via
 *                        rt_ieee80211msg
 *
 * Threading: all driver-side sinks run in worker contexts and only
 * queue (wpa_send_event); ensure_up runs on the shell thread (chip
 * bring-up blocks on USB workers).
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "net80211_bridge.h"
#include "net80211_port.h"
#include "wpa_vendor_port.h"

#ifdef CONFIG_WLAN_NETIF
/* port/adapters/rtw88/wlanrtw_netif.c: the netif rides the supplicant's
 * event pump (single event-slot composition - see the netif file) */
extern void wlanrtw_netif_on_assoc(void);
extern void wlanrtw_netif_on_disassoc(void);
#endif

/* prototype in rtw88_entry.c (LKPI world) - keep the C ABI scalar */
void rtw88_wpa_backend_init(void);

static void (*wpa_vp_event)(int event, const uint8_t *addr,
    const uint8_t *buf, size_t len, void *arg);
static void *wpa_vp_event_arg;

static unsigned int wpa_vp_eapol_rx, wpa_vp_eapol_tx;
static unsigned int wpa_vp_scan_done;
static unsigned int wpa_vp_keys;

static void
rtw88_wpa_event(int event, const uint8_t *addr, const uint8_t *buf,
    size_t len)
{

	/* board-round visibility: the supplicant's own prints are mostly
	 * MSG_DEBUG (suppressed at INFO); these drive-side lines are the
	 * only reliable clocks on the event chain */
	switch (event) {
	case WPA_VENDOR_EVENT_SCAN_DONE:
		printf("rtw88u: wpa event scan-done (cb %p)\n",
		    (void *)wpa_vp_event);
		break;
	case WPA_VENDOR_EVENT_ASSOC:
		printf("rtw88u: wpa event assoc\n");
		break;
	case WPA_VENDOR_EVENT_DISASSOC:
		printf("rtw88u: wpa event disassoc\n");
		break;
	case WPA_VENDOR_EVENT_EAPOL_RX:
		printf("rtw88u: wpa event eapol len %u\n", (unsigned)len);
		break;
	}
	if (wpa_vp_event != NULL)
		wpa_vp_event(event, addr, buf, len, wpa_vp_event_arg);
}

/* ------------------------------------------------------------------ */
/* seams from the net80211 compat world                                */
/* ------------------------------------------------------------------ */

/* whole ethernet frame: src = +6, ethertype = +12, 802.1X = +14.
 * The event carries the 802.1X PAYLOAD (src separately) - feeding the
 * ethernet frame through would make the supplicant blind to M1 (codex
 * 2026-10-05 answer-4 D1).  Framing violations are counted, not just
 * dropped, so "chip RX saw M1, supplicant did not" splits cleanly. */
static unsigned int wpa_vp_eapol_badframe;

static void
rtw88_wpa_eapol_sink(const void *buf, size_t len, void *arg)
{
	const uint8_t *f = buf;

	(void)arg;
	if (len < 14 + 4)
		return;
	if (f[12] != 0x88 || f[13] != 0x8e)
		return;
	if ((f[14] < 1 || f[14] > 3) || f[15] != 3) {	/* version, KEY */
		if (wpa_vp_eapol_badframe++ < 4)
			printf("rtw88u: eapol framing bad v=%u t=%u len %u\n",
			    f[14], f[15], (unsigned)len);
		return;
	}
	wpa_vp_eapol_rx++;
	rtw88_wpa_event(WPA_VENDOR_EVENT_EAPOL_RX, f + 6, f + 14,
	    len - 14);
}

static void
rtw88_wpa_event_sink(enum net80211_port_event event, const void *mac,
    void *arg)
{

	(void)arg;
	switch (event) {
	case NET80211_PORT_SCAN_DONE:
		wpa_vp_scan_done++;
		rtw88_wpa_event(WPA_VENDOR_EVENT_SCAN_DONE, NULL, NULL, 0);
		break;
	case NET80211_PORT_ASSOC:
#ifdef CONFIG_WLAN_NETIF
		wlanrtw_netif_on_assoc();
#endif
		/* compat passes iev_addr (6 B) - the BSSID */
		rtw88_wpa_event(WPA_VENDOR_EVENT_ASSOC,
		    (const uint8_t *)mac, NULL, 0);
		break;
	case NET80211_PORT_DISASSOC:
#ifdef CONFIG_WLAN_NETIF
		wlanrtw_netif_on_disassoc();
#endif
		rtw88_wpa_event(WPA_VENDOR_EVENT_DISASSOC, NULL, NULL, 0);
		break;
	}
}

/* ------------------------------------------------------------------ */
/* the nine ops                                                        */
/* ------------------------------------------------------------------ */

static int
vp_ensure_up(void)
{

	if (net80211_bridge_up() != 0)
		return -1;
	/* supplicant-driven MLME: AUTO's end_scan matcher would race the
	 * supplicant's own join decision */
	net80211_bridge_set_manual(1);
	/* WPA mode must precede any scan: the flag gates the RSN/WPA IE
	 * parse, and a table scanned without it carries no ni_wpa_ie -
	 * the supplicant then sees no PSK network and loops SCANNING
	 * (R4 board round 1 finding) */
	net80211_bridge_set_wpa(1);
	net80211_port_set_eapol_rx(rtw88_wpa_eapol_sink, NULL);
	net80211_port_set_event_handler(rtw88_wpa_event_sink, NULL);
	return 0;
}

static int
vp_get_hwaddr(uint8_t addr[6])
{

	return net80211_bridge_get_hwaddr(addr);
}

static int vp_fetch_dump_left;	/* rows to hex-dump after a scan start */

static int
vp_scan_start(const uint8_t *ssid, size_t len)
{

	printf("rtw88u: wpa scan start ssid_len %u\n", (unsigned)len);
	vp_fetch_dump_left = 3;
	return net80211_bridge_scan(ssid, len);
}

static int
vp_scan_fetch(int idx, struct wpa_scan_res_ret *out)
{
	struct net80211_bridge_scan in;

	if (idx == 0) {
		unsigned int rows, wpa_rows;

		net80211_bridge_scan_sensor(&rows, &wpa_rows);
		printf("rtw88u: wpa fetch table rows %u wpa-ie %u\n",
		    rows, wpa_rows);
	}
	if (net80211_bridge_scan_fetch(idx, &in) != 0)
		return -1;
	/* wire-truth sample of the target row after a scan: the matcher's
	 * input for the network we are trying to reach, byte for byte
	 * (dumping arbitrary early rows showed only unrelated BSSes) */
	if (vp_fetch_dump_left > 0 && in.ssid_len == 7 &&
	    memcmp(in.ssid, "QiQiJia", 7) == 0) {
		size_t n = in.ies_len < 40 ? in.ies_len : 40, i;

		vp_fetch_dump_left--;
		printf("rtw88u: fetch[%d] %.*s cap=%04x ies=%u:",
		    idx, (int)in.ssid_len, (const char *)in.ssid,
		    in.caps, (unsigned)in.ies_len);
		for (i = 0; i < n; i++)
			printf(" %02x", in.ies[i]);
		printf("\n");
	}
	memcpy(out->bssid, in.bssid, 6);
	memcpy(out->ssid, in.ssid, in.ssid_len > 32 ? 32 : in.ssid_len);
	out->ssid_len = in.ssid_len > 32 ? 32 : in.ssid_len;
	out->freq = in.freq;
	out->beacon_int = in.beacon_int;
	out->caps = in.caps;
	out->level = in.level;
	out->ies = in.ies;
	out->ies_len = in.ies_len;
	return 0;
}

static int
vp_connect(const struct wpa_vendor_conn *p)
{
	int ret;

	printf("rtw88u: wpa connect ssid %.*s bssid_set %d ie_len %u\n",
	    (int)p->ssid_len, (const char *)p->ssid, p->bssid_set,
	    (unsigned)p->wpa_ie_len);
	ret = net80211_bridge_wpa_join(p->ssid, p->ssid_len,
	    p->bssid_set ? p->bssid : NULL, p->wpa_ie, p->wpa_ie_len);
	if (ret != 0)
		printf("rtw88u: wpa join failed %d\n", ret);
	return ret;
}

static int
vp_disconnect(uint16_t reason)
{

	net80211_bridge_disconnect(reason);
	return 0;
}

static int
vp_set_key(const struct wpa_vendor_key *k)
{
	int pairwise;

	if (k->alg == WPA_VENDOR_ALG_NONE) {
		/* cleanup path: clear whatever the slot held */
		net80211_bridge_key_clear(k->addr == NULL ||
		    (k->addr[0] & 0x01) == 0, k->key_idx);
		return 0;
	}
	if (k->alg != WPA_VENDOR_ALG_CCMP)
		return -1;
	pairwise = k->addr != NULL && (k->addr[0] & 0x01) == 0;
	wpa_vp_keys++;
	return net80211_bridge_key_install(pairwise, k->key_idx,
	    k->set_tx, k->key, k->key_len);
}

/* ethernet header built here (dst = AP/BSSID, src = our MAC, 0x888E),
 * then the one data TX channel carries it: encap adds LLC/SNAP + the
 * 802.11 header, crypto rides net80211's key state (encrypted M4 after
 * the PTK install is the spec behavior net80211 already implements) */
static int
vp_eapol_xmit(const uint8_t dst[6], const uint8_t *buf, size_t len)
{
	uint8_t frame[14 + 256];
	uint8_t own[6];

	if (len > sizeof(frame) - 14)
		return -1;
	if (net80211_bridge_get_hwaddr(own) != 0)
		return -1;
	memcpy(frame, dst, 6);
	memcpy(frame + 6, own, 6);
	frame[12] = 0x88;
	frame[13] = 0x8e;
	memcpy(frame + 14, buf, len);
	wpa_vp_eapol_tx++;
	return net80211_bridge_tx_raw(frame, 14 + len);
}

static void
vp_set_event_cb(void (*fn)(int event, const uint8_t *addr,
    const uint8_t *buf, size_t len, void *arg), void *arg)
{

	wpa_vp_event = fn;
	wpa_vp_event_arg = arg;
}

static const struct wpa_vendor_port rtw88_wpa_port = {
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

void
rtw88_wpa_backend_init(void)
{

	wpa_vp_event = NULL;
	wpa_vp_event_arg = NULL;
	(void)wpa_vendor_port_register(&rtw88_wpa_port);
	printf("rtw88u: wpa vendor backend registered\n");
}
