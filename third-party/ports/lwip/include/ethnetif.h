/*
 * @file   ethnetif.h
 * @brief  lwIP netif bridge over the GMAC driver (the FR-8 seam).
 */

#ifndef FREEWEBCAM_ETHNETIF_H
#define FREEWEBCAM_ETHNETIF_H

#include <stdint.h>

#include "lwip/ip4_addr.h"
#include "lwip/netif.h"

/* netif_add over the GMAC port (eth_init fills MAC/MTU/flags from the
 * driver), then set_up + set_default. NO_SYS=1: input = ethernet_input in
 * the main loop; P7-2a swaps to tcpip_input without touching this file. */
void ethnetif_add(struct netif *netif, unsigned int port,
		  const ip4_addr_t *ip, const ip4_addr_t *mask,
		  const ip4_addr_t *gw);

/* Driver staging ring -> pbuf -> netif->input; call from the main loop
 * (NO_SYS) or the tcpip context (P7-2a). Returns frames fed. */
unsigned int ethnetif_poll(struct netif *netif);

/* N7 diagnostics: where the RX loss happens (pool exhaustion vs mailbox
 * rejects) and how much actually reached the stack. */
void ethnetif_get_diag(unsigned long *poll_calls, unsigned long *pool_fail,
		       unsigned long *input_fail, unsigned long *fed_total);

#endif /* FREEWEBCAM_ETHNETIF_H */
