/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    wlan_tx_ba.h
 * @brief   TX block-ack state machine shared by the two ports.
 *
 * The driver's transmit path asks for a block-ack session in exactly one way
 * (ieee80211_start_tx_ba_session -> ampdu_action TX_START), and the 802.11
 * wire exchange that follows is the same on the kernel and the RTOS port:
 * ADDBA Request, ADDBA Response, TX_OPERATIONAL, DELBA, retries.  Only the
 * driver call and the management transmit live in different worlds, so they
 * are the ops table; everything between them is this file.
 *
 * The receive half is deliberately not here: the two ports carry different
 * receive paths (the RTOS bridge has wlan_rx_ba.c, the kernel port has its own
 * reorder window), and mixing them would make one port's buffer lifetime a
 * requirement of the other's.
 */

#ifndef WLAN_TX_BA_H
#define WLAN_TX_BA_H

#include <linux/types.h>
#include <net/mac80211.h>

/* The note() events.  They are this module's own, not the bridge's enum: the
 * adapter maps them, so neither world has to include the other's header. */
#define WLAN_TX_BA_NOTE_START		1
#define WLAN_TX_BA_NOTE_OPERATIONAL	2
#define WLAN_TX_BA_NOTE_STOP		3

struct wlan_tx_ba_ops {
	/*
	 * Enter the driver's ampdu_action with the action the state machine
	 * decided.  The adapter supplies hw/vif/sta from its own context; the
	 * state machine never holds a driver-world object.
	 */
	int	(*ampdu)(void *arg, enum ieee80211_ampdu_mlme_action action,
		    u16 tid, u16 ssn, u16 buf, bool amsdu, u16 timeout);
	/* Hand a finished action frame to the management path (sleepable). */
	int	(*mgmt_tx)(void *arg, const uint8_t *frame, unsigned int len);
	/* The last 802.11 sequence number the data path used on this TID. */
	uint16_t (*last_tx_seq)(void *arg, int tid);
	/* A session state change, for the bridge's counters. */
	void	(*note)(void *arg, int evt);
	/* The peer address (the station this session is with). */
	const uint8_t *(*sta_addr)(void *arg);
	/* This port's own address (the ADDBA transmitter). */
	const uint8_t *(*local_addr)(void *arg);
};

int	wlan_tx_ba_init(struct ieee80211_hw *hw,
	    const struct wlan_tx_ba_ops *ops, void *arg);
void	wlan_tx_ba_detach(struct ieee80211_hw *hw);
int	wlan_tx_ba_start(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
	    u16 tid, u16 timeout);
void	wlan_tx_ba_stop(struct ieee80211_hw *hw, const uint8_t *ra, u16 tid);
/*
 * Offer a received frame to the TX half.  Returns 1 when the frame was an
 * ADDBA Response and has been consumed; a DELBA is handled (the TX session
 * ends) but returns 0 so the receive path still sees it; everything else
 * returns 0.  The caller must call this before its receive reorder path.
 */
int	wlan_tx_ba_rx(struct ieee80211_hw *hw, const uint8_t *frame,
	    unsigned int len);

#endif /* WLAN_TX_BA_H */
