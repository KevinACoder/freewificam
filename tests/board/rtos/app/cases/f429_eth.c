/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * f429_eth.c - the F429's wired Ethernet: PHY, link, DHCP and the internet.
 *
 * The bottom half (MAC/DMA/PHY, the ST HAL) is the board driver's; this case
 * is the ladder in one boot, with one anchor per rung:
 *
 *   f429_eth: init rc=0                      the HAL brought the MAC and PHY up
 *   f429_eth: phy id=0007:c0f1 addr=0        MDIO answered, the PHY is the one
 *                                            the schematic names, at its strap
 *   f429_eth: mac=…                          UID-derived unicast address
 *   f429_eth: link speed=100 duplex=1        auto-negotiation result
 *   f429_eth: dhcp ip=… mask=… gw=…          the router's lease
 *   f429_eth: ping dst=<gw> tx=10 rx=10      the gateway, ARP resolved
 *   f429_eth: dns name=… rc=0 ip=…           (report only) name resolution
 *   f429_eth: ping dst=<public> tx=10 rx=≥9  the internet, the gate
 *   f429_eth: stats …                        driver + bridge counters
 *   f429_eth: PASS DATA
 *
 * The gate is the public echo reply; the gateway and the DNS line are
 * reported but do not gate (a router that does not answer pings to itself,
 * or a resolver the bench does not reach, is not a driver failure).  The
 * companion witness "the Pi pings the board's lease" runs on the host side
 * after the boot and is recorded next to the serial log.
 *
 * Threads: the runner thread runs this function; a receive thread drains the
 * board driver into lwIP every millisecond, and the stack's own TCP/IP
 * thread does the protocol work.  The case holds the lwIP core lock around
 * every stack call it makes from the runner thread.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "cases.h"

#include "eth.h"
#include "stm32eth_netif.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/ip.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet_chksum.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/tcpip.h"
#include "lwip/sys.h"

#include "arch/sys_arch.h"

#include "os/cmsis2/cmsis2.h"

/* The bench facts this ladder is written against (see docs/F429-ETH.md):
 * the router is the DHCP server and the gateway; the public targets are the
 * first four that a Chinese lab LAN usually reaches, tried in order until one
 * answers.  None of them is a board fact, which is why the gate is "at least
 * one". */
#define ETH_DHCP_WAIT_MS	20000U
#define ETH_PING_COUNT		10U
#define ETH_PING_TIMEOUT_MS	1000U
#define ETH_PING_PAYLOAD	32U
#define ETH_PING_ID		0x4337U
#define ETH_DNS_WAIT_MS		5000U
#define ETH_DNS_NAME		"www.baidu.com"

static const char *const eth_public_targets[] = {
	"223.5.5.5",
	"119.29.29.29",
	"8.8.8.8",
	"1.1.1.1",
};

static struct netif s_netif;
static struct stm32eth_netif_stats s_bridge_stats;
static osThreadId_t s_rx_thread;

/* --- one raw ICMP pcb for every round in this boot ------------------------ */

static struct raw_pcb *s_ping_pcb;
static ip_addr_t s_ping_dst;
static volatile unsigned s_ping_rx;
static volatile uint16_t s_ping_expected;

static void eth_reason(const char *reason)
{
	CASE_ANCHOR("f429_eth: FAIL %s\n", reason);
}

static void eth_copy_str(char *dst, unsigned int cap, const char *src)
{
	unsigned int n = (unsigned int)strlen(src);

	if (n >= cap) {
		n = cap - 1U;
	}
	memcpy(dst, src, n);
	dst[n] = '\0';
}

/* --- the receive thread --------------------------------------------------- */

static void eth_rx_thread(void *arg)
{
	(void)arg;
	for (;;) {
		unsigned int fed = stm32eth_netif_poll(&s_netif,
						       F429_ETH_RX_RING);
		/* Work present: drain again immediately.  Idle: one tick, which
		 * bounds the reply latency at ~1 ms and costs one wakeup. */
		(void)osDelay((fed != 0U) ? 0U : 1U);
	}
}

/* --- ping ----------------------------------------------------------------- */

