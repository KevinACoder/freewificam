/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    fake_driver.c
 * @brief   The scripted driver behind the bridge's seam: it records every ops
 *          call the bridge makes and answers the ones the scan and join flow
 *          needs, so the whole bridge chain (net80211 -> bridge -> substitute ->
 *          ops) runs on the host without a chip.
 *
 * @design test double behind the adapter seam. The bridge reaches its driver
 *         through the mac80211 substitute's downward helpers, which call the
 *         ops table the driver registered; this file registers such a table
 *         itself, so the chain under test is the real one (real net80211, real
 *         bridge, real substitute) and only the chip at the far end is scripted.
 *         What the double answers is deliberately small: the scan group's
 *         filter/scan calls, the interface add, the channel config, the transmit
 *         capture (which the harness answers with the fake AP's frames) and the
 *         key/bss calls. The register-level 8188eu model is NOT here - the real
 *         driver's probe runs on the board (the acceptance criterion for the
 *         board phase), and a hand-built register model on the host would test
 *         our guesses about the chip rather than our code.
 *         Reference for the shape: the sibling substitute's scripted responder
 *         (tests/host/net80211-frame-test/vwifi.c) and the driver substitute of
 *         the USB round (tests/host/linuxkpi-usb-test/usb_msc_driver.c).
 * @date    08.10.2026
 */

#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"
#include <net/mac80211.h>
#include <net/cfg80211.h>
#include <linux/ieee80211.h>

#include "fake_driver.h"

/* the bridge's sweep-bookkeeping read, declared here as the suite declares the
 * other bridge entries it reaches (this file is the linuxkpi world and must not
 * pull the net80211-world header) */
int	wlan_bridge_scan_pending(void);

/* ------------------------------------------------------------------ */
/* the recorder                                                        */
/* ------------------------------------------------------------------ */

static struct fake_driver_state fake;

/*
 * The channel table a driver advertises at registration. The hardware-scan
 * request builder resolves each frequency the bridge hands down through the
 * wiphy's own band tables (ieee80211_get_channel), so a driver without them
 * would get an empty request and the hardware route would fail for a reason
 * that is the harness's, not the chain's. The set is the stack's own 2.4 GHz
 * table (2412..2472, 13 channels), the same one the bridge's software walk
 * visits.
 */
#define FAKE_DRIVER_BAND_CHANS	13
static struct ieee80211_channel fake_band_channels[FAKE_DRIVER_BAND_CHANS];
static struct ieee80211_supported_band fake_band = {
	.channels = fake_band_channels,
	.band = NL80211_BAND_2GHZ,
	.n_channels = FAKE_DRIVER_BAND_CHANS,
};

void
fake_driver_reset(void)
{

	memset(&fake, 0, sizeof(fake));
}

const struct fake_driver_state *
fake_driver_state(void)
{

	return &fake;
}

/* the sweep-bookkeeping observation is sticky, so a case that wants it scoped to
 * one call clears it first (a walk's own transition legitimately runs with a
 * sweep in flight; only the scan-start transition must not) */
void
fake_driver_reset_pending_probe(void)
{

	fake.pending_seen_in_tx = 0;
}

void
fake_driver_knob_filter_refuses(int on)
{

	fake.filter_refuses = on != 0;
}

/*
 * The double-free fault: the n-th frame the driver is handed is released twice.
 * The pool answers a double free with a named refusal (its counter and its
 * tombstone), so the fault surfaces through the harness's balance case instead
 * of corrupting the run.
 */
void
fake_driver_knob_double_free_tx(unsigned int nth)
{

	fake.double_free_tx = 1;
	fake.double_free_at = nth;
}

static void
order_add(enum fake_call call)
{

	if (fake.order_count < FAKE_DRIVER_ORDER_MAX)
		fake.order[fake.order_count] = call;
	fake.order_count++;
}

static void
record_tx(struct sk_buff *skb, int is_mgmt)
{

	if (fake.tx_count >= FAKE_DRIVER_TX_MAX) {
		fake.tx_lost++;
		return;
	}
	{
		unsigned int len = skb->len;

		if (len > FAKE_DRIVER_FRAME_MAX)
			len = FAKE_DRIVER_FRAME_MAX;
		memcpy(fake.tx_frames[fake.tx_count], skb->data, len);
		fake.tx_lens[fake.tx_count] = len;
		fake.tx_is_mgmt[fake.tx_count] = (unsigned int)is_mgmt;
		fake.tx_count++;
	}
}

