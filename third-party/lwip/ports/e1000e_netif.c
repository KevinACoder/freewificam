/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   e1000e_netif.c
 * @brief  lwIP netif bridge over the firefly e1000e driver.
 * @author zhugengyu
 * @date   16.10.2026
 * @design upstream ethernetif skeleton (see e1000e_netif.h): netif_add with
 *          etharp_output/linkoutput, the driver's own entries in the
 *          low_level slots. Threading follows the GMAC bridge's contract
 *          (ports/lwip/ethnetif.c): the RX poll thread runs the drain and
 *          netif->input is tcpip_input; the transmit side runs on the
 *          TCP/IP thread under the core lock.
 */

#include <stddef.h>

#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include "netif/ethernet.h"

#include "generic_timer.h"

#include "e1000e_netif.h"

/* Input-cost instrument (INPUTTIME=1 builds): cumulative system-counter
 * ticks and calls inside netif->input - the heartbeat divides. */
uint64_t e1000e_netif_input_ticks;
unsigned long e1000e_netif_input_calls;

#include "e1000e.h"

static struct e1000e *nic_of(struct netif *netif)
{
	return (struct e1000e *)netif->state;
}

static err_t e1000e_netif_linkoutput(struct netif *netif, struct pbuf *p);

static err_t e1000e_netif_init(struct netif *netif)
{
	struct e1000e *nic = nic_of(netif);

	netif->name[0] = 'e';
	netif->name[1] = '0';
	netif->hwaddr_len = 6u;
	memcpy(netif->hwaddr, nic->mac, 6u);
	netif->mtu = E1000E_MTU;
	/* The ethernet/ARP flags are load-bearing: tcpip_input dispatches on
	 * them (tcpip.c:297-304) - without ETHARP every received frame is fed
	 * to ip_input with the payload still at the ethernet header, and dies
	 * at the IP version check (board evidence: ip rx=N drop=N, etharp
	 * rx=0, zero replies ever sent). The upstream ethernetif skeleton
	 * sets these in low_level_init; so does the GMAC bridge's eth_init. */
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
	netif->output = etharp_output;
	netif->linkoutput = e1000e_netif_linkoutput;

	return ERR_OK;
}

static err_t e1000e_netif_linkoutput(struct netif *netif, struct pbuf *p)
{
	return e1000e_tx_frame(nic_of(netif), p) == 0 ? ERR_OK : ERR_IF;
}

void e1000e_netif_add(struct netif *netif, struct e1000e *nic,
		      const ip4_addr_t *ip, const ip4_addr_t *mask,
		      const ip4_addr_t *gw)
{
	(void)netif_add(netif, ip, mask, gw, nic,
			e1000e_netif_init, tcpip_input);
	netif_set_default(netif);
	netif_set_up(netif);
}

unsigned int e1000e_netif_poll(struct netif *netif, unsigned int budget)
{
	struct e1000e *nic = nic_of(netif);
	unsigned int fed = 0u;
	int was_up = nic->link.up;

	e1000e_link_poll(nic);
	if (nic->link.up != was_up) {
		if (nic->link.up) {
			netif_set_link_up(netif);
		} else {
			netif_set_link_down(netif);
		}
	}

	while (fed < budget) {
		struct pbuf *p = e1000e_rx_take(nic);

		if (p == NULL) {
			break;
		}
		fed++;
#if defined(CONFIG_E1000E_INPUT_TIME)
		{
			uint64_t t0 = gtimer_count_get();
#endif
			if (netif->input(p, netif) != ERR_OK) {
				pbuf_free(p);
			}
#if defined(CONFIG_E1000E_INPUT_TIME)
			e1000e_netif_input_ticks += gtimer_count_get() - t0;
			e1000e_netif_input_calls++;
		}
#endif
	}
	e1000e_rx_kick(nic);

	return fed;
}
