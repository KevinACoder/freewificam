/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * linuxkpi_net80211.h - the seam between the two 802.11 worlds (M4a).
 *
 * The mac80211 side (linuxkpi_80211.c) implements the driver vtable over
 * the verbatim rtw88's ieee80211_ops; the net80211 side (this pair) is a
 * plain-C bridge that drives the ic-based vendored net80211 with it.  The
 * ABI carries only C scalar/byte-array types: the two worlds define
 * different layouts for identically-named structs (ieee80211_channel!)
 * and must never share one.
 */

#ifndef LINUXKPI_NET80211_H
#define LINUXKPI_NET80211_H

#include <stddef.h>
#include <stdint.h>

struct work_struct;
struct ieee80211_hw;

/* the driver side as the net80211 bridge sees it */
struct lkpi_n80211_drv {
	int	(*start)(void);		/* power on + DLFW + MAC/BB init */
	void	(*stop)(void);
	int	(*add_interface)(const uint8_t addr[6]);	/* STA vif */
	int	(*config)(int center_freq, int chan);		/* tune */
	void	(*sw_scan_start)(const uint8_t mac[6]);
	void	(*sw_scan_complete)(void);
	int	(*tx_mgmt)(const uint8_t *frame, size_t len);	/* fire-and-forget */
	/* phy calibration + coex gate mac80211 runs before every
	 * auth/assoc mgmt burst (rtw_chip_prepare_tx) */
	void	(*mgd_prepare_tx)(void);
	/* BSS context for the firmware: bssid write + assoc/port state
	 * (BSS_CHANGED_BSSID / BSS_CHANGED_ASSOC); aid = 0 pre-assoc */
	int	(*bss_info_changed)(const uint8_t bssid[6], int assoc,
	    unsigned int aid, int beacon_int);
	/* data-plane TX relay: one linear 802.11 frame (post-encap); tid is
	 * the txdesc qsel (0..7, EAPOL rides 7); key_type: 0 = no hw key,
	 * 1 = pairwise (PTK), 2 = group (GTK at key_idx) - the mac side maps
	 * it to info->control.hw_key for rtw88's sec_type fill */
	int	(*tx_data)(const uint8_t *frame, size_t len, int tid,
	    int key_type, int key_idx);
	/* peer lifecycle: rtw88's sta_add bookkeeping must exist before the
	 * S_RUN bss_info_changed call (bf/ampdu resolve the peer through
	 * ieee80211_find_sta, which warned "failed to find station entry"
	 * while this junction was unwired - board 2026-10-05 R3) */
	int	(*peer_add)(const uint8_t bssid[6], unsigned int aid);
	void	(*peer_remove)(void);
	/* CAM key programming (HW-CCMP route): writes/clears the rtw88 sec
	 * CAM; pairwise = PTK, else GTK at key_idx (0..3) */
	int	(*set_key)(int pairwise, int key_idx, const uint8_t *key,
	    size_t len);
	void	(*clear_key)(int pairwise, int key_idx);
};

int	lkpi_n80211_attach(struct ieee80211_hw *hw, const uint8_t perm_addr[6],
	    const struct lkpi_n80211_drv *drv);
void	lkpi_n80211_rx_frame(const void *data, size_t len, int signal_dbm,
	    int decrypted);

/* the bridge queues its state-machine work here (the mac80211 side
 * dispatches it onto the single lkpi workqueue thread) */
void	ieee80211_queue_work(struct ieee80211_hw *hw,
	    struct work_struct *work);

#endif /* LINUXKPI_NET80211_H */
