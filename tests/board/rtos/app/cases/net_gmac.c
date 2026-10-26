/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   net_gmac.c
 * @brief  Case net-gmac (firefly v1): the on-SoC GMAC NIC on lwIP with an
 *         lwiperf client leg against the bench host.
 *
 * The line-up: reset and configure the controller (the driver inherits the
 * PHY state the boot loader's tftp session negotiated - no PHY reset, an
 * autoneg restart only if the link reads down), bring the stack and netif
 * up, keep the lwiperf TCP server listening, then run one client leg so
 * this boot carries a throughput number. The RX side is a poll thread that
 * drains the descriptor ring through the netif bridge; the TX side runs on
 * the TCP/IP thread.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/tcpip.h"
#include "lwip/etharp.h"
#include "lwip/stats.h"
#include "lwip/apps/lwiperf.h"

#include "log.h"

#include "board_conf.h"
#include "gmac.h"
#include "gmac_netif.h"

/* The bench line: static address in the lab's wired network, the iperf peer
 * is the bench host at .30. This NIC takes its own bench address (.11) - a
 * boot may run both wired-NIC cases, and two netifs answering for one
 * address would fight over the host's ARP entry. */
#define NET_IP3			11
#define NET_GW3			1
#define PEER_IP3		30

/* PHY bring-up budget: the driver's step function is non-blocking, the
 * caller paces it (50 ms steps). */
#define NET_PHY_TIMEOUT_MS	15000u

/* One drain call walks at most this many frames; the loop re-polls
 * immediately while frames are still flowing and sleeps one tick when the
 * ring is empty. */
#define NET_POLL_BUDGET		32u

#define NET_IPERF_CLIENT_MS	8000u

static struct gmac g_nic;
static struct netif g_netif;
static int g_up;

static volatile uint32_t g_client_kbps;
static volatile unsigned g_client_done;
static void * volatile g_client_session;

static void thr_rx(void *arg)
{
	(void)arg;

	for (;;) {
		unsigned fed = gmac_netif_poll(&g_netif, NET_POLL_BUDGET);

		if (fed == 0u) {
			(void)osDelay(1u);
		}
	}
}

static void iperf_report(void *arg, enum lwiperf_report_type type,
			 const ip_addr_t *local_addr, u16_t local_port,
			 const ip_addr_t *remote_addr, u16_t remote_port,
			 u32_t bytes, u32_t ms, u32_t kbitpsec)
{
	(void)arg;
	(void)local_addr;
	(void)local_port;
	(void)remote_addr;
	(void)remote_port;
	(void)bytes;
	(void)ms;

	g_client_session = NULL;
	if (type == LWIPERF_TCP_DONE_CLIENT) {
		g_client_kbps = kbitpsec;
		g_client_done = 1u;
	}
}

/* The failure-path evidence dump: which leg of ARP/IP/TCP moved and what
 * the driver counters say, so one boot names the broken ring. */
static void net_diag_dump(void)
{
	struct gmac_stats st;

	gmac_get_stats(&g_nic, &st);
	CASE_INFO("gmac: diag rx=%u rxcrc=%u rxdrop=%u tx=%u txbusy=%u\n",
		  (unsigned)st.rx_frames, (unsigned)st.rx_crc_errors,
		  (unsigned)st.rx_dropped, (unsigned)st.tx_frames,
		  (unsigned)st.tx_busy);
#if LWIP_STATS
	CASE_INFO("gmac: lwip etharp xmit=%u recv=%u | ip recv=%u drop=%u "
		  "xmit=%u | icmp recv=%u xmit=%u | tcp xmit=%u recv=%u "
		  "drop=%u | link recv=%u xmit=%u drop=%u\n",
		  (unsigned)lwip_stats.etharp.xmit,
		  (unsigned)lwip_stats.etharp.recv,
		  (unsigned)lwip_stats.ip.recv,
		  (unsigned)lwip_stats.ip.drop,
		  (unsigned)lwip_stats.ip.xmit,
		  (unsigned)lwip_stats.icmp.recv,
		  (unsigned)lwip_stats.icmp.xmit,
		  (unsigned)lwip_stats.tcp.xmit,
		  (unsigned)lwip_stats.tcp.recv,
		  (unsigned)lwip_stats.tcp.drop,
		  (unsigned)lwip_stats.link.recv,
		  (unsigned)lwip_stats.link.xmit,
		  (unsigned)lwip_stats.link.drop);
#endif
}

