/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    wlan_seam_adapter.c
 * @brief   The linuxkpi-world half of the bridge: it implements the scalar seam
 *          vtable by calling the mac80211 substitute's helpers, and it owns the
 *          Linux-side objects (the hw the substitute allocated, its ops table,
 *          the interface address).
 *
 * @design two-worlds adapter, driver half (wlan_bridge.h states why the bridge
 *         comes in two halves). This file is the only place where the imported
 *         net80211 stack's world meets the linuxkpi world, and it does not let
 *         them share anything: it includes seam.h (scalars only) and the
 *         linuxkpi substitute face, translates one direction into the other,
 *         and never hands a net80211 struct to the linuxkpi side or the reverse.
 *
 * The translation table it implements:
 *
 *   seam vtable (net80211 half calls)        mac80211 substitute (driver side)
 *   ---------------------------------------  ---------------------------------
 *   start/stop                               lkpi80211_drv_start/stop
 *   config(channel)                          lkpi80211_drv_config_channel
 *   filter(bits)                             lkpi80211_drv_filter (the driver's
 *                                            ops->configure_filter runs here)
 *   sw_scan_start/complete                   lkpi80211_drv_scan_start/complete
 *   tx(frame)                                lkpi80211_drv_tx (the driver's
 *                                            ops->tx runs here)
 *   set_key/clear_key                        lkpi80211_drv_key (ops->set_key)
 *   bss_info(joined, bssid, freq)            fills the vif's bss_conf and calls
 *                                            lkpi80211_drv_bss_changed
 *   add_interface(mac)                       lkpi80211_drv_vif_add, then the
 *                                            net80211 half's attach
 *
 * The two upward directions (the substitute's bridge ops): attach/detach build
 * and tear down the net80211 half, rx forwards the driver's frames into it, and
 * tx_complete records the completion.
 *
 * @date    08.10.2026
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../seam.h"
#include "../osal/wlan_osal.h"
#include "wlan_bridge.h"

#include "components/linuxkpi/linuxkpi_80211.h"
#include "components/linuxkpi/fw_registry.h"

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */

static struct {
	struct ieee80211_hw *hw;
	struct ieee80211_vif *vif;
	struct ieee80211_sta *sta;	/* the one peer of a STA image */
	int sta_preauth;		/* the peer was registered when the
					 * connection was prepared (before AUTH),
					 * so it winds down as an unassociated
					 * station, not through sta_remove */
	uint8_t bssid_store[6];		/* the pre-assoc BSSID the vif points at */
	int vif_added;
	int installed;
	unsigned int filter_calls;
	unsigned int tx_calls;
	unsigned int key_calls;
} adapter;

/* ------------------------------------------------------------------ */
/* seam vtable: the net80211 half's asks                               */
/* ------------------------------------------------------------------ */

static int
seam_start(struct wlan_seam_drv *drv)
{

	(void)drv;
	if (adapter.hw == NULL)
		return -1;
	return lkpi80211_drv_start(adapter.hw);
}

static int
seam_stop(struct wlan_seam_drv *drv)
{

	(void)drv;
	if (adapter.hw == NULL)
		return -1;
	lkpi80211_drv_stop(adapter.hw, false);
	return 0;
}

static int
seam_add_interface(struct wlan_seam_drv *drv, const uint8_t mac[6], int iftype)
{

	(void)drv;
	if (adapter.hw == NULL)
		return -1;
	/*
	 * The net80211 half attaches first and then tells the driver side to add
	 * its interface, so the address it already holds is the one handed here.
	 * The vif (and its driver-private area) is the substitute's object; the
	 * type maps one-to-one (the seam's station/ap values are the nl80211
	 * numbering the vif carries).
	 */
	adapter.vif = lkpi80211_drv_vif_add(adapter.hw, (uint8_t)iftype, mac);
	if (adapter.vif == NULL)
		return -1;
	adapter.vif_added = 1;
	return 0;
}

static int
seam_config(struct wlan_seam_drv *drv, uint32_t changed, uint32_t freq)
{
	uint8_t hw_value;

	(void)drv;
	if (adapter.hw == NULL)
		return -1;
	if ((changed & WLAN_SEAM_CONFIG_CHANNEL) == 0 || freq == 0)
		return 0;
	/* 2.4 GHz: the channel's hardware value is its IEEE number */
	if (freq < 2412 || freq > 2472)
		return -1;
	hw_value = (uint8_t)((freq - 2407) / 5);
	return lkpi80211_drv_config_channel(adapter.hw, freq, hw_value,
	    NL80211_BAND_2GHZ, NL80211_CHAN_WIDTH_20, 0);
}

static int
seam_filter(struct wlan_seam_drv *drv, uint32_t changed, uint32_t total)
{

	(void)drv;
	if (adapter.hw == NULL)
		return -1;
	adapter.filter_calls++;
	/* the seam's filter bits are the mac80211 FIF_* selector numbering, and
	 * the driver's configure_filter reads exactly those, so both words travel
	 * unchanged (the Linux core passes them the same way); the driver may
	 * narrow the set and the substitute records what it kept */
	return lkpi80211_drv_filter(adapter.hw, changed, total);
}

static int
seam_scan_start(struct wlan_seam_drv *drv)
{

	(void)drv;
	if (adapter.hw == NULL)
		return -1;
	return lkpi80211_drv_scan_start(adapter.hw);
}

static void
seam_scan_complete(struct wlan_seam_drv *drv)
{

	(void)drv;
	if (adapter.hw != NULL)
		lkpi80211_drv_scan_complete(adapter.hw);
}

/*
 * The hardware-scan route's three calls. The capability question is answered
 * from the driver's own ops table (the substitute reads it), so the bridge's
 * route choice follows the driver rather than a per-image build flag.
 */
static int
seam_has_hw_scan(struct wlan_seam_drv *drv)
{

	(void)drv;
	if (adapter.hw == NULL)
		return 0;
	return lkpi80211_drv_has_hw_scan(adapter.hw);
}

static int
seam_hw_scan_start(struct wlan_seam_drv *drv, const uint32_t *freqs,
    size_t n_freqs)
{

	(void)drv;
	if (adapter.hw == NULL)
		return -1;
	return lkpi80211_drv_hw_scan(adapter.hw, freqs,
	    (unsigned int)n_freqs);
}

static void
seam_hw_scan_cancel(struct wlan_seam_drv *drv)
{

	(void)drv;
	if (adapter.hw != NULL)
		lkpi80211_drv_hw_scan_cancel(adapter.hw);
}

static int
seam_tx(struct wlan_seam_drv *drv, const uint8_t *frame, size_t len,
    int is_mgmt)
{
	struct sk_buff *skb;
	unsigned char *p;
	struct ieee80211_tx_info *info;
	unsigned int headroom;

	(void)drv;
	if (adapter.hw == NULL || frame == NULL || len == 0)
		return -1;
	/*
	 * The driver's tx path expects a socket buffer whose control block it
	 * fills (the rate keys, the AC, the key) and whose headroom has room for
	 * its own transmit descriptor - it pushes that on and checks the headroom
	 * itself (core.c:5477/:5506). That headroom is a promise mac80211 makes
	 * to every driver: each frame handed to the driver's tx op carries
	 * hw->extra_tx_headroom bytes, a number the driver declares when it
	 * registers (rtw89: IEEE80211_HT_CTL_LEN + its txwd body and info = 52).
	 * Reserve it the way mac80211 does - allocate the frame plus the headroom,
	 * then move the data pointer past it - so the driver's descriptor push
	 * always fits instead of being refused at the pool's headroom guard.
	 */
	headroom = adapter.hw->extra_tx_headroom;
	skb = lkpi_dev_alloc_skb((unsigned int)len + headroom, GFP_ATOMIC);
	if (skb == NULL)
		return -1;
	lkpi_skb_reserve(skb, headroom);
	p = lkpi_skb_put(skb, (unsigned int)len);
	memcpy(p, frame, len);

	info = (struct ieee80211_tx_info *)skb->cb;
	memset(info, 0, sizeof(*info));
	if (is_mgmt) {
		/* management frames: the driver's rate-control key and the vif
		 * are what its descriptor build reads (core.c:5467, :5290) */
		info->control.vif = adapter.vif;
		info->control.rates[0].idx = 0;	/* first supported rate */
		info->control.rates[0].count = 1;
	} else {
		/* data frames: the peer station must be known to the driver's
		 * rate path; without one the frame is dropped with a log rather
		 * than handed to a half-initialised descriptor */
		info->control.vif = adapter.vif;
		if (adapter.sta == NULL) {
			lkpi_skb_free(skb);
			lkpi_printf(ULOG_ERROR_LEVEL, "wlans: tx of a data frame with no peer\n");
			return -1;
		}
		info->control.rates[0].idx = 0;
		info->control.rates[0].count = 1;
	}
	adapter.tx_calls++;
	if (is_mgmt) {
		/*
		 * Management frames take the interface's own transmit handoff:
		 * the txq selection routes them through the station's management
		 * queue when the driver declared one, and through .tx when no
		 * queue owns them.  A Linux-family driver may drop them in the
		 * .tx route (the AC 7260's does, silently, on a station vif),
		 * which is why the selection cannot be skipped for them.
		 */
		lkpi80211_tx(adapter.hw, adapter.vif, adapter.sta, skb);
	} else {
		lkpi80211_drv_tx(adapter.hw, adapter.vif, adapter.sta, skb);
	}
	/* the driver owns the skb from here (its completion path releases it
	 * through the substitute's tx-status entry) */
	return (int)len;
}

static int
seam_set_key(struct wlan_seam_drv *drv, int pairwise, int key_idx, int cipher,
    const uint8_t *key, size_t key_len)
{
	struct ieee80211_key_conf *kc;
	uint32_t mac_cipher;
	int ret;

	(void)drv;
	if (adapter.hw == NULL || adapter.vif == NULL)
		return -1;
	/*
	 * The key object the driver's set_key reads: cipher, index, length and
	 * the material (core.c:7063). One object per call, released after the
	 * driver had its chance to copy what it needs (the driver programs the
	 * hardware and does not retain the pointer).
	 */
	switch (cipher) {
	case WLAN_SEAM_CIPHER_WEP:
		mac_cipher = WLAN_CIPHER_SUITE_WEP40;
		break;
	case WLAN_SEAM_CIPHER_TKIP:
		mac_cipher = WLAN_CIPHER_SUITE_TKIP;
		break;
	case WLAN_SEAM_CIPHER_CCMP:
		mac_cipher = WLAN_CIPHER_SUITE_CCMP;
		break;
	default:
		return -1;
	}
	kc = kzalloc(sizeof(*kc) + key_len, GFP_KERNEL);
	if (kc == NULL)
		return -1;
	kc->cipher = mac_cipher;
	kc->keyidx = (s8)key_idx;
	kc->keylen = (u8)key_len;
	memcpy(kc->key, key, key_len);
	if (pairwise) {
		kc->flags |= IEEE80211_KEY_FLAG_PAIRWISE;
	}
	adapter.key_calls++;
	ret = lkpi80211_drv_key(adapter.hw, SET_KEY, adapter.vif, adapter.sta,
	    kc);
	kfree(kc);
	return ret;
}

static int
seam_clear_key(struct wlan_seam_drv *drv, int pairwise, int key_idx)
{
	struct ieee80211_key_conf kc;
	int ret;

	(void)drv;
	if (adapter.hw == NULL || adapter.vif == NULL)
		return -1;
	memset(&kc, 0, sizeof(kc));
	if (pairwise)
		kc.flags |= IEEE80211_KEY_FLAG_PAIRWISE;
	kc.keyidx = (s8)key_idx;
	adapter.key_calls++;
	ret = lkpi80211_drv_key(adapter.hw, DISABLE_KEY, adapter.vif, adapter.sta,
	    &kc);
	return ret;
}

static void
seam_bss_info(struct wlan_seam_drv *drv, int joined, const uint8_t bssid[6],
    uint32_t freq, uint16_t aid, const uint8_t *rates, size_t rates_len)
{
	u64 changed = 0;

	(void)drv;
	(void)freq;
	if (adapter.hw == NULL || adapter.vif == NULL)
		return;
	/*
	 * The associated-state notification, carrying what the driver's
	 * association branch actually reads: vif->cfg.assoc gates the branch,
	 * vif->cfg.aid lands in the beacon-power-save report register, and the
	 * station's rate bitmaps feed the rate-adaptation mask (core.c reads
	 * all three; without cfg.assoc the driver runs its NOT-associated leg
	 * - TSF update disabled, disconnect reported - while the stack has
	 * already associated).
	 */
	if (joined) {
		if (bssid != NULL)
			memcpy(adapter.bssid_store, bssid, 6);
		adapter.vif->bss_conf.bssid = adapter.bssid_store;
		adapter.vif->bss_conf.assoc_capability = 0;
		adapter.vif->cfg.assoc = true;
		adapter.vif->cfg.aid = aid;
		/*
		 * The AP's address also goes into the vif's own config, which is
		 * where the interface keeps it for a station (mlme.c:7210 copies
		 * it there while the association request is built).  A driver
		 * that has to find its own station from the vif reads it from
		 * there: rtw89's association handler looks the peer up by
		 * vif->cfg.ap_addr and gives up when it is still zero.
		 */
		if (bssid != NULL)
			memcpy(adapter.vif->cfg.ap_addr, adapter.bssid_store, 6);
		changed = BSS_CHANGED_ASSOC | BSS_CHANGED_BSSID |
		    BSS_CHANGED_BASIC_RATES;
		if (adapter.sta == NULL) {
			/*
			 * No prepared peer: a connection that never passed the
			 * bridge's AUTH entry (or a caller that jumps straight to
			 * the association).  Register it here, as this seam always
			 * did; the ladder's AUTH -> ASSOC step below completes it.
			 */
			adapter.sta = lkpi80211_drv_sta_add(adapter.hw,
			    adapter.vif, adapter.bssid_store, aid);
		}
		if (adapter.sta != NULL) {
			/*
			 * The driver's rate bitmaps index its own registered
			 * rate table (core.c rtl8xxxu_rates: 1,2,5.5,11 then
			 * 6..54 Mbit/s, twelve entries - the same order every
			 * mac80211 band bitmap uses), NOT the on-air half-
			 * megabit value, so the transfer goes through a table;
			 * the 0x80 flag on a rate marks the AP's basic set
			 * (bss_conf->basic_rates feeds the driver's
			 * response-rate register).
			 */
			static const struct { uint8_t half_mbps; uint8_t idx; }
			    legacy[] = {
			    { 2, 0 }, { 4, 1 }, { 11, 2 }, { 22, 3 },
			    { 12, 4 }, { 18, 5 }, { 24, 6 }, { 36, 7 },
			    { 48, 8 }, { 72, 9 }, { 96, 10 }, { 108, 11 },
			};
			u32 supp = 0, basic = 0;
			size_t i, j;

			for (i = 0; i < rates_len; i++) {
				for (j = 0; j < sizeof(legacy)/sizeof(legacy[0]);
				    j++) {
					if ((rates[i] & 0x7fu) ==
					    legacy[j].half_mbps) {
						supp |= 1u << legacy[j].idx;
						if ((rates[i] & 0x80u) != 0)
							basic |=
							    1u << legacy[j].idx;
						break;
					}
				}
			}
			adapter.sta->deflink.supp_rates[0] = supp;
			adapter.vif->bss_conf.basic_rates = basic;
			/*
			 * The association step of the station ladder, now that the
			 * association actually happened.  It is separate from the
			 * upload (which the AUTH entry performed) because the
			 * interface runs the two at the two different moments - and
			 * a driver acts on both: iwlwifi's station setup needs the
			 * peer uploaded to transmit the authentication request, and
			 * its association handler needs this transition to program
			 * the association.
			 */
			(void)lkpi80211_drv_sta_assoc(adapter.hw, adapter.vif,
			    adapter.sta, aid);
		}
		/* the peer is associated now (or was never registered): its
		 * wind-down is the joined station's, not the pre-auth one's */
		adapter.sta_preauth = 0;
	} else {
		adapter.vif->cfg.assoc = false;
		adapter.vif->cfg.aid = 0;
		/* the AP's address goes with the association (mlme.c:2903) */
		memset(adapter.vif->cfg.ap_addr, 0, 6);
		changed = BSS_CHANGED_ASSOC;
		if (adapter.sta != NULL) {
			lkpi80211_drv_sta_remove(adapter.hw, adapter.vif,
			    adapter.sta);
			adapter.sta = NULL;
		}
		adapter.sta_preauth = 0;
		/*
		 * No BSS_CHANGED_BSSID on the leave: mac80211 does not push a
		 * NULL BSSID through bss_info_changed, and the driver's
		 * handler dereferences the pointer to program the register -
		 * a NULL there faults. The local view is cleared (so nothing
		 * matches the old AP by address), and the register keeps its
		 * last value until the next join reprograms it.
		 */
		adapter.vif->bss_conf.bssid = NULL;
	}
	lkpi80211_drv_bss_changed(adapter.hw, adapter.vif, changed);
}

/*
 * Pre-association BSSID programming (see seam.h): the chip's receive filter
 * compares against this register, so the AP's address must be in it before the
 * auth request leaves - the driver's BSS_CHANGED_BSSID handler is its
 * programming point (core.c:4989).
 */
static int
seam_bssid(struct wlan_seam_drv *drv, const uint8_t bssid[6])
{

	(void)drv;
	if (adapter.hw == NULL || adapter.vif == NULL)
		return -1;
	if (bssid == NULL) {
		adapter.vif->bss_conf.bssid = NULL;
		/*
		 * The connection attempt ended before the association: the peer
		 * comes off the driver, the way mac80211 destroys the AP's
		 * station when the authentication fails.  A peer that reached
		 * the association is wound down by the leave notification
		 * instead (its ladder step is the joined station's).
		 */
		if (adapter.sta != NULL && adapter.sta_preauth) {
			lkpi80211_drv_sta_free(adapter.hw, adapter.vif,
			    adapter.sta);
			adapter.sta = NULL;
			adapter.sta_preauth = 0;
		}
		/*
		 * No BSS_CHANGED_BSSID on the way out - the same contract the
		 * leave path above documents: a driver's handler dereferences
		 * the BSSID pointer to program its register, and this one is
		 * NULL.  The register keeps its last value until the next join
		 * reprograms it.
		 */
		return 0;
	}
	memcpy(adapter.bssid_store, bssid, 6);
	adapter.vif->bss_conf.bssid = adapter.bssid_store;
	/*
	 * The peer station, before the stack sends anything to it: this is the
	 * interface's prepare-a-connection moment - mac80211 inserts and uploads
	 * the AP's station before the authentication request leaves - and it is
	 * what lets the transmit handoff find the station's management queue for
	 * that request.  A driver that routes a station's management frames
	 * through the queue (the AC 7260's does, and drops them in .tx
	 * otherwise) needs the station here, not at the association.
	 */
	if (adapter.sta == NULL) {
		adapter.sta = lkpi80211_drv_sta_add(adapter.hw, adapter.vif,
		    bssid, 0);
		if (adapter.sta != NULL)
			adapter.sta_preauth = 1;
	}
	lkpi80211_drv_bss_changed(adapter.hw, adapter.vif,
	    BSS_CHANGED_BSSID);
	return 0;
}

static const struct wlan_seam_drv_ops seam_ops = {
	.start = seam_start,
	.stop = seam_stop,
	.add_interface = seam_add_interface,
	.config = seam_config,
	.filter = seam_filter,
	.sw_scan_start = seam_scan_start,
	.sw_scan_complete = seam_scan_complete,
	.has_hw_scan = seam_has_hw_scan,
	.hw_scan_start = seam_hw_scan_start,
	.hw_scan_cancel = seam_hw_scan_cancel,
	.tx = seam_tx,
	.set_key = seam_set_key,
	.clear_key = seam_clear_key,
	.bss_info = seam_bss_info,
	.bssid = seam_bssid,
};

/* ------------------------------------------------------------------ */
/* the substitute's upward bridge hooks                                */
/* ------------------------------------------------------------------ */

static int
up_attach(struct ieee80211_hw *hw)
{
	uint8_t mac[6];
	unsigned int i;

	adapter.hw = hw;
	/*
	 * The interface address: the driver installed the permanent address on
	 * the wiphy before registering (SET_IEEE80211_PERM_ADDR, core.c:7925),
	 * so the net80211 side takes that address rather than inventing one.
	 */
	for (i = 0; i < 6; i++)
		mac[i] = hw->wiphy->perm_addr[i];
	return wlan_bridge_attach((struct wlan_seam_drv *)&adapter, &seam_ops,
	    mac);
}

static void
up_detach(struct ieee80211_hw *hw)
{

	(void)hw;
	wlan_bridge_detach();
	adapter.hw = NULL;
	adapter.vif = NULL;
	adapter.sta = NULL;
	adapter.sta_preauth = 0;
	adapter.vif_added = 0;
}

static void
up_rx(struct ieee80211_hw *hw, struct ieee80211_rx_status *rs,
    struct sk_buff *skb)
{
	static unsigned int traced;
	static unsigned int authassoc_traced;

	(void)hw;
	if (skb == NULL)
		return;
	/*
	 * The driver boundary's own reading, for the first few beacons/probe
	 * responses: the receive status the driver filled in.  `freq` is the field
	 * the bridge now carries across to net80211 (the parser's off-channel
	 * check needs the frame's own channel, not the one the dwell has since
	 * moved to); `band`/`encoding`/`rate_idx` are printed because a zero or
	 * nonsense value here would mean the status block is not being populated
	 * at all - a different fault from the channel one, and one no later
	 * counter can distinguish.
	 */
	if (traced < 6 && skb->len >= 24 &&
	    (skb->data[0] & 0x0c) == 0x00 && (skb->data[0] & 0xf0) == 0x80) {
		traced++;
		wlan_osal_log("wlan: rxstatus len=%u freq=%u band=%u signal=%d "
		    "enc=%u rate=%u bw=%u flag=0x%x\n", (unsigned)skb->len,
		    rs != NULL ? (unsigned)rs->freq : 0u,
		    rs != NULL ? (unsigned)rs->band : 0u,
		    rs != NULL ? (int)rs->signal : 0,
		    rs != NULL ? (unsigned)rs->encoding : 0u,
		    rs != NULL ? (unsigned)rs->rate_idx : 0u,
		    rs != NULL ? (unsigned)rs->bw : 0u,
		    rs != NULL ? (unsigned)rs->flag : 0u);
	}
	/*
	 * The association exchange's own boundary reading, every frame: the
	 * frame-control pair (byte 1 carries the Retry bit, which is what says
	 * whether a burst of identical answers is the AP's MAC retransmitting
	 * because it got no ACK from us - i.e. the chip did not accept them),
	 * the sequence control, and the chip's free-run receive timestamp
	 * (`mactime`), which dates the frame on the air independently of the
	 * host clock.  Bounded so a retry storm cannot flood the console.
	 */
	if (skb->len >= 24 && (skb->data[0] & 0x0c) == 0x00) {
		uint8_t sub = (uint8_t)(skb->data[0] >> 4);

		if ((sub <= 3 || sub == 0xb) && authassoc_traced < 24) {
			authassoc_traced++;
			wlan_osal_log("wlan: rxmgt fc=%02x%02x retry=%u sc=%04x "
			    "mactime=%u freq=%u len=%u\n", skb->data[0],
			    skb->data[1], (skb->data[1] & 0x08) != 0,
			    (unsigned)(skb->data[22] | (skb->data[23] << 8)),
			    rs != NULL ? (unsigned)(rs->mactime & 0xffffffffu) : 0u,
			    rs != NULL ? (unsigned)rs->freq : 0u,
			    (unsigned)skb->len);
		}
	}
	(void)wlan_bridge_input(skb->data, (size_t)skb->len,
	    rs != NULL ? (int)rs->signal : 0,
	    rs != NULL && (rs->flag & RX_FLAG_FAILED_FCS_CRC) != 0,
	    rs != NULL && (rs->flag & RX_FLAG_DECRYPTED) != 0,
	    rs != NULL ? rs->freq : 0);
}

static void
up_tx_complete(struct ieee80211_hw *hw, struct sk_buff *skb)
{

	(void)hw;
	(void)skb;	/* the substitute releases it after this returns */
	wlan_bridge_tx_complete();
}

/*
 * The driver reported its hardware sweep finished. This is the hw_scan route's
 * only end-of-sweep signal: the bridge drove no channel walk to run out, so the
 * window has to be closed from here.
 */
static void
up_scan_complete(struct ieee80211_hw *hw)
{

	(void)hw;
	wlan_bridge_scan_hw_complete();
}

static const struct lkpi80211_bridge_ops up_ops = {
	.attach = up_attach,
	.detach = up_detach,
	.rx = up_rx,
	.tx_complete = up_tx_complete,
	.scan_complete = up_scan_complete,
};

/* ------------------------------------------------------------------ */
/* firmware lookup (the registry is a linuxkpi component)              */
/* ------------------------------------------------------------------ */

int
wlan_bridge_fw_lookup(const char *name, const uint8_t **data, size_t *size)
{
	const struct lkpi_fw_entry *e;

	if (name == NULL || data == NULL || size == NULL)
		return -1;
	e = lkpi_fw_registry_find(name);
	if (e == NULL)
		return -1;
	*data = e->data;
	*size = e->size;
	return 0;
}

/* ------------------------------------------------------------------ */
/* start-up                                                            */
/* ------------------------------------------------------------------ */

int
wlan_seam_adapter_init(void)
{

	if (adapter.installed)
		return 0;
	memset(&adapter, 0, sizeof(adapter));
	lkpi80211_bridge_register(&up_ops, NULL);
	adapter.installed = 1;
	lkpi_printf(ULOG_INFO_LEVEL, "wlan: seam adapter ready\n");
	return 0;
}
