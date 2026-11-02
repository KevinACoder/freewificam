/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    mac80211_ba.h
 * @brief   The minimal block-ack state machine the kernelmod line needs to
 *          run HT with the lab AP: RX ADDBA response + reorder, TX ADDBA
 *          request/telemetry, and DELBA.
 *
 * The imported net80211 stack has no HT/BA code (there is no ieee80211_ht.c or
 * ieee80211_ampdu.c in the import), and the mac80211 substitute used to answer
 * ieee80211_start_tx_ba_session() with a bare `return 0`.  The driver therefore
 * never saw TX_OPERATIONAL, the AP's ADDBA Request was dropped by the stack's
 * unknown-action path, and every downlink MPDU paid a plain ACK.  This module
 * is the mac80211 half of that gap, kept in the kernelmod ports tree where the
 * driver's ieee80211_ops and the net80211 bridge are both reachable.
 */

#ifndef _MAC80211_BA_H_
#define _MAC80211_BA_H_

#include <linux/types.h>

struct ieee80211_hw;
struct ieee80211_sta;
struct sk_buff;

int	wlan_ba_init(struct ieee80211_hw *hw);
void	wlan_ba_detach(struct ieee80211_hw *hw);

/*
 * Receive entry.  Called before the frame reaches wlan_bridge_input().
 * Returns 1 when this module consumed the frame and the caller must free the
 * skb; 2 when the reorder window now owns the skb (the caller must NOT free
 * it); 0 when the frame is ordinary traffic and the caller delivers it
 * through wlan_adapter_deliver().
 */
int	wlan_ba_rx(struct ieee80211_hw *hw, struct sk_buff *skb);

/* mac80211 entry points the driver calls from rtl8xxxu_tx()/teardown. */
int	wlan_ba_start_tx(struct ieee80211_sta *sta, u16 tid, u16 timeout);
int	wlan_ba_stop_tx(struct ieee80211_sta *sta, u16 tid);

#endif /* _MAC80211_BA_H_ */