/* the station ladder: the recorder the peer-registration case reads.  What
 * matters is the moment, not the shape: how many management frames had already
 * been handed over when the driver heard each transition - the authentication
 * request must find the station (mac80211 uploads the AP's station while it
 * prepares the connection), and the association step comes after it. */
static int
fake_sta_state(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_sta *sta, enum ieee80211_sta_state old_state,
    enum ieee80211_sta_state new_state)
{

	(void)hw;
	(void)vif;
	(void)sta;
	if (fake.sta_state_calls < FAKE_DRIVER_STA_MAX) {
		fake.sta_state_old[fake.sta_state_calls] = (int)old_state;
		fake.sta_state_new[fake.sta_state_calls] = (int)new_state;
		fake.sta_state_mgmt_at[fake.sta_state_calls] =
		    fake.tx_mgmt_calls;
	}
	fake.sta_state_calls++;
	order_add(FAKE_CALL_STA_STATE);
	return 0;
}

/* ------------------------------------------------------------------ */
/* the ops table                                                       */
/* ------------------------------------------------------------------ */

static int
fake_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	unsigned int i;

	/*
	 * A driver declares its transmit-descriptor headroom before it is handed
	 * frames, the way a real one does at registration (rtw89: core.c:7481).
	 * The bridge reads this field when it builds each frame.
	 */
	hw->extra_tx_headroom = FAKE_DRIVER_TX_HEADROOM;
	/*
	 * The wiphy band table, which a real driver fills in before registering.
	 * The hardware-scan request builder resolves the bridge's frequencies
	 * through it (ieee80211_get_channel), so the shape has to be here for that
	 * route to have a request to build at all.
	 */
	for (i = 0; i < FAKE_DRIVER_BAND_CHANS; i++) {
		fake_band_channels[i].band = NL80211_BAND_2GHZ;
		fake_band_channels[i].center_freq = 2412 + i * 5;
		fake_band_channels[i].hw_value = (u16)(i + 1);
	}
	hw->wiphy->bands[NL80211_BAND_2GHZ] = &fake_band;
	(void)vif;
	fake.add_interface_calls++;
	order_add(FAKE_CALL_ADD_INTERFACE);
	return 0;
}

static int
fake_config(struct ieee80211_hw *hw, int radio_idx, u32 changed)
{

	(void)radio_idx;
	fake.config_calls++;
	/*
	 * The channel config runs inside the stack's state transition (the bridge
	 * points the radio at the channel it is about to use, before the stack's
	 * own handler).  That makes this callback the one place the rest of the
	 * system can be observed *during* a scan start - which is what the sweep's
	 * bookkeeping flag must not be visible from yet: the dwell pump gates on
	 * that flag, and a flag raised before the transition lets the pump close a
	 * sweep whose window has not opened.
	 */
	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		if (hw->conf.chandef.chan != NULL) {
			fake.last_freq = hw->conf.chandef.chan->center_freq;
			fake.last_channel_hw = hw->conf.chandef.chan->hw_value;
			if (fake.config_freq_count < FAKE_DRIVER_CONFIG_MAX)
				fake.config_freqs[fake.config_freq_count++] =
				    fake.last_freq;
		}
	}
	order_add(FAKE_CALL_CONFIG);
	return 0;
}

