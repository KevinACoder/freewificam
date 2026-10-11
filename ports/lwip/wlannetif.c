/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    wlannetif.c
 * @brief   lwIP netif over the WLAN bridge: the presentation layer that makes
 *          the net80211 interface look like an ethernet device to lwIP.
 *
 * @design template instantiation of the upstream netif skeleton, the same shape
 *         the GMAC bridge in this directory uses (ports/lwip/ethnetif.c:78-116
 *         the eth_init/linkoutput pair, :118-125 the netif_add assembly), with
 *         the low-level slots filled against the WLAN bridge's dotted interface
 *         instead of a register-level driver: linkoutput -> wlan_bridge_tx_raw
 *         (the frame enters net80211's own encap/crypto path), the receive call
 *         -> netif->input, and the interface's address from the bridge. Where
 *         the GMAC bridge polls a driver ring, this one is called: the bridge
 *         delivers a received ethernet frame through wlan_port_set_data_rx
 *         (the port's receive seam), so the frame arrives here as a callback.
 *
 * The DHCP note: lwIP's dhcp client is started when the interface is brought up
 * and link state is up (the bridge reports ASSOC through the port's event seam).
 * The board's address plan keeps both paths (DHCP first, the image's static
 * fallback) in the caller's hands, which is why this file starts the client
 * rather than assigning an address itself.
 *
 * @date    08.10.2026
 */

#include <stddef.h>
#include <string.h>

#include "lwip/opt.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include "netif/ethernet.h"

#include <port/port.h>

#include "wlannetif.h"

/* The bridge's own declarations: this file must not include the net80211
 * world's headers (it is an lwIP-port translation unit), and the bridge's
 * dotted face is deliberately free of them. */
int	wlan_bridge_tx_raw(const uint8_t *frame, size_t len);
int	wlan_bridge_hwaddr(uint8_t addr[6]);

#define WLANNETIF_MTU		1500u
#define WLANNETIF_FRAME_MAX	1600u	/* ethernet frame + margin */

/* one linear staging buffer per direction (the callbacks are serialized by the
 * port's serializer, so one buffer each is the port's own shape) */
static uint8_t tx_linear[WLANNETIF_FRAME_MAX] __attribute__((aligned(8)));

/* loss counters, so a receive loss localizes to the pool, the input call or the
 * bridge without a debugger (the GMAC bridge's heartbeat shape) */
static unsigned long g_rx_frames;
static unsigned long g_rx_input_fail;
static unsigned long g_tx_frames;
static unsigned long g_tx_fail;

void
wlannetif_get_diag(unsigned long *rx_frames, unsigned long *rx_input_fail,
    unsigned long *tx_frames, unsigned long *tx_fail)
{

	*rx_frames = g_rx_frames;
	*rx_input_fail = g_rx_input_fail;
	*tx_frames = g_tx_frames;
	*tx_fail = g_tx_fail;
}

/* ------------------------------------------------------------------ */
/* transmit                                                            */
/* ------------------------------------------------------------------ */

static err_t
wlan_linkoutput(struct netif *netif, struct pbuf *p)
{
	uint32_t len = 0;
	struct pbuf *q;

	(void)netif;
	if (p->tot_len > WLANNETIF_FRAME_MAX)
		return ERR_BUF;
	/* linearize the chain: the bridge (and net80211 behind it) takes one
	 * contiguous frame, which is this port's documented shape */
	for (q = p; q != NULL; q = q->next) {
		memcpy(&tx_linear[len], q->payload, q->len);
		len += q->len;
	}
	if (wlan_bridge_tx_raw(tx_linear, len) < 0) {
		g_tx_fail++;
		return ERR_IF;
	}
	g_tx_frames++;
	return ERR_OK;
}

/* ------------------------------------------------------------------ */
/* receive (the port's receive seam calls this)                        */
/* ------------------------------------------------------------------ */

static void
wlan_data_rx(const uint8_t *frame, size_t len, void *arg)
{
	struct netif *netif = (struct netif *)arg;
	struct pbuf *p;

	g_rx_frames++;
	if (netif == NULL || frame == NULL || len == 0)
		return;
	p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
	if (p == NULL) {
		g_rx_input_fail++;
		return;
	}
	if (pbuf_take(p, frame, (u16_t)len) != ERR_OK) {
		pbuf_free(p);
		g_rx_input_fail++;
		return;
	}
	/* the core-locked stack: tcpip_input hands the frame to the TCP/IP
	 * thread's mailbox when the stack is up, and drops it when not */
	if (netif->input(p, netif) != ERR_OK) {
		pbuf_free(p);
		g_rx_input_fail++;
	}
}

/* The association steps, callable by whoever owns the port's single event
 * slot: this layer's own handler below, or the supplicant backend forwarding
 * through wlan_wpa_netif_bind in a WPA image. The callback runs in the
 * bridge's context, so this only sets state the TCP/IP thread will act on. */
void
wlannetif_note_assoc(struct netif *netif)
{

	if (netif != NULL)
		netif_set_link_up(netif);
}

void
wlannetif_note_disassoc(struct netif *netif)
{

	if (netif != NULL)
		netif_set_link_down(netif);
}

/* The port's event seam: the bridge reports scan/assoc/disassoc through it. On
 * association the link is up and the DHCP client starts; on disassociation it
 * stops. */
static void
wlan_event(enum wlan_port_event event, const uint8_t *addr, void *arg)
{
	struct netif *netif = (struct netif *)arg;

	(void)addr;
	if (netif == NULL)
		return;
	switch (event) {
	case WLAN_PORT_ASSOC:
		wlannetif_note_assoc(netif);
		break;
	case WLAN_PORT_DISASSOC:
		wlannetif_note_disassoc(netif);
		break;
	case WLAN_PORT_SCAN_DONE:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* init / assembly                                                     */
/* ------------------------------------------------------------------ */

static err_t
wlan_netif_init(struct netif *netif)
{
	uint8_t mac[6];

	netif->mtu = WLANNETIF_MTU;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
	    NETIF_FLAG_ETHERNET;
	netif->name[0] = 'w';
	netif->name[1] = 'l';

	if (wlan_bridge_hwaddr(mac) != 0) {
		/* not attached yet: the interface still comes up so a later
		 * attach finds it, with a locally administered placeholder */
		mac[0] = 0x02;
		mac[1] = 0x00;
		mac[2] = 0x00;
		mac[3] = 0x00;
		mac[4] = 0x00;
		mac[5] = 0x01;
	}
	netif->hwaddr_len = 6;
	memcpy(netif->hwaddr, mac, 6);

	netif->output = etharp_output;
	netif->linkoutput = wlan_linkoutput;
	return ERR_OK;
}

err_t
wlannetif_add(struct netif *netif, const ip4_addr_t *ip,
    const ip4_addr_t *mask, const ip4_addr_t *gw)
{

	if (netif == NULL)
		return ERR_ARG;
	/*
	 * The netif is assembled exactly as the upstream skeleton does it: the
	 * init callback fills the interface, tcpip_input is the input path (the
	 * stack is core-locked with a TCP/IP thread in this port).
	 */
	if (netif_add(netif, ip, mask, gw, netif, wlan_netif_init,
	    tcpip_input) == NULL)
		return ERR_IF;
	/* the port's receive and event seams: the bridge delivers frames and
	 * events through them (registered after the netif exists) */
	wlan_port_set_data_rx(wlan_data_rx, netif);
	wlan_port_set_event_handler(wlan_event, netif);
	return ERR_OK;
}

void
wlannetif_start(struct netif *netif)
{

	if (netif == NULL)
		return;
	netif_set_up(netif);
	/* the address comes from the plan (DHCP in the board flow, static as
	 * the documented fallback); the client starts only when the interface
	 * is up, and link state arrives with the ASSOC event */
#if LWIP_DHCP
	(void)dhcp_start(netif);
#endif
}

void
wlannetif_stop(struct netif *netif)
{

	if (netif == NULL)
		return;
#if LWIP_DHCP
	dhcp_stop(netif);
#endif
	netif_set_down(netif);
}
