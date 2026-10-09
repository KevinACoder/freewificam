/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    wlan_wpa_vendor.c
 * @brief   The net80211 bridge's wpa_supplicant backend: the nine-operation
 *          vendor contract implemented against the bridge, so the supplicant
 *          port drives this stack instead of a Linux driver.
 *
 * @design adapter / anticorruption layer, bridge-side instance (the contract
 *         header states the rule: no hostap type and no net80211 type crosses
 *         it; this file owns the translation in both directions). The shape
 *         follows the carrier whose board runs proved it (the prior line's
 *         rtw88 vendor instance): MANUAL roaming so the supplicant's join
 *         decision does not race the stack's own matcher, the pull-model scan
 *         table, the PAE-branch EAPOL seam, and the event pump fed from the
 *         port's event callback.
 *
 * Threading: the event sinks run in the driver/USB worker context and only
 * queue (they must not touch the supplicant's structures); ensure_up runs on
 * the caller's thread because the chip bring-up blocks on the USB workers.
 *
 * The composition with the presentation layer: the port carries one event
 * slot, and both consumers want it (the netif needs ASSOC/DISASSOC to start
 * DHCP; the supplicant needs all three events). The rule this file keeps is
 * that the supplicant's registration is the last one - the netif registers
 * its up/down through the bridge's ifops (which the bridge calls on the RUN
 * transitions) and its frames through the receive seam, so the event slot is
 * the supplicant's alone in a WPA image.
 *
 * @author  zhugengyu
 * @date    09.10.2026
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"

#include "../osal/wlan_osal.h"

#include "wlan_bridge.h"

#include "wpa_vendor_port.h"
#include "wpa_port_api.h"

#include "wlan_wpa_vendor.h"

/* the port's receive/event seams (declared by the component's port.h; only
 * scalars cross here) */
struct wlan_seam_drv;
void wlan_port_set_eapol_rx(void (*fn)(const uint8_t src[6],
    const uint8_t *buf, size_t len, void *arg), void *arg);
void wlan_port_set_event_handler(void (*fn)(int event, const uint8_t *addr,
    void *arg), void *arg);

/* scalars: the port's event numbering and the vendor contract's numbering
 * (both fixed headers; kept as scalars so this file includes neither world) */
#define WLAN_EV_SCAN_DONE	0
#define WLAN_EV_ASSOC		1
#define WLAN_EV_DISASSOC	2

static void (*wp_event)(int event, const uint8_t *addr, const uint8_t *buf,
    size_t len, void *arg);
static void *wp_event_arg;

/* the presentation layer's association steps, bound by wlan_wpa_netif_bind
 * (the single-event-slot composition documented in the header) */
static void *wp_netif;
static void (*wp_netif_assoc)(void *);
static void (*wp_netif_disassoc)(void *);

/*
 * The driver-side event pump. Called from the receive path (USB worker for a
 * frame, the state machine for an assoc/dissoc) - queue only, never touch the
 * supplicant here.
 */
static void
wlan_wpa_event_sink(int event, const uint8_t *addr, void *arg)
{

	(void)arg;
	/* the netif's link state follows the association regardless of whether
	 * a supplicant consumer is bound yet */
	switch (event) {
	case WLAN_EV_ASSOC:
		if (wp_netif_assoc != NULL)
			wp_netif_assoc(wp_netif);
		break;
	case WLAN_EV_DISASSOC:
		if (wp_netif_disassoc != NULL)
			wp_netif_disassoc(wp_netif);
		break;
	}

	if (wp_event == NULL)
		return;
	switch (event) {
	case WLAN_EV_SCAN_DONE:
		wp_event(WPA_VENDOR_EVENT_SCAN_DONE, NULL, NULL, 0,
		    wp_event_arg);
		break;
	case WLAN_EV_ASSOC:
		/* the port hands the event address (the BSSID) */
		wp_event(WPA_VENDOR_EVENT_ASSOC, addr, NULL, 0, wp_event_arg);
		break;
	case WLAN_EV_DISASSOC:
		wp_event(WPA_VENDOR_EVENT_DISASSOC, addr, NULL, 0,
		    wp_event_arg);
		break;
	}
}