static void
fake_bss_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_bss_conf *info, u64 changed)
{

	(void)hw;
	(void)vif;
	fake.bss_info_calls++;
	fake.last_bss_joined = (changed & BSS_CHANGED_ASSOC) != 0 &&
	    info->bssid != NULL;
	if (fake.bss_ev_count < FAKE_DRIVER_BSS_EV_MAX) {
		struct fake_bss_ev *ev = &fake.bss_ev[fake.bss_ev_count++];

		ev->changed = changed;
		ev->assoc = vif->cfg.assoc;
		ev->aid = vif->cfg.aid;
		memcpy(ev->ap_addr, vif->cfg.ap_addr, 6);
		ev->bssid_null = info->bssid == NULL;
		if (info->bssid != NULL)
			memcpy(ev->bssid, info->bssid, 6);
		ev->basic = info->basic_rates;
		if (info->bssid != NULL) {
			struct ieee80211_sta *sta;

			rcu_read_lock();
			sta = ieee80211_find_sta(vif, info->bssid);
			if (sta != NULL)
				ev->supp0 = sta->deflink.supp_rates[0];
			rcu_read_unlock();
		}
	}
	if (info->bssid != NULL)
		memcpy(fake.last_bssid, info->bssid, 6);
	else
		memset(fake.last_bssid, 0, 6);
	order_add(FAKE_CALL_BSS_INFO);
}

static void
fake_configure_filter(struct ieee80211_hw *hw, unsigned int changed_flags,
    unsigned int *total_flags, u64 multicast)
{

	(void)hw;
	(void)multicast;
	fake.filter_calls++;
	fake.filter_changed = changed_flags;
	fake.filter_total = *total_flags;
	fake.filter_last_total = *total_flags;
	if (*total_flags == 0)
		fake.filter_completes++;
	if (fake.filter_refuses) {
		*total_flags = 0;
		order_add(FAKE_CALL_FILTER_REFUSED);
		return;
	}
	order_add(FAKE_CALL_FILTER);
}

static void
fake_sw_scan_start(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    const u8 *mac_addr)
{

	(void)hw;
	(void)vif;
	(void)mac_addr;
	fake.scan_start_calls++;
	order_add(FAKE_CALL_SCAN_START);
}

static void
fake_sw_scan_complete(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{

	(void)hw;
	(void)vif;
	fake.scan_complete_calls++;
	order_add(FAKE_CALL_SCAN_COMPLETE);
}

/*
 * The hardware-scan op (the shape an AC 7260-class driver registers: hw_scan
 * plus cancel_hw_scan, no sw_scan_start). The request object is the one the
 * substitute built, and it is live for the duration of this call - the driver
 * reads its channel list here, which is exactly what a real firmware-scan
 * driver does when it packs the request for the firmware.
 */
static int
fake_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_scan_request *req)
{
	unsigned int i;

	(void)hw;
	(void)vif;
	fake.hw_scan_calls++;
	order_add(FAKE_CALL_HW_SCAN);
	if (req != NULL) {
		fake.hw_scan_n_channels = req->req.n_channels;
		fake.hw_scan_n_ssids = (unsigned int)req->req.n_ssids;
		fake.hw_scan_req_readable = 1;
		for (i = 0; i < req->req.n_channels &&
		    i < FAKE_DRIVER_HW_SCAN_MAX; i++) {
			const struct ieee80211_channel *c = req->req.channels[i];

			if (c == NULL) {
				fake.hw_scan_req_readable = 0;
				continue;
			}
			fake.hw_scan_freqs[i] = c->center_freq;
		}
	}
	return 0;
}

static void
fake_cancel_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{

	(void)hw;
	(void)vif;
	fake.hw_scan_cancel_calls++;
	order_add(FAKE_CALL_HW_SCAN_CANCEL);
}

static int
fake_start(struct ieee80211_hw *hw)
{

	(void)hw;
	fake.start_calls++;
	order_add(FAKE_CALL_START);
	return 0;
}

static void
fake_stop(struct ieee80211_hw *hw, bool suspend)
{

	(void)hw;
	(void)suspend;
	fake.stop_calls++;
	order_add(FAKE_CALL_STOP);
}

/*
 * The transmit capture: the bridge hands a frame over, the harness has already
 * queued the fake AP's answer (when the frame is one the AP replies to), and the
 * answer is injected back through the substitute's rx entry - which is the same
 * path a real chip's receive would take, so the chain closes on itself exactly
 * as it does on the board.
 */
static void
fake_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
    struct sk_buff *skb)
{

