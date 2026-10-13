/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_80211.h
 * @brief   The seam between the mac80211 substitute body and the net80211 bridge.
 *
 * @design ports & adapters, the same shape as the USB layer's backend seam
 *         (usb/lkpi_usb_backend.h): the substitute body (linuxkpi_80211.c) is the
 *         adapter-neutral core that implements the mac80211 face the driver was
 *         written against, and the bridge (ports/net80211/bridge/) is the
 *         adapter that knows both worlds. The seam carries only what those two
 *         must exchange: four upward calls (register/unregister, a received
 *         frame, a transmit completion) and the downward calls that let the
 *         bridge drive the driver through its ops table.
 *
 * Why the driver's own ops are called from here rather than by the bridge
 * directly: the ops signatures are mac80211 types (struct ieee80211_hw and
 * friends). Keeping those types on this side of the seam means the bridge never
 * holds a driver-world struct in its own control flow - it asks for an action
 * by name (start, scan, filter, join, key, tx) and this file performs the
 * corresponding ops call, so the driver-call order is one readable list in one
 * place. The bridge may still include the mac80211 headers for the object
 * pointers it passed in (a vif, a skb); what it does not do is reach into the
 * ops table itself.
 *
 * Frame ownership: ieee80211_rx_irqsafe() hands the skb to the bridge and gives
 * up all rights to it (the bridge or the stack frees it). Everything else
 * stays owned by its caller.
 *
 * Reference for the shape: components/linuxkpi/usb/lkpi_usb_backend.h:31-36
 * (the seam-registration pattern this face mirrors).
 * @date    08.10.2026
 */

#ifndef LINUXKPI_80211_H
#define LINUXKPI_80211_H

#include "linuxkpi_core.h"
#include <net/mac80211.h>

/* ------------------------------------------------------------------ */
/* upward: what the substitute asks of the bridge                      */
/* ------------------------------------------------------------------ */

struct lkpi80211_bridge_ops {
	/*
	 * Called from ieee80211_register_hw(). The bridge builds the net80211
	 * side (ieee80211com, interface, receive seam) around this hw and
	 * returns 0, or a negative error which register_hw reports - a
	 * registration without a bridge (or with a failing one) must fail
	 * honestly rather than leave a half-live hw behind.
	 */
	int	(*attach)(struct ieee80211_hw *hw);
	/* Called from ieee80211_unregister_hw(). */
	void	(*detach)(struct ieee80211_hw *hw);

	/*
	 * A received 802.11 frame, no FCS, with its status block already
	 * filled by the substitute. Ownership of `skb` passes to the bridge.
	 */
	void	(*rx)(struct ieee80211_hw *hw, struct ieee80211_rx_status *rs,
		    struct sk_buff *skb);

	/*
	 * A transmitted frame is complete. Ownership of `skb` stays with the
	 * substitute (it releases it after the bridge has been told), because
	 * the caller of ieee80211_tx_status_irqsafe() has already stopped
	 * caring and the substitute is the only side that must guarantee the
	 * release exactly once.
	 */
	void	(*tx_complete)(struct ieee80211_hw *hw, struct sk_buff *skb);

	/*
	 * The driver reported a hardware scan finished - it called
	 * ieee80211_scan_completed(). The bridge closes its sweep window here.
	 *
	 * This is the hw_scan path's only completion signal, and it exists
	 * because the two scan routes end differently: a software scan ends when
	 * the bridge's own channel walk runs out (the bridge drives it and knows
	 * when), while a hardware scan ends when the *driver* says so - the
	 * firmware owned the dwell. Without this the bridge would have no way to
	 * learn that a firmware sweep ended.
	 */
	void	(*scan_complete)(struct ieee80211_hw *hw);
};

/* install the bridge; without one, register_hw fails with -ENODEV */
void	lkpi80211_bridge_register(const struct lkpi80211_bridge_ops *ops,
	    void *arg);

/* ------------------------------------------------------------------ */
/* downward: the bridge drives the driver's ops through these          */
/* ------------------------------------------------------------------ */

/* hw->ops->start / stop */
int	lkpi80211_drv_start(struct ieee80211_hw *hw);
void	lkpi80211_drv_stop(struct ieee80211_hw *hw, bool suspend);

/* Channel: writes the channel descriptor into hw->conf.chandef (the storage
 * lives here) and calls ops->config(hw, 0, IEEE80211_CONF_CHANGE_CHANNEL). */
int	lkpi80211_drv_config_channel(struct ieee80211_hw *hw, u32 freq,
	    u8 hw_value, u8 band, u8 width, u32 center_freq1);

/* ops->sw_scan_start(hw, vif, addr) / sw_scan_complete(hw, vif) */
int	lkpi80211_drv_scan_start(struct ieee80211_hw *hw);
void	lkpi80211_drv_scan_complete(struct ieee80211_hw *hw);

/*
 * The hardware-scan route, for a driver that exposes ops->hw_scan and no
 * ops->sw_scan_start (the AC 7260's mvm path: its ops table has no software
 * fallback, so a sweep there can only be a firmware one).
 *
 * `has_hw_scan` is what the bridge asks to choose the route; `hw_scan` builds
 * the scan request from the bridge's channel list and hands it to the driver,
 * which owns the dwell from then on; `hw_scan_cancel` takes it back. The
 * completion comes up the other way, through the bridge ops' scan_complete.
 *
 * The channel list is the bridge's because net80211 owns the channel set (the
 * driver-side request wants the same channels, as frequencies in MHz).
 */
