/*
 * @file   ethnetif.h
 * @brief  lwIP netif bridge over the GMAC driver: the adapter surface.
 *
 * The bridge is the only consumer of the driver's receive path and the only
 * producer of its transmit path; the stack sees it as one netif. The driver
 * side of the seam is dwc_eqos_read_frame/send_frame (board/rk3568-itx/drivers),
 * the stack side is tcpip_input (the RX thread hands frames to the TCP/IP
 * mailbox).
 */

#ifndef FREEWEB_LWIP_ETHNETIF_H
#define FREEWEB_LWIP_ETHNETIF_H

#include "lwip/ip_addr.h"
#include "lwip/netif.h"

/* Create the netif for one GMAC port and register it with the stack. Runs in
 * a thread context (the call comes under the core lock from the caller). */
void ethnetif_add(struct netif *netif, unsigned int port,
		  const ip4_addr_t *ip, const ip4_addr_t *mask,
		  const ip4_addr_t *gw);

/* Driver-side receive seam: drain the driver's staged frames into the stack
 * (netif->input = tcpip_input). Returns the number of frames handed over. */
unsigned int ethnetif_poll(struct netif *netif);

/* Loss counters for the heartbeat: poll calls, pbuf-pool failures, input
 * failures, frames fed in total. */
void ethnetif_get_diag(unsigned long *poll_calls, unsigned long *pool_fail,
		       unsigned long *input_fail, unsigned long *fed_total);

#endif /* FREEWEB_LWIP_ETHNETIF_H */