/* Bring the wired line up. Idempotent per boot. */
static int nic_line_up(void)
{
	ip4_addr_t ip, mask, gw;
	uint32_t waited = 0u;

	if (g_up) {
		return 0;
	}

	if (gmac_init(&g_nic, BOARD_GMAC0_BASE) != 0) {
		LOG_ERROR("net-gmac: controller init failed\n");
		return -1;
	}

	/* The shared one-shot stack bring-up (once per boot even when both
	 * wired-NIC cases run). */
	if (case_net_stack_up() != 0) {
		LOG_ERROR("net-gmac: stack bring-up failed\n");
		return -1;
	}

	while (gmac_phy_step(&g_nic) != 1) {
		if (waited >= NET_PHY_TIMEOUT_MS) {
			LOG_ERROR("net-gmac: phy bring-up timeout "
				  "(phy=%u link=%u)\n",
				  g_nic.phy_addr, g_nic.link.up);
			return -1;
		}
		waited += 50u;
		(void)osDelay(50u);
	}
	CASE_ANCHOR("net-gmac: phy link up speed=%u duplex=%s\n",
		    g_nic.link.speed_mbps,
		    g_nic.link.full_duplex ? "full" : "half");

	gmac_start(&g_nic);

	IP4_ADDR(&ip, 192, 168, 4, NET_IP3);
	IP4_ADDR(&mask, 255, 255, 255, 0);
	IP4_ADDR(&gw, 192, 168, 4, NET_GW3);

	LOCK_TCPIP_CORE();
	gmac_netif_add(&g_netif, &g_nic, &ip, &mask, &gw);
	netif_set_link_up(&g_netif);
	UNLOCK_TCPIP_CORE();
	case_net_lwiperf_server_start();

	CASE_ANCHOR("net-gmac: netif ip=192.168.4.%u/24 "
		    "mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    NET_IP3,
		    g_nic.mac[0], g_nic.mac[1], g_nic.mac[2],
		    g_nic.mac[3], g_nic.mac[4], g_nic.mac[5]);

	if (osThreadNew(thr_rx, NULL,
			&(osThreadAttr_t){ .name = "gmacrx",
					   .priority = osPriorityNormal,
					   .stack_size = 4096u }) == NULL) {
		LOG_ERROR("net-gmac: rx thread create failed\n");
		return -1;
	}

	g_up = 1;
	return 0;
}

/* The failure-path frame watch: receive-all for a bounded window, print
 * every arriving frame's header line (destination, source, ethertype,
 * length). Answers, from the wire itself, the question the counters
 * cannot: whether the peer's IP frames ever reach this port. */
static void frame_watch(unsigned seconds)
{
	static uint8_t buf[BOARD_GMAC_BUF_BYTES];
	unsigned printed = 0u;

	gmac_set_promisc(&g_nic, 1);
	CASE_ANCHOR("net-gmac: frame watch %us (promiscuous)\n", seconds);
	for (unsigned s = 0u; s < seconds; s++) {
		for (unsigned i = 0u; i < 50u; i++) {
			int len = gmac_rx_frame(&g_nic, buf, sizeof(buf));

			if (len <= 0) {
				break;
			}
			if (printed++ < 60u) {
				CASE_ANCHOR("net-gmac: frame dst=%02x:%02x:%02x:"
					    "%02x:%02x:%02x src=%02x:%02x:%02x:"
					    "%02x:%02x:%02x type=%02x%02x "
					    "len=%d\n",
					    buf[0], buf[1], buf[2], buf[3],
					    buf[4], buf[5], buf[6], buf[7],
					    buf[8], buf[9], buf[10], buf[11],
					    buf[12], buf[13], len);
			}
		}
		(void)osDelay(1000u);
	}
	gmac_set_promisc(&g_nic, 0);
	CASE_ANCHOR("net-gmac: frame watch done (%u frames)\n", printed);
}

int case_net_gmac_run(void)
{
	ip_addr_t peer;
	void *client;

	if (nic_line_up() != 0) {
		CASE_ANCHOR("net-gmac: FAIL line up\n");
		return -1;
	}

	/* The client leg: the whole bench answer (ARP, routing, TCP) in one
	 * number. lwiperf's session setup runs the stack's raw API, so the
	 * core lock is mandatory from this thread; the abort is guarded - a
	 * session the stack already closed must not be aborted again. */
	IP4_ADDR(&peer, 192, 168, 4, PEER_IP3);
	LOCK_TCPIP_CORE();
	client = lwiperf_start_tcp_client_default(&peer, iperf_report, NULL);
	UNLOCK_TCPIP_CORE();
	if (client == NULL) {
		CASE_ANCHOR("net-gmac: FAIL iperf client start\n");
		net_diag_dump();
		return -1;
	}
	g_client_session = client;
	(void)osDelay(NET_IPERF_CLIENT_MS);
	/* Take the session under the core lock and clear the slot first: the
	 * report callback runs on the same thread we lock out, so the abort
	 * cannot race a teardown that frees the session underneath it. */
	LOCK_TCPIP_CORE();
	client = g_client_session;
	g_client_session = NULL;
	if (client != NULL) {
		lwiperf_abort(client);
	}
	UNLOCK_TCPIP_CORE();

	for (unsigned i = 0u; i < 100u && !g_client_done; i++) {
		(void)osDelay(10u);
	}

	if (!g_client_done) {
		CASE_ANCHOR("net-gmac: FAIL iperf no report\n");
		net_diag_dump();
		return -1;
	}

	CASE_ANCHOR("net-gmac: iperf tx %u kbit/s -> 192.168.4.%u\n",
		    (unsigned)g_client_kbps, PEER_IP3);
	if (g_client_kbps == 0u) {
		return -1;
	}
	return 0;
}
