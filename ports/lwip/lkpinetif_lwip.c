/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    lkpinetif_lwip.c
 * @brief   The stack half of the FullMAC presentation layer: it assembles the
 *          lwIP interface over the driver's net device and hands frames to the
 *          stack.
 *
 * @design template instantiation of the upstream netif skeleton, the same shape
 *         this directory's WLAN bridge uses (ports/lwip/wlannetif.c:178-227 the
 *         init/netif_add assembly, :77-98 the linkoutput linearization, :104-129
 *         the receive path into netif->input), with the interface's address and
 *         the transmit door taken from the adapter's other half through the seam
 *         header (lkpinetif_seam.h) instead of from a WiFi stack's dotted face.
 *         The split exists because lwIP's headers and the driver world's headers
 *         both own the same names (lwIP's htons against the contract header's), so
 *         no translation unit may see both.
 *
 * The DHCP note (wlannetif.c:18-22): the client starts when the interface is
 * brought up, and link state arrives from the driver's connect notification; the
 * address plan stays in the caller's hands.
 *
 * @date    12.10.2026
 */

#include <stddef.h>
#include <string.h>

#include "lwip/opt.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include "lwip/stats.h"
#include "lwip/dhcp.h"
#include "netif/ethernet.h"
#include "arch/sys_arch.h"

#include "lkpinetif.h"

#define LKPINETIF_MTU		1500u
#define LKPINETIF_FRAME_MAX	1600u	/* ethernet frame + margin */
#define LKPINETIF_ETH_ALEN	6

/* one linear staging buffer for the transmit direction (the stack serializes
 * linkoutput, so one buffer is this port's own shape) */
static uint8_t tx_linear[LKPINETIF_FRAME_MAX] __attribute__((aligned(8)));

static struct netif *g_netif;

int
lkpinetif_deliver(const uint8_t *frame, size_t len)
{
	struct pbuf *p;

	if (g_netif == NULL)
		return -1;
	if (frame == NULL || len == 0)
		return -2;
	p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
	if (p == NULL)
		return -2;
	if (pbuf_take(p, frame, (u16_t)len) != ERR_OK) {
		pbuf_free(p);
		return -2;
	}
	/* the core-locked stack: tcpip_input hands the frame to the TCP/IP thread's
	 * mailbox when the stack is up, and drops it when not */
	if (g_netif->input(p, g_netif) != ERR_OK) {
		pbuf_free(p);
		return -2;
	}
	return 0;
}

/* --- the core lock, with a ceiling ---------------------------------------- */

/*
 * LOCK_TCPIP_CORE() waits forever, and a wait that never ends prints nothing:
 * a wedged stack is indistinguishable from a slow one in the transcript. The
 * measurement paths take the lock through this pair instead, so a timeout can
 * name the holder (the owner record lives in sys_arch.c, where every take and
 * release of this lock is seen) and report the stack's own liveness.
 */
int
lkpinetif_core_lock_timeout(unsigned int ms)
{
	if (ms == 0u) {
		LOCK_TCPIP_CORE();
		return 0;
	}
	return sys_mutex_lock_timeout(&lock_tcpip_core, (u32_t)ms);
}

void
lkpinetif_core_unlock(void)
{
	UNLOCK_TCPIP_CORE();
}

const char *
lkpinetif_core_lock_owner(void)
{
	return sys_arch_core_lock_owner();
}

unsigned long
lkpinetif_core_lock_held_ms(void)
{
	return sys_arch_core_lock_held_ms();
}

unsigned long
lkpinetif_tcpip_heartbeat(void)
{
	return sys_arch_mbox_fetch_count();
}

static err_t
lkpinetif_linkoutput(struct netif *netif, struct pbuf *p)
{
	struct pbuf *q;
	uint32_t len = 0;

	(void)netif;
	if (p->tot_len > LKPINETIF_FRAME_MAX)
		return ERR_BUF;
	/* linearize the chain: the driver's transmit entry takes one contiguous
	 * frame, which is this port's documented shape */
	for (q = p; q != NULL; q = q->next) {
		memcpy(&tx_linear[len], q->payload, q->len);
		len += q->len;
	}
	if (lkpinetif_tx_frame(tx_linear, len) != 0)
		return ERR_IF;
	return ERR_OK;
}

