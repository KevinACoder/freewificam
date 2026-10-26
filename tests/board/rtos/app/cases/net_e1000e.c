/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   net_e1000e.c
 * @brief  Case net-e1000e (firefly v1): the 82574L PCIe NIC behind the
 *         firmware-trained ECAM tree, on lwIP with lwiperf on both legs.
 *
 * The line-up follows the wired-NIC milestone shape: find the function by
 * class, enable bus mastering, reset the NIC, negotiate the PHY at the
 * caller's pace, bring the stack and netif up, keep the lwiperf TCP server
 * listening, then run one client leg against the bench host so the case's
 * own boot carries a throughput number. The RX side is a poll thread that
 * drains the descriptor ring into tcpip_input; the TX side runs on the
 * TCP/IP thread.
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
#include "pcie_ecam.h"
#include "e1000e.h"
#include "e1000e_netif.h"

/* The bench line: static address in the lab's 192.168.4.0/24 network (the
 * same address the previous RTOS wired images on this bench used, so the
 * host-side iperf logs key on one number); the iperf peer is the bench
 * host at .30. */
#define NET_IP3			12
#define NET_GW3			1
#define PEER_IP3		30
#define NET_IPERF_PORT		5201

/* PHY auto-negotiation: the driver's step function is non-blocking by
 * design; 50 ms steps inside a bounded budget. */
#define NET_AUTONEG_TIMEOUT_MS	15000u

/* One drain call walks at most this many descriptors; the loop re-polls
 * immediately while frames are still flowing and sleeps one tick when the
 * ring is empty. */
#define NET_POLL_BUDGET		32u

/* The case's own client leg: run, then abort and read the report. */
#define NET_IPERF_CLIENT_MS	8000u

static struct e1000e g_nic;
static struct netif g_netif;
static int g_up;

static volatile uint32_t g_client_kbps;
static volatile unsigned g_client_done;
/* The session handle as the TCP/IP thread sees it: set by the starter,
 * cleared by the report callback. lwiperf_abort() on a session the stack
 * already tore down (connection refused, RST) walks freed memory - the
 * first board round caught it as lwIP's mem double-free assert. */
static void * volatile g_client_session;

/* The failure-path evidence dump: which leg of ARP/IP/TCP moved and what
 * the driver counters say, so one boot names the broken ring. */
static void net_diag_dump(void)
{
	struct e1000e_stats st;

	e1000e_get_stats(&g_nic, &st);
	CASE_INFO("e1000e: diag rx=%u rxerr=%u poolfail=%u tx=%u txerr=%u "
		  "txwait=%u\n",
		  (unsigned)st.rx_frames, (unsigned)st.rx_errors,
		  (unsigned)st.rx_pool_fail, (unsigned)st.tx_frames,
		  (unsigned)st.tx_errors, (unsigned)st.tx_waits);
#if LWIP_STATS
	{
		size_t i;

		for (i = 0u; i < ARP_TABLE_SIZE; i++) {
			ip4_addr_t *ip = NULL;
			struct netif *nif = NULL;
			struct eth_addr *eth = NULL;

			if (etharp_get_entry(i, &ip, &nif, &eth) &&
			    (ip != NULL) && (eth != NULL)) {
				CASE_INFO("e1000e: arp[%u] %s\n", (unsigned)i,
					  ipaddr_ntoa(ip));
			}
		}
		CASE_INFO("e1000e: lwip etharp tx=%u rx=%u | ip rx=%u chkerr=%u "
			  "drop=%u tx=%u | icmp rx=%u tx=%u | tcp xmit=%u "
			  "recv=%u drop=%u chkerr=%u err=%u | link rx=%u "
			  "tx=%u drop=%u\n",
			  (unsigned)lwip_stats.etharp.xmit,
			  (unsigned)lwip_stats.etharp.recv,
			  (unsigned)lwip_stats.ip.recv,
			  (unsigned)lwip_stats.ip.chkerr,
			  (unsigned)lwip_stats.ip.drop,
			  (unsigned)lwip_stats.ip.xmit,
			  (unsigned)lwip_stats.icmp.recv,
			  (unsigned)lwip_stats.icmp.xmit,
			  (unsigned)lwip_stats.tcp.xmit,
			  (unsigned)lwip_stats.tcp.recv,
			  (unsigned)lwip_stats.tcp.drop,
			  (unsigned)lwip_stats.tcp.chkerr,
			  (unsigned)lwip_stats.tcp.err,
			  (unsigned)lwip_stats.link.recv,
			  (unsigned)lwip_stats.link.xmit,
			  (unsigned)lwip_stats.link.drop);
	}
#endif
}

