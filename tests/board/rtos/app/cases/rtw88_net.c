/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtw88_net.c
 * @brief  Case rtw88_net: the IP presentation layer over the net80211 bridge on
 *         the RTL8821CS - lwIP attached to the bridged interface, an address
 *         from the AP's DHCP (or a static one), and ICMP to the gateway.
 *
 * Why this file is an lwIP translation unit and not a driver one: lwIP's
 * headers and the driver world's headers own the same names, so no file may see
 * both.  The bridge's face is scalar-only (wlan_bridge.h -> seam.h), so this
 * file includes it and never a linuxkpi header:
 *   TX  linkoutput -> wlan_bridge_tx_raw() (the frame enters net80211's own
 *       encap/crypto path), under the port's serializer - the bridge does not
 *       take it for the raw entry and a data plane has more than one frame;
 *   RX  wlan_bridge_set_data_rx() hands back a complete ethernet frame, which
 *       this file copies into a pbuf and gives to the stack's input (no
 *       transmit happens on that context).
 * The low edge points at the bridge instead of a FullMAC driver's net device:
 * this port has no net_device per interface (linuxkpi_80211.c), so lkpinetif
 * cannot bind here.  The shape is the iwl7260/rtl8xxxu net cases', trimmed to
 * the first-packet gate: no iperf, no sockets.
 *
 * The address mode is a named profile fact, closed at build time (the platform
 * facts rule): CONFIG_RTW88_NET_ADDR_DHCP or CONFIG_RTW88_NET_ADDR_STATIC, and the static arm
 * also requires CONFIG_RTW88_NET_IP / CONFIG_RTW88_NET_MASK / CONFIG_RTW88_NET_GW.  Both arms are
 * present in this file and exactly one is compiled; a profile that names
 * neither fails the build rather than silently picking an address.
 *
 * Anchors:
 *   rtw88: net up mode=dhcp|static mac=<mac> mtu=1500
 *   rtw88: net dhcp wait t=<s> state=<n> tries=<n>            (INFO, every 5 s)
 *   rtw88: net dhcp bound ip=<ip> mask=<mask> gw=<gw> lease=<s>
 *   rtw88: net up ip=<ip> mask=<mask> gw=<gw>                 (static arm)
 *   rtw88: net arp gw=<ip> wait_ms=<n> seen=<n> entry=<idx>
 *   rtw88: net ping tx=10 rx=<n> ok_mask=<hex> min_ms=<n> max_ms=<n>
 *   rtw88: net counters tx=<n> txb=<n> txfail=<n> rx=<n> rxb=<n>
 *           rxfail=<n> bridge_data_tx=<n> bridge_tx_done=<n> bridge_rx=<n>
 *           bridge_rx_data=<n> bridge_tx_err=<n>
 *   rtw88: net PASS
 *
 * A failed gate is `rtw88: net FAIL <reason>` with the same counters, so "not
 * associated", "the netif never came up", "DHCP never answered", "the gateway
 * answered ARP but not ICMP" and "the echo path carried only part of the
 * probes" are told apart from the log alone.
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "wlan_bridge.h"

#if defined(RTOS_CASE_RTW88_NET)


/* The port's serializer (declared by the component's port.h; repeated here so
 * this lwIP translation unit stays free of the net80211 world's headers). */
void wlan_port_serializer_lock(void);
void wlan_port_serializer_unlock(void);

#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/prot/icmp.h"
#include "lwip/prot/ip4.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet_chksum.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"

/* The address mode is a profile fact, closed at build time: exactly one of the
 * two Kconfig symbols is on, and the static arm also names its address.  The
 * build's fragment checks the same thing before compiling; this block keeps the
 * rule in the file that depends on it. */
