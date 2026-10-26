/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   e1000e_netif.h
 * @brief  lwIP netif bridge over the firefly e1000e driver: the adapter
 *         surface for one PCIe NIC.
 *
 * The bridge is the only consumer of the driver's receive path and the only
 * producer of its transmit path; the stack sees it as one netif. The driver
 * side of the seam is e1000e_rx_take/e1000e_tx_frame
 * (board/firefly/drivers/net), the stack side is tcpip_input (the RX poll
 * thread hands frames to the TCP/IP thread's mailbox).
 *
 * @design template instantiation of the upstream netif skeleton - the same
 *          shape as this repository's GMAC bridge (ports/lwip/ethnetif.c,
 *          after third-party/lwip/contrib/examples/ethernetif/ethernetif.c:
 *          netif_add assembly at :327-332, the linkoutput pbuf walk at
 *          :137-183), reduced to the e1000e seam: the RX ring is armed with
 *          pool pbufs by the driver itself, so the bridge allocates nothing
 *          on the receive path.
 */

#ifndef FREEWIFICAM_LWIP_E1000E_NETIF_H
#define FREEWIFICAM_LWIP_E1000E_NETIF_H

#include "lwip/ip_addr.h"
#include "lwip/netif.h"

struct e1000e;

/* Create the netif for one NIC and register it with the stack. Runs in a
 * thread context (the call comes under the core lock from the caller). */
void e1000e_netif_add(struct netif *netif, struct e1000e *nic,
		      const ip4_addr_t *ip, const ip4_addr_t *mask,
		      const ip4_addr_t *gw);

/* Driver-side receive seam: drain the descriptor ring into the stack
 * (netif->input = tcpip_input), up to `budget` frames per call. Returns
 * the number of frames handed over. Also refreshes the link view and
 * mirrors it into the netif (netif_set_link_up/down on changes). */
unsigned int e1000e_netif_poll(struct netif *netif, unsigned int budget);

#endif /* FREEWIFICAM_LWIP_E1000E_NETIF_H */