static u8_t eth_ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p,
			  const ip_addr_t *addr)
{
	struct ip_hdr *iphdr;
	struct icmp_echo_hdr *iecho;
	u16_t hlen;

	(void)arg;
	(void)pcb;
	(void)addr;

	if (p == NULL || p->len < sizeof(struct ip_hdr)) {
		return 0;
	}
	iphdr = (struct ip_hdr *)p->payload;
	if (IPH_V(iphdr) != 4U) {
		return 0;
	}
	hlen = IPH_HL_BYTES(iphdr);
	if (p->len < (u16_t)(hlen + sizeof(struct icmp_echo_hdr))) {
		return 0;
	}

	iecho = (struct icmp_echo_hdr *)((u8_t *)p->payload + hlen);
	if (ICMPH_TYPE(iecho) == ICMP_ER &&
	    iecho->id == PP_HTONS(ETH_PING_ID) &&
	    addr != NULL && ip_addr_cmp(addr, &s_ping_dst) &&
	    lwip_ntohs(iecho->seqno) == s_ping_expected) {
		s_ping_rx++;
		pbuf_free(p);
		return 1;
	}
	return 0;
}

static int eth_ping_pcb_up(void)
{
	if (s_ping_pcb != NULL) {
		return 0;
	}
	LOCK_TCPIP_CORE();
	s_ping_pcb = raw_new(IP_PROTO_ICMP);
	if (s_ping_pcb != NULL) {
		raw_recv(s_ping_pcb, eth_ping_recv, NULL);
	}
	UNLOCK_TCPIP_CORE();
	return (s_ping_pcb != NULL) ? 0 : -1;
}

static int eth_ping_send(uint16_t seq)
{
	struct pbuf *p;
	struct icmp_echo_hdr *iecho;
	uint8_t *payload;
	u16_t len = (u16_t)(sizeof(struct icmp_echo_hdr) + ETH_PING_PAYLOAD);
	unsigned int i;
	err_t err;

	p = pbuf_alloc(PBUF_IP, len, PBUF_RAM);
	if (p == NULL) {
		return -1;
	}

	iecho = (struct icmp_echo_hdr *)p->payload;
	ICMPH_TYPE_SET(iecho, ICMP_ECHO);
	ICMPH_CODE_SET(iecho, 0);
	iecho->chksum = 0;
	iecho->id = PP_HTONS(ETH_PING_ID);
	iecho->seqno = lwip_htons(seq);

	payload = (uint8_t *)p->payload + sizeof(struct icmp_echo_hdr);
	for (i = 0U; i < ETH_PING_PAYLOAD; i++) {
		payload[i] = (uint8_t)(0xa5U ^ (uint8_t)(seq + i));
	}
	iecho->chksum = inet_chksum(iecho, p->len);

	LOCK_TCPIP_CORE();
	err = raw_sendto(s_ping_pcb, p, &s_ping_dst);
	UNLOCK_TCPIP_CORE();

	pbuf_free(p);
	return (err == ERR_OK) ? 0 : -1;
}

/*
 * `count` echoes, one at a time, `timeout_ms` each.  One warm-up echo goes
 * out first and is not counted: the first packet is what triggers ARP, and
 * lwIP drops the echoes that race the unresolved entry.  RTT is lwIP's
 * millisecond clock, which is the resolution the console prints.
 */
