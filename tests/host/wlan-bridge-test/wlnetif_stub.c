/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    wlnetif_stub.c
 * @brief   The presentation layer's stub test: the real ports/lwip/wlannetif.c
 *          compiled against the real lwIP headers, with the lwIP functions it
 *          calls answered by recording stubs. Its bridge side is the real
 *          bridge (this suite links it), so the layer is tested inside the
 *          chain: frames leave through net80211's own encap into the scripted
 *          driver's capture, and frames the stack delivers arrive here through
 *          the port's receive seam.
 *
 * @design stub substitution of the imported stack's surface. The stubs answer
 *         only what the layer calls (netif_add/set_up/set_down/set_link_*,
 *         pbuf_alloc/take/free, tcpip_input, etharp_output, dhcp_start/stop)
 *         and record each call; the unit under test is the production file, not
 *         a copy. The lwIP component itself is not in this link - its
 *         acceptance is the cross-build gate plus the board's dataplane, which
 *         is the line the design draws for the host tier.
 * @date    08.10.2026
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lwip/opt.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include "netif/ethernet.h"

#include "wlannetif.h"
#include "wlnetif_stub.h"

/* the ethernet frame the linkoutput step sends: header + a recognisable payload */
static const uint8_t stub_eth_hdr[14] = {
	0x02, 0x11, 0x22, 0x33, 0x44, 0x55,	/* the AP */
	0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee,	/* the station */
	0x08, 0x00
};
static const uint8_t stub_payload[16] = {
	'w', 'l', 'a', 'n', 'n', 'e', 't', 'i',
	'f', '-', 's', 't', 'u', 'b', 0, 0
};

static struct wlnetif_stub_result res;
static struct netif stub_netif;

/* ------------------------------------------------------------------ */
/* the lwIP surface the layer calls                                    */
/* ------------------------------------------------------------------ */

/* pbufs: one shelf entry per allocation; the payload borrows from a scratch
 * shelf the size the layer asked for (the layer allocates, then copies in) */
#define STUB_PBUF_MAX	4
#define STUB_PBUF_SZ	2048
static struct pbuf pbuf_shelf[STUB_PBUF_MAX];
static uint8_t pbuf_payload[STUB_PBUF_MAX][STUB_PBUF_SZ];
static unsigned int pbuf_used;

struct pbuf *
pbuf_alloc(pbuf_layer layer, u16_t length, pbuf_type type)
{
	struct pbuf *p;

	(void)layer;
	(void)type;
	if (pbuf_used >= STUB_PBUF_MAX || length > STUB_PBUF_SZ)
		return NULL;
	p = &pbuf_shelf[pbuf_used];
	memset(p, 0, sizeof(*p));
	p->payload = pbuf_payload[pbuf_used];
	p->tot_len = length;
	p->len = length;
	p->ref = 1;
	pbuf_used++;
	return p;
}

err_t
pbuf_take(struct pbuf *p, const void *data, u16_t len)
{

	if (p == NULL || p->payload == NULL || len > p->len)
		return ERR_MEM;
	memcpy(p->payload, data, len);
	return ERR_OK;
}

u8_t
pbuf_free(struct pbuf *p)
{

	if (p == NULL)
		return 0;
	p->ref = 0;
	return 1;
}

struct netif *
netif_add(struct netif *netif, const ip4_addr_t *ipaddr,
    const ip4_addr_t *netmask, const ip4_addr_t *gw, void *state,
    netif_init_fn init, netif_input_fn input)
{

	(void)state;
	if (netif == NULL || init == NULL)
		return NULL;
	memset(netif, 0, sizeof(*netif));
	if (init(netif) != ERR_OK)
		return NULL;
	if (ipaddr != NULL)
		netif->ip_addr = *ipaddr;
	if (netmask != NULL)
		netif->netmask = *netmask;
	if (gw != NULL)
		netif->gw = *gw;
	netif->input = input;
	return netif;
}

void
netif_set_up(struct netif *netif)
{

	netif->flags |= NETIF_FLAG_UP;
}

void
netif_set_down(struct netif *netif)
{

	netif->flags &= (u8_t)~NETIF_FLAG_UP;
}

void
netif_set_link_up(struct netif *netif)
{

	netif->flags |= NETIF_FLAG_LINK_UP;
	res.link_up_seen++;
}

