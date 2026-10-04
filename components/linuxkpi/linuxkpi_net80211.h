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
};

int	lkpi_n80211_attach(struct ieee80211_hw *hw, const uint8_t perm_addr[6],
	    const struct lkpi_n80211_drv *drv);
void	lkpi_n80211_rx_frame(const void *data, size_t len, int signal_dbm);

/* the bridge queues its state-machine work here (the mac80211 side
 * dispatches it onto the single lkpi workqueue thread) */
void	ieee80211_queue_work(struct ieee80211_hw *hw,
	    struct work_struct *work);

#endif /* LINUXKPI_NET80211_H */
