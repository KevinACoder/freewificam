/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    lkpinetif.h
 * @brief   lwIP netif over a FullMAC driver's net device: the presentation layer
 *          for a driver that registers a Linux netdev through the
 *          Linux-compatibility face.
 *
 * @design template instantiation of the upstream netif skeleton, the same shape
 *         this directory's WLAN bridge uses (ports/lwip/wlannetif.h:24-47): the
 *         netif is assembled with netif_add and an init callback, linkoutput is
 *         the transmit path, and the receive path is a call into the stack.  What
 *         differs is the side that owns the seam.  The WLAN bridge is called by
 *         the WiFi stack and asks it for the interface's address and frames
 *         (wlannetif.c:45-46 declares that dotted face); here the driver is a
 *         netdev whose transmit entry and receive hook are the netdev face's own
 *         (components/linuxkpi/linuxkpi_netdev.c:465 offers the receive hook), so
 *         the adapter binds to the registered interface and drives it directly.
 *
 * This header is the stack half's face; the calls that cross between the two
 * worlds - and the reason they cross through a header of their own - are in
 * lkpinetif_seam.h.
 * Reference for the shape: ports/lwip/wlannetif.h:1-23.
 * @date    12.10.2026
 */

#ifndef LKPINETIF_H
#define LKPINETIF_H

#include "lwip/netif.h"
#include "lwip/ip_addr.h"

#include "lkpinetif_seam.h"

/* Assemble the lwIP interface over the bound net device (the netif_add
 * assembly; call after lkpinetif_bind and before lkpinetif_start). */
err_t	lkpinetif_add(struct netif *netif, const ip4_addr_t *ip,
	    const ip4_addr_t *mask, const ip4_addr_t *gw);

/* Bring the interface up (and start the DHCP client) / down. */
void	lkpinetif_start(struct netif *netif);
void	lkpinetif_stop(struct netif *netif);

/* Link state, from whoever hears the driver's connect/disconnect notifications. */
void	lkpinetif_note_link(struct netif *netif, int up);

#endif /* LKPINETIF_H */
