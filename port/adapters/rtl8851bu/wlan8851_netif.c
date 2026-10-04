/*
 * @file   wlan8851_netif.c
 * @brief  lwIP netif bridge over the rtl8851bu vendor driver (S5b W5
 *         precursor, mirrored from the rtl8821cu sibling's W5 shape).
 *
 * The RX seam: the KPI netif_rx sink (registered at adapter bind) routes
 * non-EAPOL frames here; this file copies the skb payload into a pbuf and
 * hands it to netif->input (tcpip_input, callable from any thread -
 * NO_SYS=0 posts to the tcpip mailbox).
 *
 * The TX seam: netif->linkoutput linearizes the pbuf chain into one static
 * buffer, wraps it in a kpi skb (netdev_alloc_skb + skb_put, the shape the
 * sibling line proved) and hands it to the vendor's own ndo_start_xmit.
 * Every byte the DMA engine sees comes from the vendor's own allocator.
 *
 * Lifecycle: the netif is added lazily on the first ASSOC event (the MAC
 * is efuse-read by then), set_up + dhcp_start; DISASSOC does dhcp_stop +
 * set_down.  netif management runs in the tcpip thread via tcpip_callback
 * (LWIP_TCPIP_CORE_LOCKING=1), same discipline as the sibling bridge.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <drv_types.h>

#include "rtl8851bu_port.h"

#ifdef CONFIG_WLAN_NETIF

#include "lwip/opt.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"
#include "netif/ethernet.h"

extern void *rtl8851bu_get_adapter(void);

/* one frame, linearized (the sibling bridge's ceiling shape) */
#define WLAN_TX_LINEAR_SZ	1600u

static struct netif	g_wlan_netif;
static int		g_wlan_added;
static int		g_wlan_up;
static unsigned long	g_wlan_rx_fed;
static unsigned long	g_wlan_rx_drop;
static unsigned long	g_wlan_last_rx_ms;
static unsigned long	g_wlan_tx_frames;
static unsigned long	g_wlan_tx_err;

/* ------------------------------------------------------------------ */
/* TX: pbuf chain -> vendor xmit entry                                 */

static err_t
wlan8851_linkoutput(struct netif *netif, struct pbuf *p)
{
	static uint8_t linear[WLAN_TX_LINEAR_SZ];
	_adapter *a = (_adapter *)rtl8851bu_get_adapter();
	struct net_device *ndev;
	struct sk_buff *skb;
	u16 total;
	uint8_t *dst;

	(void)netif;
	if (a == NULL || (ndev = a->pnetdev) == NULL)
		return ERR_IF;
	total = p->tot_len;
	if (total == 0 || total > sizeof(linear))
		return ERR_MEM;
	if (pbuf_copy_partial(p, linear, total, 0) != total)
		return ERR_BUF;

	skb = netdev_alloc_skb(ndev, (unsigned int)total);
	if (skb == NULL) {
		g_wlan_tx_err++;
		return ERR_MEM;
	}
	dst = skb_put(skb, (unsigned int)total);
	memcpy(dst, linear, total);
	if (ndev->netdev_ops == NULL ||
	    ndev->netdev_ops->ndo_start_xmit == NULL) {
		kfree_skb(skb);
		g_wlan_tx_err++;
		return ERR_IF;
	}
	(void)ndev->netdev_ops->ndo_start_xmit(skb, ndev);
	g_wlan_tx_frames++;
	return ERR_OK;
}

static err_t
wlan8851_netif_init(struct netif *netif)
{
	_adapter *a = (_adapter *)rtl8851bu_get_adapter();

	netif->mtu = 1500;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
		       NETIF_FLAG_LINK_UP;
	netif->output = etharp_output;
	netif->linkoutput = wlan8851_linkoutput;
	if (a != NULL) {
		memcpy(netif->hwaddr, adapter_mac_addr(a), ETH_ALEN);
	}
	netif->hwaddr_len = ETH_ALEN;
	return ERR_OK;
}

/* ------------------------------------------------------------------ */
/* lifecycle (tcpip thread)                                            */

