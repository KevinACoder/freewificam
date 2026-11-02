/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   wlan_rx_ba.h
 * @brief  The downlink block-ack receive side of the RTOS arm: answer the
 *         AP's ADDBA Request and deliver the aggregated MPDUs in sequence.
 *
 * Why this exists: the imported net80211 has no HT/BA code at all (there is
 * no ieee80211_ht.c or ADDBA handler in the import), and this port's mac80211
 * substitute refuses aggregation on purpose (linuxkpi_80211.c:
 * ieee80211_start_tx_ba_session returns -EOPNOTSUPP).  The AP asks for a
 * downlink block-ack session as soon as it has data (P0 of RTOS-02: five
 * ADDBA Requests, each followed by DELBA, no answer from the STA), so the
 * link stays at one MPDU per TXOP and the airtime ledger fills with gap.
 *
 * This module is the missing answer: it intercepts the AP's ADDBA
 * Request/DELBA action frames before the stack, replies through the bridge's
 * management window, and holds the out-of-order MPDUs of an active TID until
 * their sequence gap closes.  Frames are copied into the module's own slots:
 * the lkpi skb pool is 64 data buffers and 32 of them are permanently held by
 * the driver's receive URBs, so a reorder window that held skbs would starve
 * the receive path.
 *
 * The chip generates the BlockAck responses itself once the session exists
 * (RTOS-02 P0: the ko cell, which has no host BA transmitter, carried 2,270
 * STA->AP BlockAcks and 29,515 A-MPDU-marked frames), so this module does not
 * transmit BlockAcks - only the ADDBA Response.
 */

#ifndef RTOS_NET80211_WLAN_RX_BA_H
#define RTOS_NET80211_WLAN_RX_BA_H

#include <stddef.h>
#include <stdint.h>

/* The receive metadata the bridge's input needs, carried beside each held
 * frame so a reordered delivery keeps the frame's own signal/channel/decrypt
 * status instead of the radio's current one. */
struct wlan_rx_ba_meta {
	int	rssi;
	int	fcs_bad;
	int	hw_dec;
	uint32_t freq;
	uint32_t device_ts;
};

/* Deliver one frame to the stack (the adapter binds this to
 * wlan_bridge_input).  Called with the module's delivery lock held. */
typedef void (*wlan_rx_ba_deliver_fn)(void *arg, const uint8_t *frame,
    size_t len, const struct wlan_rx_ba_meta *meta);

#if defined(CONFIG_WLAN_RX_BA) && CONFIG_WLAN_RX_BA

int	wlan_rx_ba_attach(wlan_rx_ba_deliver_fn fn, void *arg);
void	wlan_rx_ba_detach(void);

/*
 * Hand one received frame to the module.  Returns 1 when the module took
 * charge of it (delivered, held, answered or dropped - the caller must not
 * deliver it again), 0 when no BA session can consume it and the caller
 * should use the ordinary path.  With a session active this is the only
 * delivery path, which is what serialises the receive worker against the
 * reorder window's flush work.
 */
int	wlan_rx_ba_rx(const struct wlan_rx_ba_meta *meta, const uint8_t *frame,
    size_t len);

/* One anchor line with the module's counters (cell boundaries). */
void	wlan_rx_ba_dump(void);

#else /* !CONFIG_WLAN_RX_BA */

static inline int
wlan_rx_ba_attach(wlan_rx_ba_deliver_fn fn, void *arg)
{

	(void)fn;
	(void)arg;
	return 0;
}

static inline void
wlan_rx_ba_detach(void)
{
}

static inline int
wlan_rx_ba_rx(const struct wlan_rx_ba_meta *meta, const uint8_t *frame,
    size_t len)
{

	(void)meta;
	(void)frame;
	(void)len;
	return 0;
}

static inline void
wlan_rx_ba_dump(void)
{
}

#endif /* CONFIG_WLAN_RX_BA */

#endif /* RTOS_NET80211_WLAN_RX_BA_H */
