/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   gmac_netif.c
 * @brief  lwIP netif bridge over the firefly board's on-SoC GMAC driver.
 * @design upstream ethernetif skeleton: netif_add with etharp_output/
 *          linkoutput and the driver's entries in the low-level slots.
 *          Threading follows the wired line's contract (the e1000e bridge
 *          on the same board): the RX poll thread runs the drain and
 *          netif->input is tcpip_input; the transmit side runs on the
 *          TCP/IP thread under the core lock. The driver's copy-based
 *          frame contract turns the receive drain into one pbuf_alloc plus
 *          one memcpy per frame.
 */

#include <string.h>

#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"

#include "gmac_netif.h"
#include "gmac.h"

static struct gmac *gmac_of(struct netif *netif)
{
	return (struct gmac *)netif->state;
}

static err_t gmac_netif_linkoutput(struct netif *netif, struct pbuf *p);

static err_t gmac_netif_init(struct netif *netif)
{
	struct gmac *m = gmac_of(netif);

	netif->name[0] = 'g';
	netif->name[1] = 'm';
	netif->hwaddr_len = 6u;
	memcpy(netif->hwaddr, m->mac, 6u);
	netif->mtu = GMAC_NETIF_MTU;
	/* The ethernet/ARP flags are load-bearing: tcpip_input dispatches on
	 * them - without ETHARP every received frame is fed to ip_input with
	 * the payload still at the ethernet header (the wired line's board
	 * evidence on the same stack). */
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
	netif->output = etharp_output;
	netif->linkoutput = gmac_netif_linkoutput;

	return ERR_OK;
}

static err_t gmac_netif_linkoutput(struct netif *netif, struct pbuf *p)
{
	struct gmac *m = gmac_of(netif);

	if (p->next != NULL) {
		/* The driver takes one contiguous frame; the stack's
		 * single-pbuf transmit discipline keeps this the common
		 * path, a chain is linearised here. */
		struct pbuf *q;
		size_t total = 0u;

		for (q = p; q != NULL; q = q->next) {
			total += q->len;
		}
		if (total > BOARD_GMAC_BUF_BYTES) {
			return ERR_MEM;
		}
		{
			static uint8_t linear[BOARD_GMAC_BUF_BYTES];
			size_t off = 0u;

			for (q = p; q != NULL; q = q->next) {
				memcpy(&linear[off], q->payload, q->len);
				off += q->len;
			}
			return gmac_tx_frame(m, linear, off) == 0 ? ERR_OK
								 : ERR_IF;
		}
	}

	return gmac_tx_frame(m, p->payload, p->len) == 0 ? ERR_OK : ERR_IF;
}

void gmac_netif_add(struct netif *netif, struct gmac *m,
		    const ip4_addr_t *ip, const ip4_addr_t *mask,
		    const ip4_addr_t *gw)
{
	(void)netif_add(netif, ip, mask, gw, m, gmac_netif_init, tcpip_input);
	netif_set_default(netif);
	netif_set_up(netif);
}

unsigned int gmac_netif_poll(struct netif *netif, unsigned int budget)
{
	struct gmac *m = gmac_of(netif);
	unsigned int fed = 0u;
	int was_up = m->link.up;

	gmac_link_poll(m);
	if (m->link.up != was_up) {
		if (m->link.up) {
			netif_set_link_up(netif);
		} else {
			netif_set_link_down(netif);
		}
	}

	while (fed < budget) {
		struct pbuf *p = pbuf_alloc(PBUF_RAW, BOARD_GMAC_BUF_BYTES,
					    PBUF_RAM);
		int len;

		if (p == NULL) {
			break;
		}
		len = gmac_rx_frame(m, p->payload, BOARD_GMAC_BUF_BYTES);
		if (len <= 0) {
			pbuf_free(p);
			break;
		}
		pbuf_realloc(p, (u16_t)len);
		fed++;
		if (netif->input(p, netif) != ERR_OK) {
			pbuf_free(p);
		}
	}

	return fed;
}
