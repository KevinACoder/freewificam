/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    lkpinetif_seam.h
 * @brief   The lwIP-free half of the netdev presentation layer: what the
 *          Linux-compatibility side and the stack side of the adapter say to each
 *          other.
 *
 * @design adapter split at the world boundary.  The two sides of this adapter
 *         live in different header worlds - the driver's netdev and buffer types
 *         on one side, lwIP's interface and buffer types on the other - and those
 *         worlds disagree about names they both own (lwIP's htons/ntohs against
 *         the contract header's), so no translation unit may include both.  This
 *         header is the seam that keeps them apart: it names the calls that cross,
 *         with plain C types only, and each side implements its own half
 *         (lkpinetif.c the netdev half, lkpinetif_lwip.c the stack half).  The
 *         shape follows the net80211 line's split, where the presentation layer
 *         declares the WiFi stack's dotted face rather than including it
 *         (ports/lwip/wlannetif.c:42-46).
 * Reference for the shape: ports/lwip/lkpinetif.h:1-30.
 * @date    12.10.2026
 */

#ifndef LKPINETIF_SEAM_H
#define LKPINETIF_SEAM_H

#include <stddef.h>
#include <stdint.h>

struct wiphy;

/* --- the netdev side (implemented in lkpinetif.c) ------------------------- */

/* Bind the adapter to the interface the driver registered: the address comes
 * from that net device, the transmit path is its own ndo_start_xmit, and the
 * receive hook only accepts frames that arrive on it.  Returns 0 on success. */
int	lkpinetif_bind(struct wiphy *wiphy);

/* The interface's hardware address (the driver set it from the chip before it
 * registered; its open path may fill it, so a caller reads it after bringing the
 * interface up).  Returns 0 on success. */
int	lkpinetif_hwaddr(uint8_t addr[6]);

/* Hand one ethernet frame to the driver's transmit entry - the path lwIP's
 * linkoutput takes, and the path the supplicant's EAPOL takes.  Returns 0 when
 * the driver took the frame. */
int	lkpinetif_tx_frame(const uint8_t *frame, size_t len);

/* The supplicant's EAPOL receive: called with the source address and the 802.1X
 * payload (no Ethernet header) of every EAPOL frame the interface receives. */
void	lkpinetif_set_eapol_sink(void (*fn)(const uint8_t src[6],
	    const uint8_t *pdu, size_t len, void *arg), void *arg);

/* --- the stack side (implemented in lkpinetif_lwip.c) -------------------- */

/* Deliver one whole ethernet frame to the stack.  Returns 0 when the stack took
 * it, -1 when no interface has been assembled yet, -2 when the frame could not
 * be handed over (no buffer, or the stack refused it). */
int	lkpinetif_deliver(const uint8_t *frame, size_t len);

/* --- diagnostics ---------------------------------------------------------- */

/* Loss counters, so a receive loss localizes to the interface, the EAPOL sink or
 * the stack without a debugger (wlannetif_get_diag's shape). */
struct lkpinetif_diag {
	unsigned long	rx_frames;	/* frames the driver handed over */
	unsigned long	rx_foreign;	/* another interface's frames */
	unsigned long	rx_eapol;	/* handed to the EAPOL sink */
	unsigned long	rx_no_netif;	/* arrived before the netif was added */
	unsigned long	rx_input_fail;	/* the stack refused them */
	unsigned long	tx_frames;	/* frames handed to the transmit entry */
	unsigned long	tx_fail;	/* no interface or no buffer */
};

void	lkpinetif_get_diag(struct lkpinetif_diag *out);

#endif /* LKPINETIF_SEAM_H */
