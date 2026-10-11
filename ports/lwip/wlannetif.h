/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    wlannetif.h
 * @brief   The WLAN netif's face: assembly, start/stop and the diagnostics the
 *          board console reads.
 *
 * @design template instantiation declaration side - the shape follows
 *         ports/lwip/ethnetif.h (the GMAC bridge's face), so the two
 *         presentation layers are read the same way.
 * @date    08.10.2026
 */

#ifndef WLANNETIF_H
#define WLANNETIF_H

#include "lwip/netif.h"
#include "lwip/err.h"

/*
 * Assemble the netif over the WLAN bridge: netif_add with this layer's init,
 * the port's receive/event seams registered, the ARP output and the bridge as
 * the link output. The address arguments come from the caller's plan.
 */
err_t	wlannetif_add(struct netif *netif, const ip4_addr_t *ip,
	    const ip4_addr_t *mask, const ip4_addr_t *gw);

/* Bring the interface up and start its address acquisition (the DHCP client
 * when the build enables it, in which case the caller's address is the
 * documented fallback until a lease arrives). */
void	wlannetif_start(struct netif *netif);
void	wlannetif_stop(struct netif *netif);

/*
 * Association and disassociation steps, exported because a WPA image has two
 * event consumers and the port carries one event slot: the supplicant's
 * backend owns the slot and forwards these two events here (the composition
 * the driver-side backend documents). The actions are exactly what this
 * layer's own event handler does - link state up/down, which is what the
 * DHCP client watches.
 */
void	wlannetif_note_assoc(struct netif *netif);
void	wlannetif_note_disassoc(struct netif *netif);

/* counters: received frames, receive-path failures, transmitted frames,
 * transmit failures */
void	wlannetif_get_diag(unsigned long *rx_frames, unsigned long *rx_input_fail,
	    unsigned long *tx_frames, unsigned long *tx_fail);

#endif /* WLANNETIF_H */