int	lkpi80211_drv_has_hw_scan(struct ieee80211_hw *hw);
int	lkpi80211_drv_hw_scan(struct ieee80211_hw *hw, const u32 *freqs,
	    unsigned int n_freqs);
void	lkpi80211_drv_hw_scan_cancel(struct ieee80211_hw *hw);

/* ops->configure_filter(hw, changed, &total, 0) */
int	lkpi80211_drv_filter(struct ieee80211_hw *hw, u32 changed, u32 total);

/* Interface lifetime: allocate the vif (type + addr + vif_data_size bytes of
 * zeroed driver-private area), call ops->add_interface, and hand it back.
 * `lkpi80211_drv_vif_remove` reverses it. */
struct ieee80211_vif *lkpi80211_drv_vif_add(struct ieee80211_hw *hw, u8 type,
	    const u8 addr[6]);
void	lkpi80211_drv_vif_remove(struct ieee80211_hw *hw,
	    struct ieee80211_vif *vif);

/* ops->bss_info_changed(hw, vif, &vif->bss_conf, changed): the bridge fills
 * the vif's bss_conf fields it wants the driver to see, then calls this. */
void	lkpi80211_drv_bss_changed(struct ieee80211_hw *hw,
	    struct ieee80211_vif *vif, u64 changed);

/* ops->set_key(hw, cmd, vif, sta, key) */
int	lkpi80211_drv_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
	    struct ieee80211_vif *vif, struct ieee80211_sta *sta,
	    struct ieee80211_key_conf *key);

/* ops->tx(hw, control, skb): the bridge's outgoing frame (its own management
 * transmission rides this after the net80211 framing is done) */
void	lkpi80211_drv_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	    struct ieee80211_sta *sta, struct sk_buff *skb);

/*
 * Drain the deferred receive queue.  ieee80211_rx_irqsafe enqueues and a
 * worker delivers (the interface's own contract, net/mac80211/rx.c); a caller
 * that must observe the delivery - a host test that injects a frame and then
 * inspects the stack, or a shutdown path - waits for the worker to finish
 * here, the same way the interface flushes its rx work before tearing a
 * device down.
 */
void	lkpi80211_rx_flush(struct ieee80211_hw *hw);

/* the channel context lifecycle (ops->add_chanctx / assign_vif_chanctx /
 * remove_chanctx): the interface reserves one when an interface starts using a
 * channel, and a driver may refuse a station upload without the binding */
int	lkpi80211_drv_chanctx_use(struct ieee80211_hw *hw,
	    struct ieee80211_vif *vif, u32 freq, u8 hw_value, u8 band, u8 width);
void	lkpi80211_drv_chanctx_release(struct ieee80211_hw *hw,
	    struct ieee80211_vif *vif);

/* the transmit handoff the interface performs: the txq selection first
 * (ieee80211_get_txq) and, when a queue owns the frame, the enqueue and the
 * driver's wake - a driver's management frames take this route, not drv_tx */
void	lkpi80211_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	    struct ieee80211_sta *sta, struct sk_buff *skb);

/* Sta lifetime (call ops->sta_add / sta_remove; the sta carries
 * sta_data_size bytes of zeroed driver-private area).  The ladder is split the
 * way the association splits: drv_sta_add uploads the peer (the moment mac80211
 * prepares a connection), drv_sta_assoc runs AUTH -> ASSOC when the association
 * completes, drv_sta_remove winds a joined station down, and drv_sta_free winds
 * down a station that never associated. */
struct ieee80211_sta *lkpi80211_drv_sta_add(struct ieee80211_hw *hw,
	    struct ieee80211_vif *vif, const u8 addr[6], u16 aid);
int	lkpi80211_drv_sta_assoc(struct ieee80211_hw *hw,
	    struct ieee80211_vif *vif, struct ieee80211_sta *sta, u16 aid);
void	lkpi80211_drv_sta_remove(struct ieee80211_hw *hw,
	    struct ieee80211_vif *vif, struct ieee80211_sta *sta);
void	lkpi80211_drv_sta_free(struct ieee80211_hw *hw,
	    struct ieee80211_vif *vif, struct ieee80211_sta *sta);

/* enumerate what the substitute holds (the bridge's bookkeeping) */
struct ieee80211_vif *lkpi80211_hw_vif(struct ieee80211_hw *hw, unsigned int idx);
struct ieee80211_sta *lkpi80211_hw_sta(struct ieee80211_hw *hw, unsigned int idx);
/* the walk behind the interface's for_each_station(): the next station after
 * `prev`, or the first one when `prev` is NULL */
struct ieee80211_sta *lkpi80211_hw_sta_next(struct ieee80211_hw *hw,
	    struct ieee80211_sta *prev);
unsigned int	lkpi80211_hw_vif_count(struct ieee80211_hw *hw);

/* the driver-private area behind an hw (hw->priv), for the bridge's own use */
void	*lkpi80211_hw_priv(struct ieee80211_hw *hw);

/*
 * The receive entry's counters: how many frames the driver handed to
 * ieee80211_rx_napi, and the frame-control byte of the first one (with a flag
 * for "none seen", since 0x00 is a legal byte).  A bring-up that finds an empty
 * receive table reads them to tell "nothing was handed over" from "what arrived
 * was not a frame".
 */
void	lkpi80211_rx_counters(u32 *frames, u32 *first_fc, int *fc_seen);

#endif /* LINUXKPI_80211_H */