#if defined(CONFIG_RTW88_NET_ADDR_DHCP) && defined(CONFIG_RTW88_NET_ADDR_STATIC)
#error "CONFIG_RTW88_NET_ADDR names both dhcp and static: a profile picks exactly one"
#elif defined(CONFIG_RTW88_NET_ADDR_DHCP)
#elif defined(CONFIG_RTW88_NET_ADDR_STATIC)
#ifndef CONFIG_RTW88_NET_IP
#error "CONFIG_RTW88_NET_ADDR_STATIC requires CONFIG_RTW88_NET_IP (the board's address)"
#endif
#ifndef CONFIG_RTW88_NET_MASK
#error "CONFIG_RTW88_NET_ADDR_STATIC requires CONFIG_RTW88_NET_MASK"
#endif
#ifndef CONFIG_RTW88_NET_GW
#error "CONFIG_RTW88_NET_ADDR_STATIC requires CONFIG_RTW88_NET_GW (the AP-side gateway)"
#endif
#else
#error "CONFIG_RTW88_NET_ADDR_DHCP or CONFIG_RTW88_NET_ADDR_STATIC must be selected (a profile fact, no default)"
#endif

#define RTW88_NET_MTU		1500u
#define RTW88_NET_FRAME_MAX	1600u

#define RTW88_NET_PING_N	10u
#define RTW88_NET_PING_GAP_MS	500u
#define RTW88_NET_PING_WAIT_MS	800u
#define RTW88_NET_PING_ID	0x8821u
#define RTW88_NET_ARP_WAIT_MS	4000u
#define RTW88_NET_DHCP_WAIT_MS	40000u
#define RTW88_NET_POLL_MS	100u

#define RTW88_NET_RUN_STATE	4

static struct netif rtw_netif;
static osSemaphoreId_t rtw_stack_ready;

/* The transmit staging buffer: lwIP serialises linkoutput on the TCP/IP
 * thread, so one buffer is this port's shape (the other net cases keep one). */
static uint8_t rtw_tx_linear[RTW88_NET_FRAME_MAX] __attribute__((aligned(8)));

static uint8_t rtw_net_mac[6];

/* Accounting: the judge and the run report read these to place a failure on the
 * bridge, the netif or the stack. */
static volatile unsigned rtw_tx_frames;
static volatile unsigned rtw_tx_bytes;
static volatile unsigned rtw_tx_fail;
static volatile unsigned rtw_rx_frames;
static volatile unsigned rtw_rx_bytes;
static volatile unsigned rtw_rx_fail;

static int rtw_net_up;
static int rtw_net_ping_ok;

/* --- the stack ----------------------------------------------------------- */

static void
rtw_net_stack_done(void *arg)
{
	(void)arg;
	if (rtw_stack_ready != NULL)
		(void)osSemaphoreRelease(rtw_stack_ready);
}

static int
rtw_net_stack_start(void)
{

	rtw_stack_ready = osSemaphoreNew(1u, 0u, NULL);
	if (rtw_stack_ready == NULL)
		return -1;
	tcpip_init(rtw_net_stack_done, NULL);
	if (osSemaphoreAcquire(rtw_stack_ready, 5000u) != osOK)
		return -1;
	return 0;
}

/* --- the netif ----------------------------------------------------------- */

static err_t
rtw_net_link_output(struct netif *netif, struct pbuf *p)
{
	int rc;

	(void)netif;

	if (p->tot_len > (u16_t)sizeof(rtw_tx_linear)) {
		rtw_tx_fail++;
		return ERR_BUF;
	}
	if (pbuf_copy_partial(p, rtw_tx_linear, p->tot_len, 0) != p->tot_len) {
		rtw_tx_fail++;
		return ERR_BUF;
	}
	wlan_port_serializer_lock();
	rc = wlan_bridge_tx_raw(rtw_tx_linear, p->tot_len);
	wlan_port_serializer_unlock();
	if (rc < 0) {
		rtw_tx_fail++;
		return ERR_IF;
	}
	rtw_tx_frames++;
	rtw_tx_bytes += p->tot_len;
	return ERR_OK;
}

static err_t
rtw_net_if_init(struct netif *netif)
{

	netif->name[0] = 'w';
	netif->name[1] = 'l';
	netif->mtu = RTW88_NET_MTU;
	netif->hwaddr_len = 6u;
	(void)memcpy(netif->hwaddr, rtw_net_mac, 6u);
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
	    NETIF_FLAG_LINK_UP;
	netif->output = etharp_output;
	netif->linkoutput = rtw_net_link_output;
	return ERR_OK;
}

