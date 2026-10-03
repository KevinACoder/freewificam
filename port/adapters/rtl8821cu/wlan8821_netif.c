/*
 * @file   wlan8821_netif.c
 * @brief  lwIP netif bridge over the rtl8821cu vendor driver (S5a W5).
 *
 * The RX seam: the KPI netif_rx sink (registered in
 * wpa_vendor_rtl8821cu.c) routes non-EAPOL frames here; this file copies
 * the skb payload into a pbuf and hands it to netif->input (tcpip_input,
 * callable from any thread - NO_SYS=0 posts to the tcpip mailbox).
 *
 * The TX seam: netif->linkoutput linearizes the pbuf chain into one static
 * buffer, wraps it in a kpi skb (netdev_alloc_skb + skb_put, the shape the
 * EAPOL xmit already proved) and hands it to the vendor's own ndo_start_xmit.
 * Every byte the DMA engine sees comes from the vendor's own allocator.
 *
 * Lifecycle: the netif is added lazily on the first ASSOC event (the MAC is
 * efuse-read by then), set_up + dhcp_start; DISASSOC does dhcp_stop +
 * set_down.  netif management runs in the tcpip thread via tcpip_callback
 * (LWIP_TCPIP_CORE_LOCKING=1), same discipline as app/lwip_lan.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <drv_types.h>

#include "rtl8821cu_port.h"

#ifdef CONFIG_WLAN_NETIF

#include "lwip/opt.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include "netif/ethernet.h"

extern void *rtl8821cu_get_adapter(void);

/* one frame, linearized (the ethnetif.c ceiling shape) */
#define WLAN_TX_LINEAR_SZ	1600u

static struct netif	g_wlan_netif;
static int		g_wlan_added;
static int		g_wlan_up;
static unsigned long	g_wlan_rx_fed;
static unsigned long	g_wlan_rx_drop;
static unsigned long	g_wlan_tx_frames;
static unsigned long	g_wlan_tx_err;

static struct net_device *
wlan8821_ndev(void)
{
	_adapter *a = (_adapter *)rtl8821cu_get_adapter();

	return (a != NULL) ? a->pnetdev : NULL;
}

/* ------------------------------------------------------------------ */
/* TX: pbuf chain -> vendor xmit entry                                 */

static err_t
wlan8821_linkoutput(struct netif *netif, struct pbuf *p)
{
	static uint8_t linear[WLAN_TX_LINEAR_SZ];
	_adapter *a = (_adapter *)rtl8821cu_get_adapter();
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
wlan8821_netif_init(struct netif *netif)
{
	_adapter *a = (_adapter *)rtl8821cu_get_adapter();

	netif->mtu = 1500;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
		       NETIF_FLAG_LINK_UP;
	netif->output = etharp_output;
	netif->linkoutput = wlan8821_linkoutput;
	if (a != NULL) {
		memcpy(netif->hwaddr, adapter_mac_addr(a), ETH_ALEN);
	}
	netif->hwaddr_len = ETH_ALEN;
	return ERR_OK;
}

/* ------------------------------------------------------------------ */
/* lifecycle (tcpip thread)                                            */

static void
wlan8821_assoc_cb(void *arg)
{
	(void)arg;

	if (!g_wlan_added) {
		if (netif_add(&g_wlan_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4,
			      IP4_ADDR_ANY4, NULL, wlan8821_netif_init,
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
wlan8821_disassoc_cb(void *arg)
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
rtl8821cu_wlan_netif_on_assoc(void)
{
	if (g_wlan_added == 0 || g_wlan_up == 0) {
		(void)tcpip_callback(wlan8821_assoc_cb, NULL);
	}
}

void
rtl8821cu_wlan_netif_on_disassoc(void)
{
	if (g_wlan_added != 0 && g_wlan_up != 0) {
		(void)tcpip_callback(wlan8821_disassoc_cb, NULL);
	}
}

/* ------------------------------------------------------------------ */
/* RX: sink -> pbuf -> netif->input (any thread; tcpip_input posts)     */

/* Consumed = 0 (the skb is freed here); -1 = not consumed (caller keeps
 * ownership).  Only called for non-EAPOL frames from the netif_rx sink. */
int
rtl8821cu_wlan_netif_input(struct sk_buff *skb)
{
	struct pbuf *p;

	if (skb == NULL || skb->data == NULL || skb->len == 0)
		return -1;
	if (g_wlan_added == 0 || g_wlan_up == 0 || skb->len > 1600) {
		g_wlan_rx_drop++;
		return -1;
	}
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
	kfree_skb(skb);
	return 0;
}

void
rtl8821cu_wlan_netif_stats(unsigned long *rx_fed, unsigned long *rx_drop,
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

#endif /* CONFIG_WLAN_NETIF */
