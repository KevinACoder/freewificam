/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    backend_cfg80211.c
 * @brief   The wpa_supplicant backend for a vendor driver that talks cfg80211:
 *          the nine-operation vendor contract implemented against the
 *          Linux-compatibility face, so the supplicant port drives this driver
 *          instead of a hostap driver.
 *
 * @design adapter / anticorruption layer, driver-side instance. The shape is the
 *         one whose board runs proved it - the net80211 bridge's backend
 *         (ports/net80211/bridge/wlan_wpa_vendor.c:62-138 the event slot and the
 *         EAPOL sink, :144-291 the nine operations, :302-326 the table and its
 *         registration) - with the bridge's dotted face replaced by the
 *         substitute's seam (components/linuxkpi/linuxkpi_cfg80211.h): the
 *         downward calls are lkpicfg_connect/disconnect/set_key/scan_start and
 *         the upward ones are the consumer callbacks the substitute invokes
 *         (attached/scan_done/bss/connected/disconnected). The transmit side is
 *         the netif adapter's frame door (ports/lwip/lkpinetif.h), because this
 *         driver's only data path out is its net device.
 *
 * Threading (the contract's rule): the consumer callbacks and the EAPOL sink run
 * in the driver's own contexts - its receive worker and its command thread - so
 * they only queue (they must not touch the supplicant's structures); ensure_up
 * runs on the caller's thread because the driver's open path blocks.
 *
 * @date    12.10.2026
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linuxkpi_cfg80211.h"
#include <linux/if_ether.h>

#include "lkpinetif_seam.h"

#include "wpa_vendor_port.h"

#include "backend_cfg80211.h"

static void (*wp_event)(int event, const uint8_t *addr, const uint8_t *buf,
    size_t len, void *arg);
static void *wp_event_arg;

/* the presentation layer's link steps, bound by the image (the same composition
 * the net80211 backend uses: the backend owns the event slot and forwards) */
static void *wp_netif;
static void (*wp_netif_up)(void *);
static void (*wp_netif_down)(void *);

/* counters, so a loss localizes without a debugger */
static unsigned int wp_bss_seen;
static unsigned int wp_eapol_rx;
static unsigned int wp_eapol_badframe;

/* ------------------------------------------------------------------ */
/* upward: what the driver reports back                                */
/* ------------------------------------------------------------------ */

/*
 * The substitute's consumer. Every callback here runs in a driver context, so it
 * only forwards: the supplicant's event pump takes a copy and queues.
 */
static void
wp_consumer_attached(struct wiphy *wiphy)
{

	(void)wiphy;
}

static void
wp_consumer_bss(struct wiphy *wiphy, const struct lkpicfg_bss *bss)
{

	(void)wiphy;
	(void)bss;
	wp_bss_seen++;
}

static void
wp_consumer_scan_done(struct wiphy *wiphy)
{

	(void)wiphy;
	if (wp_event != NULL)
		wp_event(WPA_VENDOR_EVENT_SCAN_DONE, NULL, NULL, 0, wp_event_arg);
}

static void
wp_consumer_connected(struct wiphy *wiphy, const uint8_t bssid[6], uint16_t status)
{

	(void)wiphy;
	if (wp_netif_up != NULL)
		wp_netif_up(wp_netif);
	if (wp_event != NULL)
		wp_event(WPA_VENDOR_EVENT_ASSOC, bssid, NULL, 0, wp_event_arg);
	(void)status;
}

static void
wp_consumer_disconnected(struct wiphy *wiphy, uint16_t reason)
{

	(void)wiphy;
	if (wp_netif_down != NULL)
		wp_netif_down(wp_netif);
	if (wp_event != NULL)
		wp_event(WPA_VENDOR_EVENT_DISASSOC, NULL, NULL, 0, wp_event_arg);
	(void)reason;
}

static const struct lkpicfg_consumer_ops wp_consumer = {
	.attached = wp_consumer_attached,
	.scan_done = wp_consumer_scan_done,
	.bss = wp_consumer_bss,
	.connected = wp_consumer_connected,
	.disconnected = wp_consumer_disconnected,
};

/*
 * EAPOL from the interface's receive path. The netif adapter already classified
 * the frame and hands the payload behind the 14-byte ethernet header with the
 * source address separately - the exact bytes the supplicant's rx_eapol expects.
 * Malformed framing is counted, not silently dropped, so "the chip saw M1, the
 * supplicant did not" splits at the right place.
 */
static void
wp_eapol_sink(const uint8_t src[6], const uint8_t *buf, size_t len, void *arg)
{

	(void)arg;
	if (len < 4 || buf[1] != 3) {	/* EAPOL-Key */
		wp_eapol_badframe++;
		return;
	}
	wp_eapol_rx++;
	if (wp_event != NULL)
		wp_event(WPA_VENDOR_EVENT_EAPOL_RX, src, buf, len, wp_event_arg);
}

void
lkpi_wpa_cfg80211_events_arm(void)
{

	(void)lkpicfg_consumer_register(&wp_consumer, NULL);
}

/* ------------------------------------------------------------------ */
/* the nine operations                                                 */
/* ------------------------------------------------------------------ */

static int
vp_ensure_up(void)
{
	struct wiphy *wiphy;
	unsigned int i;

	/*
	 * The console may ask for the interface before the bring-up case has
	 * attached it (both run concurrently). Waiting here is the honest semantics
	 * of "bring the interface up": the caller is the console thread by
	 * contract and can afford it, and the alternative is that the first
	 * connect on a fresh boot loses a race it cannot see. Bounded, so a
	 * genuinely absent card reports instead of hanging the shell.
	 */
	for (i = 0; i < 1200; i++) {
		wiphy = lkpicfg_wiphy();
		if (wiphy != NULL)
			break;
		osDelay(50);
	}
	if (wiphy == NULL)
		return -1;

	/* the driver's own open path: it runs the chip's init and fills the
	 * interface's address, which is why it precedes every read of it */
	if (lkpicfg_netdev_up(wiphy) != 0)
		return -1;

	/*
	 * The presentation layer's interface binding belongs here, not to the
	 * application that later assembles the lwIP netif: the supplicant needs the
	 * interface's frames (the handshake's EAPOL) from the moment the driver
	 * starts delivering them, and its own frames leave through the same door.
	 * A binding made later would miss the first exchange - the driver's join
	 * completes and the AP's first key message is dropped as "another
	 * interface's frame".
	 */
	if (lkpinetif_bind(wiphy) != 0)
		return -1;

	/* the supplicant's EAPOL receive, and the backend as the substitute's
	 * consumer (last registration wins, and this is the one that must win) */
	lkpinetif_set_eapol_sink(wp_eapol_sink, NULL);
	lkpi_wpa_cfg80211_events_arm();
	return 0;
}

static int
vp_get_hwaddr(uint8_t addr[6])
{
	struct wiphy *wiphy = lkpicfg_wiphy();

	if (wiphy == NULL)
		return -1;
	return lkpicfg_get_hwaddr(wiphy, addr);
}

static int
vp_scan_start(const uint8_t *ssid, size_t len)
{
	struct wiphy *wiphy = lkpicfg_wiphy();

	(void)ssid;
	(void)len;
	if (wiphy == NULL)
		return -1;
	/*
	 * A broadcast sweep: the substitute's scan request carries no SSID list, so
	 * the driver walks every channel it advertises and reports every candidate.
	 * A caller that asked for one name gets it among the results, which is what
	 * the pull-model table below is read for.
	 */
	return lkpicfg_scan_start(wiphy);
}

static int
vp_scan_fetch(int idx, struct wpa_scan_res_ret *out)
{
	struct wiphy *wiphy = lkpicfg_wiphy();
	struct lkpicfg_bss in;

	if (wiphy == NULL || out == NULL || idx < 0)
		return -1;
	if (lkpicfg_bss_at(wiphy, (unsigned int)idx, &in) != 0)
		return -1;
	memcpy(out->bssid, in.bssid, 6);
	memcpy(out->ssid, in.ssid, in.ssid_len > 33 ? 33 : in.ssid_len);
	out->ssid_len = in.ssid_len > 33 ? 33 : in.ssid_len;
	/* the substitute keeps the channel as cfg80211 reports it, in MHz, which is
	 * the unit the supplicant's table uses */
	out->freq = (int)in.freq;
	out->beacon_int = 0;		/* the substitute does not keep it */
	out->caps = in.capability;
	out->level = in.signal;
	/* borrowed for this call: the table entry owns the stream, and the
	 * supplicant's translation copies what it keeps */
	out->ies = in.ies;
	out->ies_len = in.ies_len;
	return 0;
}

static int
vp_connect(const struct wpa_vendor_conn *p)
{
	struct wiphy *wiphy = lkpicfg_wiphy();

	if (wiphy == NULL || p == NULL)
		return -1;
	return lkpicfg_connect(wiphy, p->ssid, p->ssid_len,
	    p->bssid_set ? p->bssid : NULL, p->wpa_ie, p->wpa_ie_len);
}

static int
vp_disconnect(uint16_t reason)
{
	struct wiphy *wiphy = lkpicfg_wiphy();

	if (wiphy == NULL)
		return -1;
	return lkpicfg_disconnect(wiphy, reason);
}

static int
vp_set_key(const struct wpa_vendor_key *k)
{
	struct wiphy *wiphy = lkpicfg_wiphy();
	int pairwise;

	if (wiphy == NULL || k == NULL)
		return -1;
	if (k->alg == WPA_VENDOR_ALG_NONE) {
		/*
		 * The cleanup path, and also the PMK install (the supplicant hands
		 * a key with no algorithm): this driver holds no key to clear until
		 * the 4-way installs one, and the PMK stays in the supplicant, so
		 * answering "done" is the honest reply - failing here would abort a
		 * handshake that has not started.
		 */
		return 0;
	}
	if (k->alg != WPA_VENDOR_ALG_CCMP)
		return -1;
	/* pairwise = a unicast address; group keys carry the multicast bit */
	pairwise = k->addr != NULL && (k->addr[0] & 0x01) == 0;
	return lkpicfg_set_key(wiphy, (unsigned int)k->key_idx, pairwise,
	    pairwise ? k->addr : NULL, k->key, k->key_len, k->seq, k->seq_len);
}

/*
 * EAPOL out: the ethernet header is built here (the contract says the caller
 * passes the 802.1X payload), then the interface's own transmit entry carries it
 * - so the driver's encapsulation and its per-frame security state decide how it
 * leaves, which is what makes the handshake's M2 leave unencrypted and its M4
 * encrypted exactly as the standard requires.
 */
static int
vp_eapol_xmit(const uint8_t dst[6], const uint8_t *buf, size_t len)
{
	uint8_t frame[ETH_HLEN + 256];
	uint8_t own[6];

	if (len == 0 || len > sizeof(frame) - ETH_HLEN)
		return -1;
	if (vp_get_hwaddr(own) != 0)
		return -1;
	memcpy(frame, dst, 6);
	memcpy(frame + 6, own, 6);
	frame[12] = 0x88;
	frame[13] = 0x8e;	/* ethertype PAE */
	memcpy(frame + ETH_HLEN, buf, len);
	return lkpinetif_tx_frame(frame, ETH_HLEN + len) == 0 ? (int)len : -1;
}

static void
vp_set_event_cb(void (*fn)(int event, const uint8_t *addr,
    const uint8_t *buf, size_t len, void *arg), void *arg)
{

	wp_event = fn;
	wp_event_arg = arg;
}

static const struct wpa_vendor_port lkpi_wpa_cfg80211_port = {
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

int
lkpi_wpa_cfg80211_init(void)
{

	wp_event = NULL;
	wp_event_arg = NULL;
	return wpa_vendor_port_register(&lkpi_wpa_cfg80211_port);
}

void
lkpi_wpa_cfg80211_netif_bind(void *netif, void (*on_up)(void *),
    void (*on_down)(void *))
{

	wp_netif = netif;
	wp_netif_up = on_up;
	wp_netif_down = on_down;
}

void
lkpi_wpa_cfg80211_stats(unsigned int *eapol_rx, unsigned int *badframe,
    unsigned int *bss_seen)
{

	if (eapol_rx != NULL)
		*eapol_rx = wp_eapol_rx;
	if (badframe != NULL)
		*badframe = wp_eapol_badframe;
	if (bss_seen != NULL)
		*bss_seen = wp_bss_seen;
}