/*
 * The bridge's receive seam.  It runs on the driver's receive context, so it
 * only copies and posts; the stack's input call enqueues the frame on the
 * TCP/IP thread (core-locked input is deferred in this build) and nothing here
 * takes the serializer or transmits.
 */
static void
rtw_net_rx(const uint8_t *frame, size_t len, void *arg)
{
	struct netif *netif = arg;
	struct pbuf *p;

	if (netif == NULL || frame == NULL || len == 0u) {
		rtw_rx_fail++;
		return;
	}
	p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
	if (p == NULL) {
		rtw_rx_fail++;
		return;
	}
	if (pbuf_take(p, frame, (u16_t)len) != ERR_OK) {
		pbuf_free(p);
		rtw_rx_fail++;
		return;
	}
	if (netif->input(p, netif) != ERR_OK) {
		pbuf_free(p);
		rtw_rx_fail++;
		return;
	}
	rtw_rx_frames++;
	rtw_rx_bytes += (unsigned)len;
}

/* --- ICMP ---------------------------------------------------------------- */

static struct raw_pcb *rtw_ping_pcb;
static ip_addr_t rtw_ap_ip;
static volatile unsigned rtw_ping_tx;
static volatile unsigned rtw_ping_rx;
static volatile unsigned rtw_ping_seen;
static volatile unsigned rtw_ping_ok_mask;
static uint64_t rtw_ping_sent_us[RTW88_NET_PING_N + 1u];
static unsigned rtw_ping_rtt_min_us;
static unsigned rtw_ping_rtt_max_us;

/*
 * The raw receive contract has two parts, and both matter:
 *  - the payload a raw pcb is handed still starts at the IP header, so the
 *    ICMP header is at payload + IP header length;
 *  - a non-zero return says this callback consumed the pbuf (and freed it),
 *    zero says lwIP still owns it and frees it after the raw walk.  A branch
 *    that frees and returns zero frees the same pbuf twice.
 */
static u8_t
rtw_net_ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p,
    const ip_addr_t *addr)
{
	struct ip_hdr *iphdr;
	struct icmp_echo_hdr *iecho;
	u16_t hlen;
	u16_t seq;
	uint64_t now;

	(void)arg;
	(void)pcb;
	(void)addr;

	if (p == NULL || p->len < (u16_t)sizeof(struct ip_hdr))
		return 0u;
	iphdr = (struct ip_hdr *)p->payload;
	if (IPH_V(iphdr) != 4u)
		return 0u;
	hlen = IPH_HL_BYTES(iphdr);
	if (p->len < (u16_t)(hlen + sizeof(struct icmp_echo_hdr)))
		return 0u;
	iecho = (struct icmp_echo_hdr *)((u8_t *)p->payload + hlen);
	if ((ICMPH_TYPE(iecho) != ICMP_ER) ||
	    (iecho->id != PP_HTONS(RTW88_NET_PING_ID)))
		return 0u;

	rtw_ping_seen++;
	seq = lwip_ntohs(iecho->seqno);
	if ((seq != 0u) && (seq <= RTW88_NET_PING_N) &&
	    (rtw_ping_sent_us[seq] != 0u) &&
	    ((rtw_ping_ok_mask & (1u << seq)) == 0u)) {
		unsigned rtt_us;

		now = rtos_clock_us();
		rtt_us = (unsigned)(now - rtw_ping_sent_us[seq]);
		if ((rtw_ping_rtt_min_us == 0u) || (rtt_us < rtw_ping_rtt_min_us))
			rtw_ping_rtt_min_us = rtt_us;
		if (rtt_us > rtw_ping_rtt_max_us)
			rtw_ping_rtt_max_us = rtt_us;
		rtw_ping_ok_mask |= (1u << seq);
		rtw_ping_rx++;
	}
	pbuf_free(p);
	return 1u;	/* consumed: we freed it above */
}