static int eth_ping(const char *dst, unsigned int count, unsigned int *tx_out,
		    unsigned int *rx_out, unsigned int *min_out,
		    unsigned int *avg_out, unsigned int *max_out)
{
	ip4_addr_t target;
	unsigned int tx = 0U;
	unsigned int rx = 0U;
	unsigned int rtt_n = 0U;
	unsigned int rtt_sum = 0U;
	unsigned int rtt_min = 0U;
	unsigned int rtt_max = 0U;
	unsigned int i;

	if (eth_ping_pcb_up() != 0 || !ip4addr_aton(dst, &target)) {
		*tx_out = 0U;
		*rx_out = 0U;
		*min_out = 0U;
		*avg_out = 0U;
		*max_out = 0U;
		return -1;
	}
	ip_addr_copy_from_ip4(s_ping_dst, target);

	if (eth_ping_send(0U) == 0) {
		(void)osDelay(500U);	/* let ARP resolve before counting */
	}

	s_ping_rx = 0U;
	for (i = 1U; i <= count; i++) {
		uint32_t sent_ms;
		unsigned int waited = 0U;

		s_ping_expected = (uint16_t)i;
		sent_ms = sys_now();
		if (eth_ping_send((uint16_t)i) != 0) {
			continue;
		}
		tx++;
		while (s_ping_rx < i && waited < ETH_PING_TIMEOUT_MS) {
			(void)osDelay(1U);
			waited++;
		}
		if (s_ping_rx >= i) {
			unsigned int rtt = (unsigned int)(sys_now() - sent_ms);

			rx++;
			rtt_n++;
			rtt_sum += rtt;
			if (rtt_n == 1U || rtt < rtt_min) {
				rtt_min = rtt;
			}
			if (rtt > rtt_max) {
				rtt_max = rtt;
			}
		}
		(void)osDelay(20U);
	}

	*tx_out = tx;
	*rx_out = rx;
	*min_out = rtt_min;
	*avg_out = (rtt_n != 0U) ? (rtt_sum / rtt_n) : 0U;
	*max_out = rtt_max;
	return (rx != 0U) ? 0 : -1;
}

/* --- name resolution (report only) ---------------------------------------- */

#if LWIP_DNS
static volatile int s_dns_done;
static volatile int s_dns_ok;
static ip4_addr_t s_dns_addr;

static void eth_dns_found(const char *name, const ip_addr_t *ipaddr,
			  void *arg)
{
	(void)name;
	(void)arg;
	if (ipaddr != NULL && IP_IS_V4(ipaddr)) {
		ip4_addr_copy(s_dns_addr, *ip_2_ip4(ipaddr));
		s_dns_ok = 1;
	} else {
		s_dns_ok = 0;
	}
	s_dns_done = 1;
}

static int eth_resolve(const char *name, char *ip_out, unsigned int cap)
{
	ip_addr_t addr;
	err_t err;

	s_dns_done = 0;
	s_dns_ok = 0;
	ip_addr_set_zero(&addr);
	LOCK_TCPIP_CORE();
	err = dns_gethostbyname(name, &addr, eth_dns_found, NULL);
	UNLOCK_TCPIP_CORE();
	if (err == ERR_OK) {
		ip4addr_ntoa_r(ip_2_ip4(&addr), ip_out, (int)cap);
		return 0;
	}
	if (err != ERR_INPROGRESS) {
		return (int)err;
	}
	{
		unsigned int waited = 0U;

		while (!s_dns_done && waited < ETH_DNS_WAIT_MS) {
			(void)osDelay(10U);
			waited += 10U;
		}
	}
	if (!s_dns_done) {
		return -1;
	}
	if (!s_dns_ok) {
		return -2;
	}
	ip4addr_ntoa_r(&s_dns_addr, ip_out, (int)cap);
	return 0;
}
#endif /* LWIP_DNS */

/* --- the ladder ----------------------------------------------------------- */

static const char *eth_ip4_str(const ip4_addr_t *addr)
{
	return ip4addr_ntoa(addr);
}