/*
 * EAPOL from the port's receive seam. The port already classified the frame
 * and hands the payload behind the 14-byte ethernet header (with the source
 * address separately) - the exact bytes the supplicant's rx_eapol expects.
 * Malformed framing is counted, not silently dropped, so "the chip saw M1,
 * the supplicant did not" splits at the right place.
 */
static unsigned int wlan_wpa_eapol_rx;
static unsigned int wlan_wpa_eapol_badframe;

static void
wlan_wpa_eapol_sink(const uint8_t src[6], const uint8_t *buf, size_t len,
    void *arg)
{

	(void)arg;
	if (len < 4 || buf[1] != 3) {	/* EAPOL-Key */
		wlan_wpa_eapol_badframe++;
		return;
	}
	wlan_wpa_eapol_rx++;
	if (wp_event != NULL) {
		wp_event(WPA_VENDOR_EVENT_EAPOL_RX, src, buf, len,
		    wp_event_arg);
	}
}

/* ------------------------------------------------------------------ */
/* the nine operations                                                 */
/* ------------------------------------------------------------------ */

static int
vp_ensure_up(void)
{
	struct wlan_bridge_counters c;
	unsigned int i;

	/* The console may ask for the interface before the bring-up case has
	 * attached it (both run concurrently). Waiting here is the honest
	 * semantics of "bring the interface up": the caller - the console
	 * thread by contract - can afford it, and the alternative is that the
	 * first connect on a fresh boot loses a race it cannot see. Bounded so
	 * a genuinely absent card reports instead of hanging the shell. */
	for (i = 0; i < 1200; i++) {
		wlan_bridge_stats(&c);
		if (c.attached)
			break;
		osDelay(50);
	}
	if (!c.attached) {
		wlan_osal_log("wlan: no interface after 60 s; is the card in?\n");
		return -1;
	}

	if (wlan_bridge_up() != 0)
		return -1;
	/* supplicant-driven MLME: the sweep must report, not join */
	wlan_bridge_set_manual(1);
	/* WPA mode must precede any sweep the supplicant will read a table
	 * from: without the flags the stack records no RSN/WPA element on the
	 * candidates and the supplicant sees no PSK network at all */
	wlan_bridge_set_wpa(1);
	wlan_port_set_eapol_rx(wlan_wpa_eapol_sink, NULL);
	wlan_wpa_vendor_events_arm();
	return 0;
}

/*
 * (Re-)take the port's single event slot for the backend's sink. ensure_up
 * calls this, and so does a presentation layer that registered its own
 * handler afterwards (wlannetif_add does): the composition rule is that the
 * backend owns the slot and forwards the association to the bound netif, so
 * whoever registered last, one call puts the chain back in that shape.
 */
void
wlan_wpa_vendor_events_arm(void)
{

	wlan_port_set_event_handler(wlan_wpa_event_sink, NULL);
}

static int
vp_get_hwaddr(uint8_t addr[6])
{

	return wlan_bridge_hwaddr(addr);
}

static int
vp_scan_start(const uint8_t *ssid, size_t len)
{

	/* the sweep with no automatic join (MANUAL): the table is the
	 * supplicant's to read through scan_fetch */
	if (ssid != NULL && len != 0) {
		if (wlan_bridge_set_ssid(ssid, len) != 0)
			return -1;
	}
	return wlan_bridge_scan_start();
}