static void
wlan8851_assoc_cb(void *arg)
{
	(void)arg;

	if (!g_wlan_added) {
		if (netif_add(&g_wlan_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4,
			      IP4_ADDR_ANY4, NULL, wlan8851_netif_init,
			      tcpip_input) == NULL) {
			printf("wlan netif: add FAILED\n");
			return;
		}
		netif_set_default(&g_wlan_netif);
		g_wlan_added = 1;
	}
	netif_set_up(&g_wlan_netif);
	netif_set_link_up(&g_wlan_netif);
	g_wlan_up = 1;
	printf("wlan netif: up mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
	       g_wlan_netif.hwaddr[0], g_wlan_netif.hwaddr[1],
	       g_wlan_netif.hwaddr[2], g_wlan_netif.hwaddr[3],
	       g_wlan_netif.hwaddr[4], g_wlan_netif.hwaddr[5]);
	(void)dhcp_start(&g_wlan_netif);
	printf("wlan netif: dhcp start\n");
}

static void
wlan8851_disassoc_cb(void *arg)
{
	(void)arg;

	if (!g_wlan_added)
		return;
	dhcp_stop(&g_wlan_netif);
	netif_set_link_down(&g_wlan_netif);
	netif_set_down(&g_wlan_netif);
	g_wlan_up = 0;
	printf("wlan netif: down (disassoc)\n");
}

void
wlan8851_wlan_netif_on_assoc(void)
{
	if (g_wlan_added == 0 || g_wlan_up == 0) {
		(void)tcpip_callback(wlan8851_assoc_cb, NULL);
	}
}

void
wlan8851_wlan_netif_on_disassoc(void)
{
	if (g_wlan_added != 0 && g_wlan_up != 0) {
		(void)tcpip_callback(wlan8851_disassoc_cb, NULL);
	}
}

/* ------------------------------------------------------------------ */
/* RX: sink -> pbuf -> netif->input (any thread; tcpip_input posts)     */

/* Consumed = 0 (the skb is freed here); -1 = not consumed (caller keeps
 * ownership).  Only called for non-EAPOL frames from the netif_rx sink. */
int
wlan8851_wlan_netif_input(struct sk_buff *skb)
{
	struct pbuf *p;

	if (skb == NULL || skb->data == NULL || skb->len == 0)
		return -1;
	if (g_wlan_added == 0 || g_wlan_up == 0 || skb->len > 1600) {
		g_wlan_rx_drop++;
		return -1;
	}
	/* the vendor indicates through eth_type_trans: the 14-byte
	 * Ethernet header was PULLED - un-pull before feeding lwIP (the
	 * sibling line's W5 lesson, board-proven there) */
	skb->data -= 14;
	skb->len += 14;
	skb->tail -= 14;
	p = pbuf_alloc(PBUF_RAW, (u16_t)skb->len, PBUF_POOL);
	if (p == NULL) {
		g_wlan_rx_drop++;
		return -1;
	}
	(void)pbuf_take(p, skb->data, skb->len);
	if (g_wlan_netif.input(p, &g_wlan_netif) != ERR_OK) {
		pbuf_free(p);
		g_wlan_rx_drop++;
		return -1;
	}
	g_wlan_rx_fed++;
	g_wlan_last_rx_ms = jiffies;
	kfree_skb(skb);
	return 0;
}

#ifndef CONFIG_NIC_WPA_BACKEND
/* the single-sink seam consumer: EAPOL falls through as not-consumed
 * (the sibling line's wpa filter mirrors; without CONFIG_WPA_SUPPLICANT
 * in this lane EAPOL is simply counted-and-dropped by the sink caller).
 * When the wpa backend IS in the image it owns the single sink and
 * composes this input itself (wpa_vendor_rtl8851bu.c) - this consumer
 * then stays uncompiled. */
static int
wlan8851_netif_rx_sink(struct sk_buff *skb)
{
	if (skb == NULL)
		return 1; /* NET_RX_DROP */
	if (skb->len >= 16 && skb->data != NULL) {
		/* un-pull once to inspect/match the wire bytes, then hand on
		 * with the header intact (wlan8851_wlan_netif_input expects
		 * the pulled state and un-pulls again - so re-pull here) */
		skb->data -= 14;
		skb->len += 14;
		skb->tail -= 14;
		if (skb->data[12] == 0x88 && skb->data[13] == 0x8e) {
			/* EAPOL: no supplicant in this lane yet; count+drop */
			kfree_skb(skb);
			return 1;
		}
		skb->data += 14;
		skb->len -= 14;
		skb->tail += 14;
	}
	if (wlan8851_wlan_netif_input(skb) == 0)
		return 0;
	kfree_skb(skb);
	return 1;
}

