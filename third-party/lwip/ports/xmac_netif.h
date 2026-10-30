/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   xmac_netif.h
 * @brief  lwIP netif bridge over the firefly v2 board's on-SoC xmac driver.
 */

#ifndef FREEWIFICAM_LWIP_XMAC_NETIF_H
#define FREEWIFICAM_LWIP_XMAC_NETIF_H

#include <stdint.h>

#include "lwip/netif.h"

#include "xmac.h"

#define XMAC_NETIF_MTU		1500u

/*
 * Attach the driver as the default netif (up). Threading follows the board
 * family's other wired NIC: the RX poll thread runs the drain through
 * xmac_netif_poll(), the transmit side runs on the TCP/IP thread under the
 * core lock.
 */
void xmac_netif_add(struct netif *netif, struct xmac *m,
		    const ip4_addr_t *ip, const ip4_addr_t *mask,
		    const ip4_addr_t *gw);

/* Drain at most budget received frames into the stack; returns how many
 * were fed. Also refreshes the link state into the netif. */
unsigned int xmac_netif_poll(struct netif *netif, unsigned int budget);

/* Receive-path drop counters, for the bench's diagnostic line: how many pbuf
 * allocations failed, how many polls found the ring empty, how many frames the
 * stack refused, and how many were actually fed up. */
/* Interrupt entry: pass the netif's state pointer; returns 1 when the receive
 * line fired (there is work for the drain). */
int xmac_netif_irq(void *netif_state);




void xmac_netif_rx_diag(unsigned long *pbuf_fail, unsigned long *ring_empty,
			unsigned long *input_fail, unsigned long *fed,
			unsigned long *polls);

#endif /* FREEWIFICAM_LWIP_XMAC_NETIF_H */