static int
vp_scan_fetch(int idx, struct wpa_scan_res_ret *out)
{
	struct wlan_bridge_scan_res in;

	if (wlan_bridge_scan_fetch(idx, &in) != 0)
		return -1;
	memcpy(out->bssid, in.bssid, 6);
	memcpy(out->ssid, in.ssid, in.ssid_len > 33 ? 33 : in.ssid_len);
	out->ssid_len = in.ssid_len > 33 ? 33 : in.ssid_len;
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

	return wlan_bridge_join(p->ssid, p->ssid_len,
	    p->bssid_set ? p->bssid : NULL, p->wpa_ie, p->wpa_ie_len);
}

static int
vp_disconnect(uint16_t reason)
{

	wlan_bridge_disconnect(reason);
	return 0;
}

static int
vp_set_key(const struct wpa_vendor_key *k)
{
	int pairwise;

	if (k->alg == WPA_VENDOR_ALG_NONE) {
		/* the cleanup path: clear whatever the slot held (the stack
		 * side and the driver side both) */
		pairwise = k->addr == NULL || (k->addr[0] & 0x01) == 0;
		(void)wlan_bridge_key_clear(pairwise ? 1 : 0, k->key_idx);
		return 0;
	}
	if (k->alg != WPA_VENDOR_ALG_CCMP)
		return -1;
	/* pairwise = a unicast address; group keys carry the multicast bit */
	pairwise = k->addr != NULL && (k->addr[0] & 0x01) == 0;
	return wlan_bridge_key_install(pairwise ? 1 : 0, k->key_idx, k->set_tx,
	    WLAN_SEAM_CIPHER_CCMP, k->key, k->key_len);
}

/*
 * EAPOL out: the ethernet header is built here (the contract says the caller
 * passes the 802.1X payload), then the one data path carries it - net80211's
 * own encapsulation and crypto run on the way out, so after the PTK lands the
 * supplicant's M4 leaves encrypted exactly as the standard requires.
 */
static int
vp_eapol_xmit(const uint8_t dst[6], const uint8_t *buf, size_t len)
{
	uint8_t frame[14 + 256];
	uint8_t own[6];

	if (len == 0 || len > sizeof(frame) - 14)
		return -1;
	if (wlan_bridge_hwaddr(own) != 0)
		return -1;
	memcpy(frame, dst, 6);
	memcpy(frame + 6, own, 6);
	frame[12] = 0x88;
	frame[13] = 0x8e;	/* ethertype PAE */
	memcpy(frame + 14, buf, len);
	return wlan_bridge_tx_raw(frame, 14 + len);
}

static void
vp_set_event_cb(void (*fn)(int event, const uint8_t *addr,
    const uint8_t *buf, size_t len, void *arg), void *arg)
{

	wp_event = fn;
	wp_event_arg = arg;
}

static const struct wpa_vendor_port wlan_wpa_port = {
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

/*
 * Register the backend. Called by the image's start-up after the bridge
 * attached (the supplicant's own thread later picks it up through the
 * registered table).
 */
int
wlan_wpa_vendor_init(void)
{

	wp_event = NULL;
	wp_event_arg = NULL;
	return wpa_vendor_port_register(&wlan_wpa_port);
}

/*
 * Bind the presentation layer's association steps: from here the event sink
 * forwards ASSOC/DISASSOC to them, so an image with both consumers sees the
 * netif's link state follow the association while the supplicant keeps the
 * port's single event slot. All scalars - the netif world passes its own
 * pointer and two step functions.
 */
void
wlan_wpa_netif_bind(void *netif, void (*on_assoc)(void *),
    void (*on_disassoc)(void *))
{

	wp_netif = netif;
	wp_netif_assoc = on_assoc;
	wp_netif_disassoc = on_disassoc;
}

/* diagnostics for the console and the board runs */
void
wlan_wpa_vendor_stats(unsigned int *eapol_rx, unsigned int *badframe)
{

	if (eapol_rx != NULL)
		*eapol_rx = wlan_wpa_eapol_rx;
	if (badframe != NULL)
		*badframe = wlan_wpa_eapol_badframe;
}
