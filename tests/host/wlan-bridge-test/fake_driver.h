/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    fake_driver.h
 * @brief   The scripted driver's face: its ops table, the recorder the cases
 *          assert against, and the knobs that make a case go red.
 */

#ifndef FAKE_DRIVER_H
#define FAKE_DRIVER_H

#include <stdint.h>
#include <stddef.h>

#include "linuxkpi_core.h"
#include <net/mac80211.h>
#include <linux/ieee80211.h>

/* The scan group alone leaves one probe request per channel (13 on the 2.4 GHz
 * table this bridge builds), so the capture must hold the whole sweep plus the
 * join exchange; a capture that filled up would silently drop the auth/assoc
 * request and the case would fail for a reason that is the harness's, not the
 * chain's. Anything dropped is counted (tx_lost) so that never stays invisible. */
/* three sweeps plus two join exchanges now ride one run (the re-sweep and
 * the mid-sweep-join cases added their own); the capture must hold them all
 * or the frames past the end fall out of the answerable window and the case
 * fails for a reason that is the harness's, not the chain's */
#define FAKE_DRIVER_TX_MAX	160
#define FAKE_DRIVER_FRAME_MAX	1600
#define FAKE_DRIVER_ORDER_MAX	512
#define FAKE_DRIVER_CONFIG_MAX	32
#define FAKE_DRIVER_BSS_EV_MAX	24

/* The headroom the double declares, the way a real driver declares it at
 * registration: mac80211 hands every frame to the driver's tx op with at least
 * this much room in front, because the driver builds its transmit descriptor by
 * pushing it on.  rtw89's is 52 (IEEE80211_HT_CTL_LEN + its txwd body and info,
 * core.c:7469-7481); the value here only has to be larger than the pool's plain
 * dev allocation (NET_SKB_PAD, 32) so that a bridge which forgets the
 * reservation is caught rather than accidentally passing. */
#define FAKE_DRIVER_TX_HEADROOM	52

/* one entry per ops call, in the order the bridge made them */
enum fake_call {
	FAKE_CALL_ADD_INTERFACE = 1,
	FAKE_CALL_CONFIG,
	FAKE_CALL_FILTER,
	FAKE_CALL_FILTER_REFUSED,
	FAKE_CALL_SCAN_START,
	FAKE_CALL_SCAN_COMPLETE,
	FAKE_CALL_TX,
	FAKE_CALL_SET_KEY,
	FAKE_CALL_CLEAR_KEY,
	FAKE_CALL_BSS_INFO,
	FAKE_CALL_START,
	FAKE_CALL_STOP,
	FAKE_CALL_WAKE_TXQ,
};

struct fake_driver_state {
	unsigned int add_interface_calls;
	unsigned int config_calls;
	unsigned int filter_calls;
	unsigned int scan_start_calls;
	unsigned int scan_complete_calls;
	unsigned int tx_calls;
	unsigned int tx_mgmt_calls;
	unsigned int set_key_calls;
	unsigned int clear_key_calls;
	unsigned int bss_info_calls;
	unsigned int start_calls;
	unsigned int stop_calls;

	unsigned int filter_completes;	/* clears (bit went back to zero) */
	uint32_t filter_changed;
	uint32_t filter_total;
	uint32_t filter_last_total;
	uint32_t last_freq;
	uint32_t last_channel_hw;
	uint32_t config_freqs[FAKE_DRIVER_CONFIG_MAX];
	unsigned int config_freq_count;
	int last_bss_joined;
	uint8_t last_bssid[6];
	/* every bss_info_changed call, as the driver's association branch
	 * would read it: the change bits, vif->cfg's assoc/aid/ap_addr at call
	 * time, the BSSID (and whether it was NULL), and the rate bitmaps the
	 * adapter built - the association wiring the join judgment stands on */
	struct fake_bss_ev {
		uint64_t	changed;
		int		assoc;
		unsigned int	aid;
		uint8_t		bssid[6];
		int		bssid_null;
		uint8_t		ap_addr[6];
		uint32_t	supp0;
		uint32_t	basic;
	} bss_ev[FAKE_DRIVER_BSS_EV_MAX];
	unsigned int bss_ev_count;
	int last_key_pairwise;
	int last_key_idx;
	int last_key_cipher;
	unsigned int last_key_len;

	uint8_t tx_frames[FAKE_DRIVER_TX_MAX][FAKE_DRIVER_FRAME_MAX];
	unsigned int tx_lens[FAKE_DRIVER_TX_MAX];
	unsigned int tx_is_mgmt[FAKE_DRIVER_TX_MAX];
	unsigned int tx_count;
	unsigned int tx_lost;		/* frames the capture could not hold */
	/* the headroom the frames actually arrived with: the smallest seen, and how
	 * many were short of the declared FAKE_DRIVER_TX_HEADROOM */
	unsigned int tx_headroom_min;
	unsigned int tx_headroom_bad;

	enum fake_call order[FAKE_DRIVER_ORDER_MAX];
	unsigned int order_count;

	int filter_refuses;
	int double_free_tx;		/* fault: release the n-th frame twice */
	unsigned int double_free_at;
};

const struct ieee80211_ops *fake_driver_ops(void);
void	fake_driver_reset(void);
const struct fake_driver_state *fake_driver_state(void);
void	fake_driver_knob_filter_refuses(int on);
void	fake_driver_knob_double_free_tx(unsigned int nth);

#endif /* FAKE_DRIVER_H */
