/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   xmac_netif.c
 * @brief  lwIP netif bridge over the firefly v2 board's on-SoC xmac driver.
 * @design upstream ethernetif skeleton: netif_add with etharp_output/
 *          linkoutput and the driver's entries in the low-level slots.
 *          Threading follows the board family's other wired NIC: the RX poll
 *          thread runs the drain and netif->input is tcpip_input; the
 *          transmit side runs on the TCP/IP thread under the core lock. The
 *          driver's copy-based frame contract turns the receive drain into
 *          one pbuf_alloc plus one memcpy per frame.
 */

#include <string.h>

#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"

#include "xmac_netif.h"
#include "xmac.h"

static struct xmac *xmac_of(struct netif *netif)
{
	return (struct xmac *)netif->state;
}

/* Interrupt entry for the board's IRQ trampoline: hand it netif->state. Kept
 * here rather than in the case file so the netif <-> driver pairing stays in
 * one place. */
int xmac_netif_irq(void *state)
{
	return xmac_irq_ack((struct xmac *)state);
}

static err_t xmac_netif_linkoutput(struct netif *netif, struct pbuf *p);

static err_t xmac_netif_init(struct netif *netif)
{
	struct xmac *m = xmac_of(netif);

	netif->name[0] = 'x';
	netif->name[1] = 'm';
	netif->hwaddr_len = 6u;
	memcpy(netif->hwaddr, m->mac, 6u);
	netif->mtu = XMAC_NETIF_MTU;
	/* The ethernet/ARP flags are load-bearing: tcpip_input dispatches on
	 * them - without ETHARP every received frame is fed to ip_input with
	 * the payload still at the ethernet header. */
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
	/* This driver computes no checksums in hardware; the netif must say so
	 * explicitly when the build runs the per-netif checksum control (a
	 * zero-initialised netif leaves every checksum disabled - frames leave
	 * with a zero TCP checksum and the peer drops them silently; ARP has no
	 * L4 checksum, which is why ARP keeps working). */
	NETIF_SET_CHECKSUM_CTRL(netif, NETIF_CHECKSUM_ENABLE_ALL);
	netif->output = etharp_output;
	netif->linkoutput = xmac_netif_linkoutput;

	return ERR_OK;
}

static err_t xmac_netif_linkoutput(struct netif *netif, struct pbuf *p)
{
	struct xmac *m = xmac_of(netif);

	if (p->next != NULL) {
		/* The driver takes one contiguous frame; the stack's
		 * single-pbuf transmit discipline keeps this the common path,
		 * a chain is linearised here. */
		struct pbuf *q;
		size_t total = 0u;

		for (q = p; q != NULL; q = q->next) {
			total += q->len;
		}
		if (total > BOARD_XMAC_BUF_BYTES) {
			return ERR_MEM;
		}
		{
			static uint8_t linear[BOARD_XMAC_BUF_BYTES];
			size_t off = 0u;

			for (q = p; q != NULL; q = q->next) {
				memcpy(&linear[off], q->payload, q->len);
				off += q->len;
			}
			return xmac_tx_frame(m, linear, off) == 0 ? ERR_OK
								  : ERR_IF;
		}
	}

	return xmac_tx_frame(m, p->payload, p->len) == 0 ? ERR_OK : ERR_IF;
}

void xmac_netif_add(struct netif *netif, struct xmac *m,
		    const ip4_addr_t *ip, const ip4_addr_t *mask,
		    const ip4_addr_t *gw)
{
	(void)netif_add(netif, ip, mask, gw, m, xmac_netif_init, tcpip_input);
	netif_set_default(netif);
	netif_set_up(netif);
}

/* Where received frames die, one counter per step of the receive path. The
 * driver's own rx_dropped cannot see any of these - it counts ring misses only,
 * so a stack-side squeeze reads as "the NIC received everything" while TCP
 * processes a fraction of it. Bumped from the poll thread only. */
static unsigned long rx_pbuf_fail;
static unsigned long rx_ring_empty;
static unsigned long rx_input_fail;
static unsigned long rx_fed;
static unsigned long rx_polls;

void xmac_netif_rx_diag(unsigned long *pbuf_fail, unsigned long *ring_empty,
			unsigned long *input_fail, unsigned long *fed,
			unsigned long *polls)
{
	*pbuf_fail = rx_pbuf_fail;
	*ring_empty = rx_ring_empty;
	*input_fail = rx_input_fail;
	*fed = rx_fed;
	*polls = rx_polls;
}

/* How often the PHY's link bit is resampled, in lwIP milliseconds. A link
 * transition is a state change, not per-frame work. */
#define LINK_POLL_MS			1000u
static uint32_t last_link_poll_ms;

unsigned int xmac_netif_poll(struct netif *netif, unsigned int budget)
{
	struct xmac *m = xmac_of(netif);
	unsigned int fed = 0u;
	int was_up = m->link.up;

	rx_polls++;
	/* The link check is a clause-22 MDIO transaction pair (two reads, tens of
	 * microseconds each on this MAC). Running it once per drain put that in
	 * front of every poll - more than the drain of a typical burst, on the
	 * one path that has to keep up with the wire. Resample it on a wall-clock
	 * cadence instead: the first poll of each second refreshes the state, and
	 * the counters downstream are cumulative so nothing is missed in between. */
	if ((uint32_t)(sys_now() - last_link_poll_ms) >= LINK_POLL_MS) {
		last_link_poll_ms = sys_now();
		xmac_link_poll(m);
	}
	/* One drain = whatever the ring holds, not a fixed budget: the callback
	 * that asked for this drain has already coalesced a burst, so stopping at
	 * a frame count would leave the rest for a wake-up that may not come. */
	if (m->link.up != was_up) {
		if (m->link.up) {
			netif_set_link_up(netif);
		} else {
			netif_set_link_down(netif);
		}
	}

	/* Drain until the ring is empty, the way the upstream ethernetif input
	 * loop does (it only stops when low_level_input returns NULL). A frame
	 * budget here would leave the remainder of a burst waiting for a wake
	 * that the masked receive line cannot deliver: the handler mutes the
	 * line before this runs, so nothing re-enters until the re-arm below.
	 * The budget argument is therefore a non-binding ceiling, not a
	 * throttle. */
	while (fed < budget) {
		struct pbuf *p;
		int len;

		p = pbuf_alloc(PBUF_RAW, BOARD_XMAC_BUF_BYTES, PBUF_POOL);
		if (p == NULL) {
			rx_pbuf_fail++;
			break;
		}

		len = xmac_rx_frame(m, p->payload, BOARD_XMAC_BUF_BYTES);
		if (len <= 0) {
			rx_ring_empty++;
			pbuf_free(p);
			break;
		}
		pbuf_realloc(p, (u16_t)len);
		fed++;
		rx_fed++;
		if (netif->input(p, netif) != ERR_OK) {
			/* tcpip_input says ERR_MEM when the input mailbox or its
			 * message pool is full - the frame is gone and nothing
			 * upstream ever hears about it. */
			rx_input_fail++;
			pbuf_free(p);
		}
	}

	/* Re-open the receive line: the handler masks it, because a level
	 * receive-complete line stays asserted while any completed descriptor is
	 * still owned by software, so leaving it enabled across the drain would
	 * just re-enter the handler until the ring emptied. Re-arming after the
	 * drain is also what makes a frame that arrived *during* the drain get a
	 * fresh interrupt rather than wait for the next one. */
	xmac_rx_rearm(m);
	return fed;
}
