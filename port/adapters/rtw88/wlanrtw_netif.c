/*
 * wlanrtw_netif.c - lwIP netif bridge over the rtw88/net80211 bridge
 * (S9 W5, [S8] FR-7 seam instantiated on the rtw88 line; shape mirrored
 * from the 8851bu sibling's board-proven wlan8851_netif.c).
 *
 * RX seam: net80211's deliver path (if_percpuq_enqueue -> the data sink
 * registered here - EAPOL frames branch off to the wpa sink before this
 * one sees them) hands up whole ethernet frames; they are copied into a
 * pbuf and fed to netif->input (tcpip_input, callable from any thread -
 * NO_SYS=0 posts to the tcpip mailbox).
 *
 * TX seam: netif->linkoutput linearizes the pbuf chain into one static
 * buffer and injects it into net80211's if_snd (the same single channel
 * EAPOL uses, S8 FR-12): encap + CCMP + the driver relay all run there.
 *
 * Lifecycle: netif lazily added/up on the first ASSOC event (MAC is the
 * efuse address by then), dhcp_start; DISASSOC stops it.  netif
 * management runs in the tcpip thread via tcpip_callback.
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
#include "lwip/prot/dhcp.h"
#include "netif/ethernet.h"

#include "net80211_bridge.h"
#include "net80211_port.h"

#ifdef CONFIG_WLAN_NETIF

#define WLAN_TX_LINEAR_SZ	1600u

static struct netif	g_wlan_netif;
static int		g_wlan_added;
static int		g_wlan_up;
static unsigned long	g_wlan_rx_fed;
static unsigned long	g_wlan_rx_drop;
static unsigned long	g_wlan_tx_frames;
static unsigned long	g_wlan_tx_err;

/* ------------------------------------------------------------------ */
/* TX: pbuf chain -> net80211 data channel                             */

static err_t
wlanrtw_linkoutput(struct netif *netif, struct pbuf *p)
{
	static uint8_t linear[WLAN_TX_LINEAR_SZ];
	u16_t copied;

	(void)netif;
	if (p->tot_len > WLAN_TX_LINEAR_SZ) {
		g_wlan_tx_err++;
		return ERR_MEM;
	}
	copied = pbuf_copy_partial(p, linear, p->tot_len, 0);
	if (copied != p->tot_len) {
		g_wlan_tx_err++;
		return ERR_VAL;
	}
	if (net80211_bridge_tx_raw(linear, p->tot_len) != 0) {
		g_wlan_tx_err++;
		return ERR_IF;
	}
	g_wlan_tx_frames++;
	return ERR_OK;
}

static err_t
wlanrtw_netif_init(struct netif *netif)
{
	uint8_t mac[6];

	netif->mtu = 1500;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
	    NETIF_FLAG_LINK_UP;
	netif->output = etharp_output;
	netif->linkoutput = wlanrtw_linkoutput;
	if (net80211_bridge_get_hwaddr(mac) == 0)
		memcpy(netif->hwaddr, mac, 6);
	netif->hwaddr_len = 6;
	return ERR_OK;
}

/* ------------------------------------------------------------------ */
/* lifecycle (tcpip thread)                                            */

static void
wlanrtw_assoc_cb(void *arg)
{

	(void)arg;
	if (!g_wlan_added) {
		if (netif_add(&g_wlan_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4,
		    IP4_ADDR_ANY4, NULL, wlanrtw_netif_init,
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
wlanrtw_disassoc_cb(void *arg)
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
wlanrtw_netif_on_assoc(void)
{

	if (g_wlan_added == 0 || g_wlan_up == 0)
		(void)tcpip_callback(wlanrtw_assoc_cb, NULL);
}

void
wlanrtw_netif_on_disassoc(void)
{

	if (g_wlan_added != 0 && g_wlan_up != 0)
		(void)tcpip_callback(wlanrtw_disassoc_cb, NULL);
}

/* ------------------------------------------------------------------ */
/* RX: data sink -> pbuf -> netif->input                               */

static void
wlanrtw_netif_data_sink(const void *buf, size_t len, void *arg)
{
	struct pbuf *p;

	(void)arg;
	if (g_wlan_added == 0 || g_wlan_up == 0 || len > 1600) {
		g_wlan_rx_drop++;
		return;
	}
	p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
	if (p == NULL) {
		g_wlan_rx_drop++;
		return;
	}
	(void)pbuf_take(p, buf, len);
	if (g_wlan_netif.input(p, &g_wlan_netif) != ERR_OK) {
		pbuf_free(p);
		g_wlan_rx_drop++;
		return;
	}
	g_wlan_rx_fed++;
}

void
wlanrtw_netif_bind(void)
{

	net80211_port_set_data_rx(wlanrtw_netif_data_sink, NULL);
	printf("rtw88u: wlan netif data sink registered\n");
}

void
wlanrtw_netif_stats(unsigned long *rx_fed, unsigned long *rx_drop,
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

/* ------------------------------------------------------------------ */
/* DHCP-state probe (snapshot in the tcpip thread, shell reads after)  */

static volatile int	g_ip_state;	/* dhcp_state_enum_t */
static ip4_addr_t	g_ip_addr;
static ip4_addr_t	g_ip_gw;
static volatile int	g_ip_changed;

static void
wlanrtw_ip_snapshot_cb(void *arg)
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

int
wlanrtw_netif_ip(void)
{
	int changed = g_ip_changed;
	unsigned long deadline = sys_now() + 2000;

	if (tcpip_callback(wlanrtw_ip_snapshot_cb, NULL) != ERR_OK) {
		printf("wlan netif: ip probe failed (tcpip busy)\n");
		return -1;
	}
	while (g_ip_changed == changed && sys_now() < deadline)
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

#ifndef CONFIG_RTW88_WPA_BACKEND
/* single-event-slot composition: with the wpa backend in the image IT
 * owns the event handler and forwards assoc/dissoc here; a netif-only
 * image registers its own */
static void
wlanrtw_netif_event_sink(enum net80211_port_event event, const void *mac,
    void *arg)
{

	(void)mac;
	(void)arg;
	switch (event) {
	case NET80211_PORT_ASSOC:
		wlanrtw_netif_on_assoc();
		break;
	case NET80211_PORT_DISASSOC:
		wlanrtw_netif_on_disassoc();
		break;
	default:
		break;
	}
}

void
wlanrtw_netif_bind_events(void)
{

	net80211_port_set_event_handler(wlanrtw_netif_event_sink, NULL);
}
#endif /* !CONFIG_RTW88_WPA_BACKEND */

#endif /* CONFIG_WLAN_NETIF */