	(void)control;
	fake.tx_calls++;
	/*
	 * The probe a scan start builds is transmitted from inside the state
	 * transition that opens the window (the sweep's first channel is armed and
	 * probed inside it), which makes this the one place the rest of the system
	 * can be observed *during* that transition - and the sweep's bookkeeping
	 * flag must not be visible from there yet: the dwell pump gates on it, and
	 * a flag raised before the transition lets the pump close a sweep whose
	 * window has not opened.
	 */
	if (wlan_bridge_scan_pending())
		fake.pending_seen_in_tx = 1;
	/* the headroom the frame arrived with: a driver's descriptor push depends on
	 * it, and the bridge owes it per frame (see FAKE_DRIVER_TX_HEADROOM) */
	{
		unsigned int hr = skb_headroom(skb);

		if (fake.tx_calls == 1 || hr < fake.tx_headroom_min)
			fake.tx_headroom_min = hr;
		if (hr < hw->extra_tx_headroom)
			fake.tx_headroom_bad++;
	}
	if (skb->len >= 1 &&
	    (skb->data[0] & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT)
		fake.tx_mgmt_calls++;
	record_tx(skb, skb->len >= 1 &&
	    (skb->data[0] & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT);
	order_add(FAKE_CALL_TX);

	/* the driver releases every frame it is handed, whichever path it takes
	 * (the completion path would hand it back to the substitute; here the
	 * frame is consumed, so the pool's pairing stays exact) */
	lkpi_skb_free(skb);
	if (fake.double_free_tx && fake.tx_calls == fake.double_free_at)
		lkpi_skb_free(skb);
}

static void
fake_wake_tx_queue(struct ieee80211_hw *hw, struct ieee80211_txq *txq)
{

	(void)hw;
	(void)txq;
	order_add(FAKE_CALL_WAKE_TXQ);
}

static int
fake_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
    struct ieee80211_vif *vif, struct ieee80211_sta *sta,
    struct ieee80211_key_conf *key)
{

	(void)hw;
	(void)vif;
	(void)sta;
	if (cmd == SET_KEY) {
		fake.set_key_calls++;
		fake.last_key_pairwise =
		    (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) != 0;
		fake.last_key_idx = key->keyidx;
		fake.last_key_cipher = (int)key->cipher;
		fake.last_key_len = key->keylen;
		order_add(FAKE_CALL_SET_KEY);
	} else {
		fake.clear_key_calls++;
		fake.last_key_pairwise =
		    (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) != 0;
		fake.last_key_idx = key->keyidx;
		order_add(FAKE_CALL_CLEAR_KEY);
	}
	return 0;
}

/*
 * The ops table is mutable on purpose: the suite switches this same object
 * between the software shape and the hardware one (fake_driver_knob_hw_scan),
 * and the substitute holds the pointer, so the route the bridge reads follows
 * the switch. A const table would need a second registration to change shape.
 */
static struct ieee80211_ops fake_ops = {
	.tx = fake_tx,
	.wake_tx_queue = fake_wake_tx_queue,
	.add_interface = fake_add_interface,
	.config = fake_config,
	.bss_info_changed = fake_bss_info_changed,
	.sta_state = fake_sta_state,
	.configure_filter = fake_configure_filter,
	.start = fake_start,
	.stop = fake_stop,
	.sw_scan_start = fake_sw_scan_start,
	.sw_scan_complete = fake_sw_scan_complete,
	.set_key = fake_set_key,
};

const struct ieee80211_ops *
fake_driver_ops(void)
{

	return &fake_ops;
}

/*
 * The route switch. `on` gives the driver the AC 7260's mvm shape - hw_scan and
 * cancel_hw_scan present, sw_scan_start/sw_scan_complete absent - so the bridge
 * has no software route to take; off restores the software shape the other
 * cases run on.
 */
void
fake_driver_knob_hw_scan(int on)
{

	if (on) {
		fake_ops.hw_scan = fake_hw_scan;
		fake_ops.cancel_hw_scan = fake_cancel_hw_scan;
		fake_ops.sw_scan_start = NULL;
		fake_ops.sw_scan_complete = NULL;
	} else {
		fake_ops.hw_scan = NULL;
		fake_ops.cancel_hw_scan = NULL;
		fake_ops.sw_scan_start = fake_sw_scan_start;
		fake_ops.sw_scan_complete = fake_sw_scan_complete;
	}
}