static int eth_bring_up(void)
{
	struct f429_eth_link link;
	struct f429_eth_diag diag;
	unsigned int speed = 0U;
	unsigned int duplex = 0U;
	int rc;

	CASE_ANCHOR("f429_eth: start\n");

	rc = f429_eth_init();
	CASE_ANCHOR("f429_eth: init rc=%d\n", rc);

	memset(&diag, 0, sizeof(diag));
	f429_eth_diag(&diag);
	CASE_ANCHOR("f429_eth: phy id=%04x:%04x addr=%u\n",
		    (unsigned)diag.phy_id1, (unsigned)diag.phy_id2,
		    (unsigned)F429_ETH_PHY_ADDR);
	{
		uint8_t mac[6];

		f429_eth_mac(mac);
		CASE_ANCHOR("f429_eth: mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
			    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	}

	memset(&link, 0, sizeof(link));
	(void)f429_eth_link(&link);
	speed = link.speed_mbit;
	duplex = link.full_duplex;
	CASE_ANCHOR("f429_eth: link speed=%u duplex=%u\n", speed, duplex);

	if (rc != 0) {
		CASE_ANCHOR("f429_eth: regs dmabmr=%08lx dmasr=%08lx maccr=%08lx "
			    "macffr=%08lx macmiiar=%08lx\n",
			    (unsigned long)diag.dmabmr,
			    (unsigned long)diag.dmasr,
			    (unsigned long)diag.maccr,
			    (unsigned long)diag.macffr,
			    (unsigned long)diag.macmiiar);
		eth_reason("init failed");
		return -1;
	}
	if (diag.phy_id1 != 0x0007U || (diag.phy_id2 & 0xFFF0U) != 0xC0F0U) {
		eth_reason("PHY is not the board's LAN8720A (MDIO or strap)");
		return -1;
	}
	if (speed == 0U) {
		eth_reason("no link (cable, router port, or auto-negotiation)");
		return -1;
	}

	if (case_net_stack_up() != 0) {
		eth_reason("lwIP stack bring-up failed");
		return -1;
	}

	{
		ip4_addr_t any;

		ip4_addr_set_zero(&any);
		LOCK_TCPIP_CORE();
		stm32eth_netif_add(&s_netif, &any, &any, &any);
		netif_set_default(&s_netif);
		netif_set_link_up(&s_netif);
		netif_set_up(&s_netif);
		UNLOCK_TCPIP_CORE();
	}

	s_rx_thread = osThreadNew(eth_rx_thread, NULL,
				  &(const osThreadAttr_t){
					  .name = "eth-rx",
					  .priority = osPriorityNormal,
					  .stack_size = 4096U,
				  });
	if (s_rx_thread == NULL) {
		eth_reason("receive thread create failed");
		return -1;
	}
	return 0;
}

static int eth_dhcp(void)
{
	unsigned int waited = 0U;
	char ip[16];
	char mask[16];
	char gw[16];
	err_t err;

	LOCK_TCPIP_CORE();
	err = dhcp_start(&s_netif);
	UNLOCK_TCPIP_CORE();
	if (err != ERR_OK) {
		eth_reason("dhcp start refused");
		return -1;
	}

	for (;;) {
		int done;

		LOCK_TCPIP_CORE();
		done = dhcp_supplied_address(&s_netif) ? 1 : 0;
		if (done) {
			eth_copy_str(ip, sizeof(ip),
				     eth_ip4_str(netif_ip4_addr(&s_netif)));
			eth_copy_str(mask, sizeof(mask),
				     eth_ip4_str(netif_ip4_netmask(&s_netif)));
			eth_copy_str(gw, sizeof(gw),
				     eth_ip4_str(netif_ip4_gw(&s_netif)));
		}
		UNLOCK_TCPIP_CORE();
		if (done) {
			CASE_ANCHOR("f429_eth: dhcp ip=%s mask=%s gw=%s "
				    "lease_ms=%u\n", ip, mask, gw, waited);
			return 0;
		}
		if (waited >= ETH_DHCP_WAIT_MS) {
			break;
		}
		(void)osDelay(100U);
		waited += 100U;
	}
	eth_reason("dhcp timeout");
	return -1;
}

static int eth_lease_strs(char *ip, unsigned int ip_cap, char *gw,
			  unsigned int gw_cap)
{
	int ok = 0;

	LOCK_TCPIP_CORE();
	if (!ip4_addr_isany_val(*netif_ip4_addr(&s_netif))) {
		eth_copy_str(ip, ip_cap,
			     eth_ip4_str(netif_ip4_addr(&s_netif)));
		eth_copy_str(gw, gw_cap, eth_ip4_str(netif_ip4_gw(&s_netif)));
		ok = 1;
	}
	UNLOCK_TCPIP_CORE();
	return ok ? 0 : -1;
}

static void eth_print_arp(const char *gw)
{
	struct eth_addr *eth_ret = NULL;
	const ip4_addr_t *ip_ret = NULL;
	ip4_addr_t gw_addr;

	if (!ip4addr_aton(gw, &gw_addr)) {
		return;
	}
	LOCK_TCPIP_CORE();
	if (etharp_find_addr(&s_netif, &gw_addr, &eth_ret, &ip_ret) >= 0 &&
	    eth_ret != NULL) {
		CASE_ANCHOR("f429_eth: arp gw=%s mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
			    gw, eth_ret->addr[0], eth_ret->addr[1],
			    eth_ret->addr[2], eth_ret->addr[3],
			    eth_ret->addr[4], eth_ret->addr[5]);
	}
	UNLOCK_TCPIP_CORE();
}

int case_f429_eth_run(void)
{
	struct f429_eth_stats stats;
	char ip[16];
	char gw[16];
	unsigned int public_ok = 0U;
	unsigned int i;

	if (eth_bring_up() != 0) {
		return -1;
	}
	if (eth_dhcp() != 0) {
		return -1;
	}
	if (eth_lease_strs(ip, sizeof(ip), gw, sizeof(gw)) != 0) {
		eth_reason("no lease in the netif");
		return -1;
	}

	/* Gateway: ARP + ICMP.  Reported, not gated: a router that does not
	 * answer pings addressed to itself is common and says nothing about
	 * the board's path. */
	{
		unsigned int tx = 0U;
		unsigned int rx = 0U;
		unsigned int rtt_min = 0U;
		unsigned int rtt_avg = 0U;
		unsigned int rtt_max = 0U;

		(void)eth_ping(gw, ETH_PING_COUNT, &tx, &rx, &rtt_min, &rtt_avg,
			       &rtt_max);
		CASE_ANCHOR("f429_eth: ping dst=%s tx=%u rx=%u min=%u avg=%u "
			    "max=%u\n", gw, tx, rx, rtt_min, rtt_avg, rtt_max);
		eth_print_arp(gw);
	}

#if LWIP_DNS
	{
		char resolved[16] = "";
		int rc = eth_resolve(ETH_DNS_NAME, resolved, sizeof(resolved));

		CASE_ANCHOR("f429_eth: dns name=%s rc=%d ip=%s\n", ETH_DNS_NAME,
			    rc, (rc == 0) ? resolved : "-");
	}
#endif

	for (i = 0U;
	     i < (sizeof(eth_public_targets) / sizeof(eth_public_targets[0]));
	     i++) {
		unsigned int tx = 0U;
		unsigned int rx = 0U;
		unsigned int rtt_min = 0U;
		unsigned int rtt_avg = 0U;
		unsigned int rtt_max = 0U;

		(void)eth_ping(eth_public_targets[i], ETH_PING_COUNT, &tx, &rx,
			       &rtt_min, &rtt_avg, &rtt_max);
		CASE_ANCHOR("f429_eth: ping dst=%s tx=%u rx=%u min=%u avg=%u "
			    "max=%u\n", eth_public_targets[i], tx, rx, rtt_min,
			    rtt_avg, rtt_max);
		if (rx >= 9U) {
			public_ok = 1U;
			break;
		}
	}

	f429_eth_stats_snapshot(&stats);
	stm32eth_netif_stats_snapshot(&s_bridge_stats);
	CASE_ANCHOR("f429_eth: stats rx=%lu tx=%lu len_err=%lu copy_err=%lu "
		    "tx_busy=%lu missed=%lu\n",
		    stats.rx_frames, stats.tx_frames, stats.rx_len_err,
		    stats.rx_copy_err, stats.tx_busy, stats.dma_missed);
	CASE_ANCHOR("f429_eth: bridge fed=%lu pbuf_fail=%lu input_fail=%lu "
		    "tx_drop=%lu\n",
		    s_bridge_stats.rx_fed, s_bridge_stats.rx_pbuf_fail,
		    s_bridge_stats.rx_input_fail, s_bridge_stats.tx_drop);

	if (public_ok == 0U) {
		eth_reason("no public echo reply (gateway path is reported above)");
		return -1;
	}

	CASE_ANCHOR("f429_eth: PASS DATA\n");
	return 0;
}