static void
rtw_net_ping_send(u16_t seq)
{
	struct pbuf *p;
	struct icmp_echo_hdr *iecho;
	u16_t len = (u16_t)(sizeof(struct icmp_echo_hdr) + 8u);

	p = pbuf_alloc(PBUF_IP, len, PBUF_RAM);
	if (p == NULL)
		return;
	iecho = (struct icmp_echo_hdr *)p->payload;
	ICMPH_TYPE_SET(iecho, ICMP_ECHO);
	ICMPH_CODE_SET(iecho, 0);
	iecho->chksum = 0;
	iecho->id = PP_HTONS(RTW88_NET_PING_ID);
	iecho->seqno = lwip_htons(seq);
	(void)memset((uint8_t *)p->payload + sizeof(struct icmp_echo_hdr),
	    0xa5, 8u);
	iecho->chksum = inet_chksum(iecho, p->len);

	rtw_ping_sent_us[seq] = rtos_clock_us();
	LOCK_TCPIP_CORE();
	if (raw_sendto(rtw_ping_pcb, p, &rtw_ap_ip) == ERR_OK) {
		/* seq 0 is the throwaway probe that makes the stack resolve the
		 * gateway; it is not one of the counted echoes, so the ping line
		 * can be read as "tx counted, rx answered" */
		if (seq != 0u)
			rtw_ping_tx++;
	}
	UNLOCK_TCPIP_CORE();
	pbuf_free(p);
}

static int
rtw_net_ping(unsigned int count)
{
	unsigned int i;

	for (i = 1u; i <= count; i++) {
		unsigned int before = rtw_ping_rx;
		unsigned int waited;

		rtw_net_ping_send((u16_t)i);
		for (waited = 0u;
		     (waited < RTW88_NET_PING_WAIT_MS) && (rtw_ping_rx == before);
		     waited += 20u)
			(void)osDelay(20u);
		if (i < count)
			(void)osDelay(RTW88_NET_PING_GAP_MS);
	}
	return (rtw_ping_rx == count) ? 0 : -1;
}

/* --- the case ------------------------------------------------------------ */

static void
rtw_net_counters_line(void)
{
	struct wlan_bridge_counters c;

	wlan_bridge_stats(&c);
	CASE_ANCHOR("rtw88: net counters tx=%u txb=%u txfail=%u rx=%u rxb=%u "
	    "rxfail=%u bridge_data_tx=%u bridge_tx_done=%u bridge_rx=%u "
	    "bridge_rx_data=%u bridge_tx_err=%u\n", rtw_tx_frames,
	    rtw_tx_bytes, rtw_tx_fail, rtw_rx_frames, rtw_rx_bytes,
	    rtw_rx_fail, c.data_tx, c.tx_completes, c.rx_frames, c.rx_data,
	    c.tx_errors);
}