void
netif_set_link_down(struct netif *netif)
{

	netif->flags &= (u8_t)~NETIF_FLAG_LINK_UP;
	res.link_down_seen++;
}

err_t
tcpip_input(struct pbuf *p, struct netif *netif)
{

	(void)netif;
	if (p == NULL || p->payload == NULL)
		return ERR_ARG;
	if (p->len > sizeof(res.rx_seen))
		return ERR_MEM;
	memcpy(res.rx_seen, p->payload, p->len);
	res.rx_input_frames++;
	return ERR_OK;
}

err_t
etharp_output(struct netif *netif, struct pbuf *q, const ip4_addr_t *ipaddr)
{

	(void)netif;
	(void)q;
	(void)ipaddr;
	return ERR_OK;
}

#if LWIP_DHCP
err_t
dhcp_start(struct netif *netif)
{

	(void)netif;
	res.dhcp_started++;
	return ERR_OK;
}

void
dhcp_stop(struct netif *netif)
{

	(void)netif;
	res.dhcp_stopped++;
}
#endif

/* ------------------------------------------------------------------ */
/* the steps the bridge suite's flow calls                             */
/* ------------------------------------------------------------------ */

int
wlnetif_stub_assemble(uint8_t mac_out[6])
{
	ip4_addr_t ip, mask, gw;

	memset(&res, 0, sizeof(res));
	memset(&pbuf_shelf, 0, sizeof(pbuf_shelf));
	pbuf_used = 0;
	memset(&stub_netif, 0, sizeof(stub_netif));

	IP4_ADDR(&ip, 192, 168, 0, 210);
	IP4_ADDR(&mask, 255, 255, 255, 0);
	IP4_ADDR(&gw, 192, 168, 0, 1);
	if (wlannetif_add(&stub_netif, &ip, &mask, &gw) != ERR_OK)
		return -1;
	res.hwaddr_ok = stub_netif.hwaddr_len == 6;
	if (stub_netif.linkoutput == NULL)
		return -1;
	if (mac_out != NULL)
		memcpy(mac_out, stub_netif.hwaddr, 6);
	return 0;
}

int
wlnetif_stub_linkoutput(void)
{
	struct pbuf p1, p2;

	/* a two-piece chain: the layer must linearise it before the bridge */
	memset(&p1, 0, sizeof(p1));
	memset(&p2, 0, sizeof(p2));
	p1.payload = (void *)stub_eth_hdr;
	p1.len = sizeof(stub_eth_hdr);
	p1.tot_len = (u16_t)(sizeof(stub_eth_hdr) + sizeof(stub_payload));
	p1.next = &p2;
	p2.payload = (void *)stub_payload;
	p2.len = sizeof(stub_payload);
	p2.tot_len = p2.len;
	res.tx_chain_calls++;
	if (stub_netif.linkoutput(&stub_netif, &p1) != ERR_OK)
		return -1;
	return 0;
}

void
wlnetif_stub_up(void)
{

	wlannetif_start(&stub_netif);
	res.up_flag = (stub_netif.flags & NETIF_FLAG_UP) != 0;
}

void
wlnetif_stub_down(void)
{
	unsigned long rx = 0, rx_fail = 0, tx = 0, tx_fail = 0;

	wlannetif_stop(&stub_netif);
	res.down_flag = (stub_netif.flags & NETIF_FLAG_UP) == 0;
	wlannetif_get_diag(&rx, &rx_fail, &tx, &tx_fail);
	res.diag_rx = (unsigned int)rx;
	res.diag_rx_fail = (unsigned int)rx_fail;
	res.diag_tx = (unsigned int)tx;
	res.diag_tx_fail = (unsigned int)tx_fail;
}

void
wlnetif_stub_get(struct wlnetif_stub_result *out)
{

	if (out == NULL)
		return;
	*out = res;
}

/* the flow reads the link flag right after each event arrives (the snapshot
 * alone cannot tell the two states apart, because both events have run) */
void
wlnetif_stub_note_link_up(void)
{

	res.link_assoc_flag = (stub_netif.flags & NETIF_FLAG_LINK_UP) != 0;
}

void
wlnetif_stub_note_link_down(void)
{

	res.link_down_flag = (stub_netif.flags & NETIF_FLAG_LINK_UP) == 0;
}
