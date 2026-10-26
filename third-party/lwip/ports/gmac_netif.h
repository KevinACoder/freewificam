/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   gmac_netif.h
 * @brief  lwIP netif bridge over the firefly board's on-SoC GMAC driver.
 */

#ifndef FREEWIFICAM_LWIP_GMAC_NETIF_H
#define FREEWIFICAM_LWIP_GMAC_NETIF_H

#include <stdint.h>

#include "lwip/netif.h"

#include "gmac.h"

#define GMAC_NETIF_MTU		1500u

/*
 * Attach the driver as the default netif (up). Threading follows the wired
 * line's contract: the RX poll thread runs the drain through
 * gmac_netif_poll(), the transmit side runs on the TCP/IP thread under the
 * core lock.
 */
void gmac_netif_add(struct netif *netif, struct gmac *m,
		    const ip4_addr_t *ip, const ip4_addr_t *mask,
		    const ip4_addr_t *gw);

/* Drain at most budget received frames into the stack; returns how many
 * were fed. Also refreshes the link state into the netif. */
unsigned int gmac_netif_poll(struct netif *netif, unsigned int budget);

#endif /* FREEWIFICAM_LWIP_GMAC_NETIF_H */