static void thr_rx(void *arg)
{
	(void)arg;

	for (;;) {
		unsigned fed = e1000e_netif_poll(&g_netif, NET_POLL_BUDGET);

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

/* Bring the wired line up. Idempotent per boot. */
static int nic_line_up(void)
{
	const struct pcie_ecam_func *f;
	ip4_addr_t ip, mask, gw;
	uint32_t cmd;
	uintptr_t bar0 = 0;
	uint32_t waited = 0u;

	if (g_up) {
		return 0;
	}

	(void)pcie_ecam_init();

	/* The board fixture records exactly one network-class function, the
	 * 82574L at 04:00.0; the vendor check keeps a future NIC honest. */
	f = pcie_ecam_find_class(0u, 0x020000u, 0xffff00u);
	if (f == 0 || f->vendor_id != 0x8086u || f->device_id != 0x10d3u) {
		LOG_ERROR("net-e1000e: no 8086:10d3 function on the tree\n");
		return -1;
	}

	/* BAR0 is the CSR block (a memory BAR; the IO flag would reject it). */
	if (!f->bars[0].present || (f->bars[0].flags & 1u) != 0u) {
		LOG_ERROR("net-e1000e: bar0 not a memory BAR\n");
		return -1;
	}
	bar0 = (uintptr_t)f->bars[0].base;

	/* Bus master + memory space: the firmware usually leaves both set for
	 * the endpoints it scanned; the explicit write makes the driver's
	 * needs true rather than inherited. */
	(void)pcie_ecam_cfg_read(f->bus, f->dev, f->fn, 0x04u, 2u, &cmd);
	cmd |= (1u << 1) | (1u << 2);
	(void)pcie_ecam_cfg_write(f->bus, f->dev, f->fn, 0x04u, 2u, cmd);

	CASE_ANCHOR("net-e1000e: nic %02x:%02x.%x 8086:10d3 bar0=0x%08lx\n",
		    f->bus, f->dev, f->fn, (unsigned long)bar0);

	/* The stack first: the driver arms its RX ring from the lwIP pool,
	 * which exists only after tcpip_init (once per boot - the shared
	 * wrapper guards against a second NIC case re-running it). */
	if (case_net_stack_up() != 0) {
		LOG_ERROR("net-e1000e: stack bring-up failed\n");
		return -1;
	}

	if (e1000e_init(&g_nic, bar0, f->bus, f->dev, f->fn) != 0) {
		LOG_ERROR("net-e1000e: driver init failed\n");
		return -1;
	}

	while (e1000e_phy_autoneg_step(&g_nic) == 0) {
		if (waited >= NET_AUTONEG_TIMEOUT_MS) {
			LOG_ERROR("net-e1000e: phy autoneg timeout "
				  "(bmcr=0x%04x bmsr=0x%04x)\n",
				  (unsigned)g_nic.phy_bmcr,
				  (unsigned)g_nic.phy_bmsr);
			return -1;
		}
		waited += 50u;
		(void)osDelay(50u);
	}
	CASE_ANCHOR("net-e1000e: phy autoneg done speed=%u duplex=%s\n",
		    g_nic.link.speed_mbps,
		    g_nic.link.full_duplex ? "full" : "half");

	e1000e_start(&g_nic);
	e1000e_link_poll(&g_nic);

	IP4_ADDR(&ip, 192, 168, 4, NET_IP3);
	IP4_ADDR(&mask, 255, 255, 255, 0);
	IP4_ADDR(&gw, 192, 168, 4, NET_GW3);

	LOCK_TCPIP_CORE();
	e1000e_netif_add(&g_netif, &g_nic, &ip, &mask, &gw);
	if (g_nic.link.up) {
		netif_set_link_up(&g_netif);
	}
	UNLOCK_TCPIP_CORE();
	case_net_lwiperf_server_start();

	CASE_ANCHOR("net-e1000e: netif ip=192.168.4.%u/24 "
		    "mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    NET_IP3,
		    g_nic.mac[0], g_nic.mac[1], g_nic.mac[2],
		    g_nic.mac[3], g_nic.mac[4], g_nic.mac[5]);
	CASE_ANCHOR("net-e1000e: link %s\n", g_nic.link.up ? "up" : "down");

	if (!g_nic.link.up) {
		return -1;
	}

	if (osThreadNew(thr_rx, NULL,
			&(osThreadAttr_t){ .name = "e1krx",
					   .priority = osPriorityNormal,
					   .stack_size = 4096u }) == NULL) {
		LOG_ERROR("net-e1000e: rx thread create failed\n");
		return -1;
	}

	g_up = 1;
	return 0;
}

/* The failure-path receive watch: one heartbeat line every few seconds
 * pairing the driver's receive count (frames the NIC handed over, before
 * any stack decision) with the stack's ARP/IP receive counters. With the
 * bench host pinging through the window, a flat driver count means the
 * peer's frames never reach this port; a growing driver count with a flat
 * IP count means they arrive and die inside. */
static void rx_watch(unsigned seconds)
{
	struct e1000e_stats st;

	for (unsigned s = 0u; s < seconds; s += 5u) {
		e1000e_get_stats(&g_nic, &st);
		CASE_ANCHOR("net-e1000e: watch rx=%u tx=%u "
			    "etharp_rx=%u ip_rx=%u\n",
			    (unsigned)st.rx_frames, (unsigned)st.tx_frames,
			    (unsigned)lwip_stats.etharp.recv,
			    (unsigned)lwip_stats.ip.recv);
		(void)osDelay(5000u);
	}
}

int case_net_e1000e_run(void)
{
	ip_addr_t peer;
	void *client;

	if (nic_line_up() != 0) {
		CASE_ANCHOR("net-e1000e: FAIL line up\n");
		return -1;
	}

	/* The client leg: the whole bench answer (ARP, routing, TCP) in one
	 * number. lwiperf's session setup runs the stack's raw API, so the
	 * core lock is mandatory from this thread (the stack's own reference
	 * case wraps the same calls; the first board round without the lock
	 * corrupted the heap - lwIP's mem double-free assert). The budget
	 * closes the session so the report's DONE verdict lands inside this
	 * case; the abort is guarded - a session the stack already closed
	 * (refused, RST) must not be aborted again. */
	IP4_ADDR(&peer, 192, 168, 4, PEER_IP3);
	LOCK_TCPIP_CORE();
	client = lwiperf_start_tcp_client_default(&peer, iperf_report, NULL);
	UNLOCK_TCPIP_CORE();
	if (client == NULL) {
		CASE_ANCHOR("net-e1000e: FAIL iperf client start\n");
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

	/* The report rides the TCP/IP thread; give it a scheduling round. */
	for (unsigned i = 0u; i < 100u && !g_client_done; i++) {
		(void)osDelay(10u);
	}

	if (!g_client_done) {
		CASE_ANCHOR("net-e1000e: FAIL iperf no report\n");
		net_diag_dump();
		return -1;
	}

	CASE_ANCHOR("net-e1000e: iperf tx %u kbit/s -> 192.168.4.%u\n",
		    (unsigned)g_client_kbps, PEER_IP3);
	if (g_client_kbps == 0u) {
		return -1;
	}
	return 0;
}
