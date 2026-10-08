/* SPDX-License-Identifier: BSD-2-Clause */
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

/* Sta lifetime (call ops->sta_add / sta_remove; the sta carries
 * sta_data_size bytes of zeroed driver-private area) */
struct ieee80211_sta *lkpi80211_drv_sta_add(struct ieee80211_hw *hw,
	    struct ieee80211_vif *vif, const u8 addr[6], u16 aid);
void	lkpi80211_drv_sta_remove(struct ieee80211_hw *hw,
	    struct ieee80211_vif *vif, struct ieee80211_sta *sta);

/* enumerate what the substitute holds (the bridge's bookkeeping) */
struct ieee80211_vif *lkpi80211_hw_vif(struct ieee80211_hw *hw, unsigned int idx);
struct ieee80211_sta *lkpi80211_hw_sta(struct ieee80211_hw *hw, unsigned int idx);
unsigned int	lkpi80211_hw_vif_count(struct ieee80211_hw *hw);

/* the driver-private area behind an hw (hw->priv), for the bridge's own use */
void	*lkpi80211_hw_priv(struct ieee80211_hw *hw);

#endif /* LINUXKPI_80211_H */
