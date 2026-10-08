/* SPDX-License-Identifier: BSD-2-Clause */
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

/* ------------------------------------------------------------------ */
/* the recorder                                                        */
/* ------------------------------------------------------------------ */

static struct fake_driver_state fake;

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

/* ------------------------------------------------------------------ */
/* the ops table                                                       */
/* ------------------------------------------------------------------ */

static int
fake_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{

	(void)hw;
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

static const struct ieee80211_ops fake_ops = {
	.tx = fake_tx,
	.wake_tx_queue = fake_wake_tx_queue,
	.add_interface = fake_add_interface,
	.config = fake_config,
	.bss_info_changed = fake_bss_info_changed,
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