static err_t
lkpinetif_init(struct netif *netif)
{
	uint8_t mac[LKPINETIF_ETH_ALEN];

	netif->mtu = LKPINETIF_MTU;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
	    NETIF_FLAG_ETHERNET;
	netif->name[0] = 'w';
	netif->name[1] = 'l';

	/* The interface's address: the driver set it from the chip before it
	 * registered, and its open path may fill it, so a placeholder here is the
	 * documented state of an interface that is not up yet, not a failure. */
	if (lkpinetif_hwaddr(mac) != 0 ||
	    (mac[0] == 0 && mac[1] == 0 && mac[2] == 0 && mac[3] == 0 &&
	    mac[4] == 0 && mac[5] == 0)) {
		mac[0] = 0x02;
		mac[1] = 0x00;
		mac[2] = 0x00;
		mac[3] = 0x00;
		mac[4] = 0x00;
		mac[5] = 0x01;
	}
	netif->hwaddr_len = LKPINETIF_ETH_ALEN;
	memcpy(netif->hwaddr, mac, LKPINETIF_ETH_ALEN);

	netif->output = etharp_output;
	netif->linkoutput = lkpinetif_linkoutput;
	return ERR_OK;
}

err_t
lkpinetif_add(struct netif *netif, const ip4_addr_t *ip,
    const ip4_addr_t *mask, const ip4_addr_t *gw)
{

	if (netif == NULL)
		return ERR_ARG;
	/*
	 * The netif is assembled exactly as the upstream skeleton does it: the init
	 * callback fills the interface, tcpip_input is the input path (the stack is
	 * core-locked with a TCP/IP thread in this port).
	 */
	if (netif_add(netif, ip, mask, gw, netif, lkpinetif_init,
	    tcpip_input) == NULL)
		return ERR_IF;
	g_netif = netif;
	return ERR_OK;
}

void
lkpinetif_start(struct netif *netif)
{

	if (netif == NULL)
		return;
	netif_set_up(netif);
	/* the address comes from the plan (DHCP in the board flow, static as the
	 * documented fallback); the client starts only when the interface is up,
	 * and link state arrives with the driver's connect notification */
#if LWIP_DHCP
	(void)dhcp_start(netif);
#endif
}

void
lkpinetif_stop(struct netif *netif)
{

	if (netif == NULL)
		return;
#if LWIP_DHCP
	dhcp_stop(netif);
#endif
	netif_set_down(netif);
}

void
lkpinetif_note_link(struct netif *netif, int up)
{

	if (netif == NULL)
		return;
	if (up)
		netif_set_link_up(netif);
	else
		netif_set_link_down(netif);
}

/*
 * The stack's counters, as scalars (lkpinetif_seam.h owns the shape and the
 * reason). lwIP's `memp` table holds pointers under this configuration, so the
 * pool's own figures are read through them.
 */
void
lkpinetif_get_stack_diag(struct lkpinetif_stack_diag *out)
{
	struct netif *n;
	unsigned int i = 0U;

	if (out == NULL)
		return;
	memset(out, 0, sizeof(*out));
	out->tcp_recv = lwip_stats.tcp.recv;
	out->tcp_xmit = lwip_stats.tcp.xmit;
	out->tcp_drop = lwip_stats.tcp.drop;
	out->tcp_memerr = lwip_stats.tcp.memerr;
	out->tcp_chkerr = lwip_stats.tcp.chkerr;
	out->tcp_lenerr = lwip_stats.tcp.lenerr;
	out->tcp_opterr = lwip_stats.tcp.opterr;
	out->tcp_err = lwip_stats.tcp.err;
	out->tcp_rterr = lwip_stats.tcp.rterr;
	if (lwip_stats.memp[MEMP_TCP_PCB] != NULL) {
		out->pcb_avail = (unsigned long)lwip_stats.memp[MEMP_TCP_PCB]->avail;
		out->pcb_err = lwip_stats.memp[MEMP_TCP_PCB]->err;
	}
	out->ip_drop = lwip_stats.ip.drop;
	out->ip_chkerr = lwip_stats.ip.chkerr;
	out->link_drop = lwip_stats.link.drop;
	for (n = netif_list; n != NULL && i < 3U; n = n->next, i++) {
		out->netif_up[i] = (n->flags & NETIF_FLAG_UP) ? 1UL : 0UL;
		out->netif_link[i] = (n->flags & NETIF_FLAG_LINK_UP) ? 1UL : 0UL;
		out->netif_ip[i] = (unsigned long)ip4_addr_get_u32(&n->ip_addr);
		out->netif_name[i][0] = n->name[0];
		out->netif_name[i][1] = n->name[1];
		out->netif_num[i] = (unsigned long)n->num;
	}
	out->netif_n = i;
}
