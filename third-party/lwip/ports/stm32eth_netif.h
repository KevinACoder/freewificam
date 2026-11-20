/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   stm32eth_netif.h
 * @brief  lwIP netif bridge over the F429 board's wired Ethernet seam.
 *
 * The board's driver face is `eth.h` (plain C).  This bridge is the only lwIP
 * consumer of its receive path and the only producer of its transmit path;
 * the stack sees one netif.
 */

#ifndef FREEWIFICAM_LWIP_STM32ETH_NETIF_H
#define FREEWIFICAM_LWIP_STM32ETH_NETIF_H

#include <stdint.h>

#include "lwip/netif.h"

/* Receive-path counters: frames handed to the stack, and the three ways a
 * completed frame can fail to get there.  The case prints them beside the
 * driver's own counters, which is what separates "the ring never saw it"
 * from "the stack refused it". */
struct stm32eth_netif_stats {
	unsigned long rx_fed;
	unsigned long rx_pbuf_fail;
	unsigned long rx_copy_fail;
	unsigned long rx_input_fail;
	unsigned long tx_oversize;
	unsigned long tx_copy_fail;
	unsigned long tx_drop;
};

/*
 * Create the netif over the board driver and register it with the stack.
 * Runs in a thread context under the core lock (the caller holds it, as
 * ethnetif_add's callers do).
 */
void stm32eth_netif_add(struct netif *netif, const ip4_addr_t *ip,
			const ip4_addr_t *mask, const ip4_addr_t *gw);

/* Drain at most `budget` completed frames into the stack; returns how many
 * were fed.  Called by the case's receive thread. */
unsigned int stm32eth_netif_poll(struct netif *netif, unsigned int budget);

void stm32eth_netif_stats_snapshot(struct stm32eth_netif_stats *out);

#endif /* FREEWIFICAM_LWIP_STM32ETH_NETIF_H */