static int
rtw_net_bringup(void)
{
	ip4_addr_t ip;
	ip4_addr_t mask;
	ip4_addr_t gw;
	struct wlan_bridge_counters c;
	int ping_ok;

	if (rtw_net_up != 0)
		return rtw_net_ping_ok != 0 ? 0 : -1;

	wlan_bridge_stats(&c);
	if (c.attached == 0u) {
		CASE_ANCHOR("rtw88: net FAIL not attached\n");
		return -1;
	}
	if (wlan_bridge_state() != RTW88_NET_RUN_STATE) {
		CASE_ANCHOR("rtw88: net FAIL not associated (state=%d)\n",
		    wlan_bridge_state());
		return -1;
	}
	if (wlan_bridge_hwaddr(rtw_net_mac) != 0) {
		CASE_ANCHOR("rtw88: net FAIL no hardware address\n");
		return -1;
	}
	if (rtw_net_stack_start() != 0) {
		CASE_ANCHOR("rtw88: net FAIL tcpip init\n");
		return -1;
	}

#if defined(CONFIG_RTW88_NET_ADDR_DHCP)
	ip4_addr_set_zero(&ip);
	ip4_addr_set_zero(&mask);
	ip4_addr_set_zero(&gw);
#else
	if ((ip4addr_aton(CONFIG_RTW88_NET_IP, &ip) == 0) ||
	    (ip4addr_aton(CONFIG_RTW88_NET_MASK, &mask) == 0) ||
	    (ip4addr_aton(CONFIG_RTW88_NET_GW, &gw) == 0)) {
		CASE_ANCHOR("rtw88: net FAIL address config ip=\"%s\" "
		    "mask=\"%s\" gw=\"%s\"\n", CONFIG_RTW88_NET_IP, CONFIG_RTW88_NET_MASK,
		    CONFIG_RTW88_NET_GW);
		return -1;
	}
#endif

	LOCK_TCPIP_CORE();
	if (netif_add(&rtw_netif, &ip, &mask, &gw, NULL, rtw_net_if_init,
	    tcpip_input) == NULL) {
		UNLOCK_TCPIP_CORE();
		CASE_ANCHOR("rtw88: net FAIL netif add\n");
		return -1;
	}
	netif_set_default(&rtw_netif);
	netif_set_up(&rtw_netif);
	netif_set_link_up(&rtw_netif);
#if defined(CONFIG_RTW88_NET_ADDR_DHCP)
	if (dhcp_start(&rtw_netif) != ERR_OK) {
		UNLOCK_TCPIP_CORE();
		CASE_ANCHOR("rtw88: net FAIL dhcp start\n");
		return -1;
	}
#endif
	UNLOCK_TCPIP_CORE();

	/* The receive seam is registered only after the netif exists: a frame
	 * that arrived between the two would be dropped by the seam's own
	 * netif-NULL guard. */
	wlan_bridge_set_data_rx(rtw_net_rx, &rtw_netif);

#if defined(CONFIG_RTW88_NET_ADDR_DHCP)
	CASE_ANCHOR("rtw88: net up mode=dhcp mac=%02x:%02x:%02x:%02x:%02x:%02x "
	    "mtu=%u\n", rtw_net_mac[0], rtw_net_mac[1], rtw_net_mac[2],
	    rtw_net_mac[3], rtw_net_mac[4], rtw_net_mac[5], RTW88_NET_MTU);
#else
	CASE_ANCHOR("rtw88: net up mode=static ip=%u.%u.%u.%u mask=%u.%u.%u.%u "
	    "gw=%u.%u.%u.%u mac=%02x:%02x:%02x:%02x:%02x:%02x mtu=%u\n",
	    ip4_addr1(&ip), ip4_addr2(&ip), ip4_addr3(&ip), ip4_addr4(&ip),
	    ip4_addr1(&mask), ip4_addr2(&mask), ip4_addr3(&mask),
	    ip4_addr4(&mask), ip4_addr1(&gw), ip4_addr2(&gw), ip4_addr3(&gw),
	    ip4_addr4(&gw), rtw_net_mac[0], rtw_net_mac[1], rtw_net_mac[2],
	    rtw_net_mac[3], rtw_net_mac[4], rtw_net_mac[5], RTW88_NET_MTU);
	ip4_addr_copy(*ip_2_ip4(&rtw_ap_ip), gw);
#endif

#if defined(CONFIG_RTW88_NET_ADDR_DHCP)
	{
		unsigned int waited = 0u;
		int bound = 0;

		while (waited < RTW88_NET_DHCP_WAIT_MS) {
			LOCK_TCPIP_CORE();
			bound = dhcp_supplied_address(&rtw_netif);
			UNLOCK_TCPIP_CORE();
			if (bound)
				break;
			(void)osDelay(RTW88_NET_POLL_MS);
			waited += RTW88_NET_POLL_MS;
			if ((waited % 5000u) == 0u) {
				struct dhcp *dhcp;
				unsigned int state = 0u;
				unsigned int tries = 0u;

				LOCK_TCPIP_CORE();
				dhcp = netif_dhcp_data(&rtw_netif);
				if (dhcp != NULL) {
					state = dhcp->state;
					tries = dhcp->tries;
				}
				UNLOCK_TCPIP_CORE();
				CASE_INFO("rtw88: net dhcp wait t=%us state=%u "
				    "tries=%u\n", waited / 1000u, state, tries);
			}
		}
		if (!bound) {
			struct dhcp *dhcp;
			unsigned int state = 0u;
			unsigned int tries = 0u;

			LOCK_TCPIP_CORE();
			dhcp = netif_dhcp_data(&rtw_netif);
			if (dhcp != NULL) {
				state = dhcp->state;
				tries = dhcp->tries;
			}
			UNLOCK_TCPIP_CORE();
			CASE_ANCHOR("rtw88: net FAIL dhcp timeout state=%u "
			    "tries=%u tx=%u\n", state, tries, rtw_tx_frames);
			return -1;
		}
		{
			uint32_t lease;

			LOCK_TCPIP_CORE();
			ip4_addr_copy(ip, *ip_2_ip4(&rtw_netif.ip_addr));
			ip4_addr_copy(mask, *ip_2_ip4(&rtw_netif.netmask));
			ip4_addr_copy(gw, *ip_2_ip4(&rtw_netif.gw));
			ip_addr_copy(rtw_ap_ip, rtw_netif.gw);
			lease = netif_dhcp_data(&rtw_netif) != NULL ?
			    netif_dhcp_data(&rtw_netif)->offered_t0_lease : 0u;
			UNLOCK_TCPIP_CORE();
			CASE_ANCHOR("rtw88: net dhcp bound ip=%u.%u.%u.%u "
			    "mask=%u.%u.%u.%u gw=%u.%u.%u.%u lease=%us\n",
			    ip4_addr1(&ip), ip4_addr2(&ip), ip4_addr3(&ip),
			    ip4_addr4(&ip), ip4_addr1(&mask),
			    ip4_addr2(&mask), ip4_addr3(&mask),
			    ip4_addr4(&mask), ip4_addr1(&gw), ip4_addr2(&gw),
			    ip4_addr3(&gw), ip4_addr4(&gw), (unsigned)lease);
		}
	}
#endif

	rtw_net_up = 1;

	/* The AP's ARP entry must exist before the first counted echo: lwIP
	 * drops an unresolved-ARP packet without queueing it.  The throwaway
	 * echo (seq 0) is what makes the stack resolve the gateway; its answer
	 * is the "ARP + ICMP round trip" evidence. */
	rtw_ping_pcb = raw_new(IP_PROTO_ICMP);
	if (rtw_ping_pcb == NULL) {
		CASE_ANCHOR("rtw88: net FAIL raw pcb\n");
		return -1;
	}
	raw_recv(rtw_ping_pcb, rtw_net_ping_recv, NULL);
	{
		unsigned int before = rtw_ping_seen;
		unsigned int waited = 0u;

		rtw_net_ping_send(0u);
		while ((waited < RTW88_NET_ARP_WAIT_MS) &&
		       (rtw_ping_seen == before)) {
			(void)osDelay(20u);
			waited += 20u;
		}
		{
			struct eth_addr *eth = NULL;
			const ip4_addr_t *ipret = NULL;
			ssize_t idx;

			LOCK_TCPIP_CORE();
			idx = etharp_find_addr(&rtw_netif, ip_2_ip4(&rtw_ap_ip),
			    &eth, &ipret);
			UNLOCK_TCPIP_CORE();
			CASE_ANCHOR("rtw88: net arp gw=%u.%u.%u.%u "
			    "wait_ms=%u seen=%u entry=%d\n",
			    ip4_addr1(ip_2_ip4(&rtw_ap_ip)),
			    ip4_addr2(ip_2_ip4(&rtw_ap_ip)),
			    ip4_addr3(ip_2_ip4(&rtw_ap_ip)),
			    ip4_addr4(ip_2_ip4(&rtw_ap_ip)), waited,
			    rtw_ping_seen - before, (int)idx);
		}
	}

	ping_ok = rtw_net_ping(RTW88_NET_PING_N);
	CASE_ANCHOR("rtw88: net ping tx=%u rx=%u ok_mask=%03x min_ms=%u "
	    "max_ms=%u\n", rtw_ping_tx, rtw_ping_rx, rtw_ping_ok_mask,
	    rtw_ping_rtt_min_us / 1000u, rtw_ping_rtt_max_us / 1000u);
	rtw_net_ping_ok = (ping_ok == 0) ? 1 : 0;
	return rtw_net_ping_ok != 0 ? 0 : -1;
}

int
case_rtw88_net_run(void)
{

	if (rtw_net_bringup() != 0) {
		rtw_net_counters_line();
		CASE_ANCHOR("rtw88: net FAIL bringup\n");
		return 1;
	}
	rtw_net_counters_line();
	CASE_ANCHOR("rtw88: net PASS\n");
	return 0;
}

#endif /* RTOS_CASE_RTW88_NET */