void
wlan8851_wlan_netif_bind(void)
{

	lkpi_netif_rx_sink_register(wlan8851_netif_rx_sink);
	printf("rtl8851bu: wlan netif rx sink registered\n");
}
#endif /* !CONFIG_NIC_WPA_BACKEND */

void
wlan8851_wlan_netif_stats(unsigned long *rx_fed, unsigned long *rx_drop,
			  unsigned long *tx_frames, unsigned long *tx_err)
{
	if (rx_fed != NULL)
		*rx_fed = g_wlan_rx_fed;
	if (rx_drop != NULL)
		*rx_drop = g_wlan_rx_drop;
	if (tx_frames != NULL)
		*tx_frames = g_wlan_tx_frames;
	if (tx_err != NULL)
		*tx_err = g_wlan_tx_err;
}

unsigned long
wlan8851_wlan_netif_last_rx_ms(void)
{

	return g_wlan_last_rx_ms;
}

/* ------------------------------------------------------------------ */
/* DHCP-state probe: values are copied in the tcpip thread (the DHCP   */
/* control block is core-owned); the shell reads the snapshot after    */
/* the callback ran.  Runs synchronously when already on the tcpip     */
/* thread, so the callback may fire inline - the snapshot pattern      */
/* makes that harmless. */

static volatile int	g_ip_state;	/* dhcp_state_enum_t of the lease */
static ip4_addr_t	g_ip_addr;	/* snapshot, network byte order */
static ip4_addr_t	g_ip_gw;
static volatile int	g_ip_changed;	/* bump per probe == "reported" */

static void
wlan8851_ip_snapshot_cb(void *arg)
{
	struct dhcp *dhcp;

	(void)arg;
	g_ip_changed++;
	if (g_wlan_added == 0) {
		g_ip_state = -1;
		return;
	}
	g_ip_addr = *netif_ip4_addr(&g_wlan_netif);
	g_ip_gw = *netif_ip4_gw(&g_wlan_netif);
	dhcp = netif_dhcp_data(&g_wlan_netif);
	g_ip_state = (dhcp != NULL) ? (int)dhcp->state : -1;
}

/* one line: netif up/added + IPv4 + DHCP state machine state */
int
wlan8851_wlan_netif_ip(void)
{
	int changed = g_ip_changed;
	unsigned long deadline = jiffies + 2000UL;

	if (tcpip_callback(wlan8851_ip_snapshot_cb, NULL) != ERR_OK) {
		printf("wlan netif: ip probe failed (tcpip busy)\n");
		return -1;
	}
	/* callback ran on the core; give the thread a moment if queued */
	while (g_ip_changed == changed && jiffies < deadline)
		;

	if (g_wlan_added == 0) {
		printf("wlan netif: not added (no assoc yet)\n");
		return -1;
	}
	printf("wlan netif: %s ip=%u.%u.%u.%u gw=%u.%u.%u.%u dhcp_state=%d\n",
	       g_wlan_up ? "up" : "down",
	       (unsigned)ip4_addr1(&g_ip_addr), (unsigned)ip4_addr2(&g_ip_addr),
	       (unsigned)ip4_addr3(&g_ip_addr), (unsigned)ip4_addr4(&g_ip_addr),
	       (unsigned)ip4_addr1(&g_ip_gw), (unsigned)ip4_addr2(&g_ip_gw),
	       (unsigned)ip4_addr3(&g_ip_gw), (unsigned)ip4_addr4(&g_ip_gw),
	       (int)g_ip_state);
	/* 10 = DHCP_STATE_BOUND (lwip/prot/dhcp.h) */
	return (g_ip_state == 10) ? 0 : -1;
}

#endif /* CONFIG_WLAN_NETIF */
