/**
 * @file    wlan_dataplane.c
 * @brief   The dataplane case: the WLAN netif over the bridge, DHCP on the
 *          association, a ping to the gateway, and the iperf server put up
 *          for the measurement phase.
 * @author  zhugengyu
 * @date    09.10.2026
 * @design pipeline: lwIP stack bring-up (the regression image's own helper
 *          shape, tests/board/threadx-rk3568-regression/tests/lwip_stack.c),
 *          then the presentation layer (ports/lwip/wlannetif.c) over this
 *          image's bridge, with the association feeding it through the
 *          supplicant backend's forwarding seam (wlan_wpa_netif_bind). The
 *          address comes from DHCP on the association (the rubric's first
 *          choice); the case waits for the lease, proves the gateway answers,
 *          and leaves the generic-TCP server on 5001 for the round's
 *          measurement. The machine-checkable anchors: the bound address, at
 *          least four echo replies (counted by the judge from the ping
 *          module's own console lines), and the server's ready line.
 */

#include <stdio.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/dhcp.h"
#include "lwip/apps/lwiperf.h"
#include "lwip/udp.h"

#include "ping.h"

#include "board_suite.h"
#include "wlannetif.h"
#include "lwip_stack.h"
#include "wlan_wpa_obs.h"
/* the lease-hunt pulse reads the bridge's data counters */
#include "wlan_bridge.h"
/* the per-phase snapshot: the USB endpoint's time partition and the stack's
 * per-frame share (see wlan_census.h) */
#include "wlan_census.h"

/* the lwIP address helpers the console command needs */
#include "lwip/ip_addr.h"

/* the console type the command face passes through (csh.h pulls the config
 * macros chry_shell.h needs) */
#include "csh.h"

void	wlan_wpa_netif_bind(void *netif, void (*on_assoc)(void *),
	    void (*on_disassoc)(void *));
void	wlan_wpa_vendor_events_arm(void);

#define WLAN_DHCP_WAIT_MS	45000U
#define WLAN_PING_WAIT_MS	9000U

/*
 * The measurement arm: equal windows in one boot, the shape the SDIO line's
 * sheet uses (tests/board/threadx-rk3568-rtl8189/tests/wlan8189_dataplane.c).
 * The runner powers the board off once the suite reports its result, so a
 * server that is merely up is not measurable - the case has to hold the image
 * alive for the windows the peer drives.  Each phase announces itself on the
 * console before it opens, which is what the peer waits for; a client that
 * arrives late then costs one phase instead of the boot, and the four numbers
 * of one boot are comparable to each other.
 *
 * Downlink: the peer's client drives this image's lwiperf server.
 * Uplink: this image's client drives the peer's server, so the peer must be
 * named first (`wlan uplink <a.b.c.d>`, sent by the runner's extra steps);
 * with none the stage says so and leaves the verdict the downlink half earned.
 */
#define WLAN_IPERF_WINDOW_MS	20000U
#define WLAN_IPERF_PHASES	4U
#define WLAN_UPLINK_PHASES	4U
/* the lwIP client's own 10 s plus the connect and the report's delivery; a
 * ceiling, the report ends the wait */
#define WLAN_UPLINK_REPORT_WAIT_MS	20000U

/* The UDP receive sink: a datagram socket that only counts what arrives.  A
 * UDP blast has no acknowledgement clock - the sender never waits for this
 * end - so the sink's byte count is the receive path's own ceiling, and the
 * difference to the TCP number is what the acknowledgement path costs.  Every
 * throughput round reads both (the peer's phase actions alternate tcp/udp),
 * because a TCP number alone cannot say whether the wall is the link or the
 * feedback.  The counters reset per phase, so a phase's sink is that phase's
 * burst; the port matches the peer's convention. */
#define WLAN_UDP_SINK_PORT	5002U

static struct netif g_netif;

/* the presentation layer's steps, handed to the backend's forwarding slot */
static void
netif_on_assoc(void *netif)
{

	wlannetif_note_assoc((struct netif *)netif);
}

static void
netif_on_disassoc(void *netif)
{

	wlannetif_note_disassoc((struct netif *)netif);
}

static int
dataplane_up(void)
{
	ip4_addr_t ip, mask, gw;
	unsigned int waited = 0;

	/* 0.0.0.0 first: the interface starts empty and DHCP fills it; the
	 * gateway arrives with the lease (the static figures below are only
	 * the request's ignored initial values) */
	IP4_ADDR(&ip, 0, 0, 0, 0);
	IP4_ADDR(&mask, 0, 0, 0, 0);
	IP4_ADDR(&gw, 0, 0, 0, 0);

	if (lwip_stack_up() != 0) {
		board_case_fail("the TCP/IP stack did not come up");
		return -1;
	}

	if (wlannetif_add(&g_netif, &ip, &mask, &gw) != ERR_OK) {
		board_case_fail("the netif did not assemble over the bridge");
		return -1;
	}
	/* the single-slot composition: the supplicant backend owns the port's
	 * event seam, so the netif's link state arrives through the backend's
	 * forwarding, and the seam is re-armed to the backend's sink (this
	 * layer's own add registered its handler) */
	wlan_wpa_netif_bind(&g_netif, netif_on_assoc, netif_on_disassoc);
	wlan_wpa_vendor_events_arm();

	/* the association normally precedes this case (the join case waited
	 * for it), so the link-up event has already fired into a netif that
	 * did not exist yet: replay it from the observer's latch, or the DHCP
	 * client would wait for a link state that has come and gone */
	if (wlan_wpa_obs_connected()) {
		netif_on_assoc(&g_netif);
	}

	wlannetif_start(&g_netif);
	LOG_ANCHOR("wlan: dataplane up, dhcp started\n");

	while (waited < WLAN_DHCP_WAIT_MS) {
		if (!ip4_addr_isany_val(g_netif.ip_addr)) {
			break;
		}
		osDelay(200U);
		waited += 200U;
		/* the lease-hunt pulse: data_tx splits "the client's discovers
		 * never reached the radio" (the counter stays put - the fault
		 * is above the bridge) from "they left but nothing came back"
		 * (the counter climbs - the fault is the encrypted data path
		 * or the air); the protection split and the receive pair then
		 * say which half of that path, and the crypto counters catch
		 * the answer that arrived but did not decrypt */
		if ((waited % 10000U) == 0U) {
			struct wlan_bridge_counters c;

			wlan_bridge_stats(&c);
			LOG_INFO("wlan: dhcp wait t=%us data_tx=%u(enc=%u "
			    "plain=%u noenc=%u) rx=%u(data=%u enc=%u hwdec=%u/%u) "
			    "crypto(rx_wepfail=%u ccmpfmt=%u replay=%u "
			    "mic=%u nodefkey=%u) tx_err=%u link=%u\n",
			    waited / 1000U, c.data_tx, c.data_tx_enc,
			    c.data_tx_plain, c.tx_noencap, c.rx_frames,
			    c.rx_data, c.rx_data_enc, c.rx_data_hwdec_prot,
			    c.rx_data_hwdec,
			    c.rx_wepfail, c.rx_ccmpformat, c.rx_ccmpreplay,
			    c.rx_ccmpmic, c.tx_nodefkey, c.tx_errors,
			    netif_is_link_up(&g_netif));
		}
	}
	if (ip4_addr_isany_val(g_netif.ip_addr)) {
		static char reason[160];

		snprintf(reason, sizeof(reason), "no DHCP lease within %u s "
		    "(link=%u assoc=%u)", (unsigned)(WLAN_DHCP_WAIT_MS / 1000),
		    netif_is_link_up(&g_netif), wlan_wpa_obs_connected());
		board_case_fail(reason);
		return -1;
	}
	LOG_ANCHOR("wlan: dhcp bound ip=%u.%u.%u.%u/%u gw=%u.%u.%u.%u\n",
	    ip4_addr1_16(&g_netif.ip_addr), ip4_addr2_16(&g_netif.ip_addr),
	    ip4_addr3_16(&g_netif.ip_addr), ip4_addr4_16(&g_netif.ip_addr),
	    ip4_addr1_16(&g_netif.netmask) == 255 ? 24u : 0u,
	    ip4_addr1_16(&g_netif.gw), ip4_addr2_16(&g_netif.gw),
	    ip4_addr3_16(&g_netif.gw), ip4_addr4_16(&g_netif.gw));
	return 0;
}

/* ------------------------------------------------------------------ */
/* the uplink sheet                                                    */
/* ------------------------------------------------------------------ */

/*
 * The reverse direction: this image runs the iperf *client* (the runner's host
 * is the server), because the lwIP stack's client is the end that can drive a
 * send test.  The far end's address arrives from the console
 * (`wlan uplink <a.b.c.d>`, sent by the runner's extra steps).
 */
static volatile unsigned int g_uplink_peer;	/* host order; 0 = unset */

void
wlan_uplink_peer_set(unsigned int peer)
{

	g_uplink_peer = peer;
}

int
wlan_uplink_peer_get(unsigned int *out)
{

	if (g_uplink_peer == 0U || out == NULL)
		return -1;
	*out = g_uplink_peer;
	return 0;
}

/* the client test's report (fires in the TCP/IP thread; the stage polls the
 * latch, so the two never share a payload struct) */
static volatile unsigned int g_uplink_report_seen;
static volatile unsigned int g_uplink_report_type;
static volatile unsigned long g_uplink_bytes;
static volatile unsigned long g_uplink_ms;
static volatile unsigned long g_uplink_kbit;

static void
uplink_report(void *arg, enum lwiperf_report_type report_type,
    const ip_addr_t *local_addr, u16_t local_port,
    const ip_addr_t *remote_addr, u16_t remote_port,
    u32_t bytes_transferred, u32_t ms_duration, u32_t bandwidth_kbitpsec)
{

	(void)arg;
	(void)local_addr;
	(void)local_port;
	(void)remote_addr;
	(void)remote_port;
	g_uplink_report_type = (unsigned int)report_type;
	g_uplink_bytes = (unsigned long)bytes_transferred;
	g_uplink_ms = (unsigned long)ms_duration;
	g_uplink_kbit = (unsigned long)bandwidth_kbitpsec;
	g_uplink_report_seen = 1U;
}

/*
 * One uplink window: run the client to its own end and record the far end's
 * own account of what arrived.  The peer is in host order; its octets are
 * split out here, so no byte-order assumption sits between the console command
 * and lwIP's address type.
 */
static int
uplink_phase_run(unsigned int phase, unsigned int peer)
{
	ip_addr_t remote;
	void *session;
	unsigned int waited;

	IP_ADDR4(&remote, (peer >> 24) & 0xffU, (peer >> 16) & 0xffU,
	    (peer >> 8) & 0xffU, peer & 0xffU);

	g_uplink_report_seen = 0U;
	g_uplink_report_type = 0U;
	g_uplink_bytes = 0UL;
	g_uplink_ms = 0UL;
	g_uplink_kbit = 0UL;

	LOG_ANCHOR("wlan: uplink phase %u ready peer=%u.%u.%u.%u\n", phase,
	    (peer >> 24) & 0xffU, (peer >> 16) & 0xffU,
	    (peer >> 8) & 0xffU, peer & 0xffU);

	LOCK_TCPIP_CORE();
	session = lwiperf_start_tcp_client(&remote, LWIPERF_TCP_PORT_DEFAULT,
	    LWIPERF_CLIENT, uplink_report, NULL);
	UNLOCK_TCPIP_CORE();
	if (session == NULL) {
		board_case_fail("the uplink iperf client did not start");
		return -1;
	}

	/* the client's own duration plus the connect and the report's delivery;
	 * the wait is a ceiling, the report ends it */
	for (waited = 0; waited < WLAN_UPLINK_REPORT_WAIT_MS; waited += 100U) {
		if (g_uplink_report_seen)
			break;
		(void)osDelay(100U);
	}
	if (!g_uplink_report_seen) {
		LOCK_TCPIP_CORE();
		lwiperf_abort(session);
		UNLOCK_TCPIP_CORE();
		board_case_fail("the uplink window ended without a report from "
		    "the far end (is the runner's iperf server up?)");
		return -1;
	}
	LOG_ANCHOR("wlan: uplink phase %u result bytes=%lu ms=%lu kbit=%lu "
	    "type=%u\n", phase, g_uplink_bytes, g_uplink_ms, g_uplink_kbit,
	    g_uplink_report_type);
	return 0;
}

/* ------------------------------------------------------------------ */
/* the UDP receive sink                                                */
/* ------------------------------------------------------------------ */

/*
 * The bisection's other half: the peer blasts datagrams at this sink, which
 * only counts them.  With no acknowledgement clock in the way the number is
 * the receive path's own ceiling, so comparing it with the TCP window of the
 * same boot says whether the wall is the link or the feedback path - and if
 * the two agree, the wall is not the feedback path at all.
 */
static struct udp_pcb *g_udp_pcb;
static volatile unsigned int g_udp_pkts;
static volatile unsigned long g_udp_bytes;

static void
udp_sink_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
    const ip_addr_t *addr, u16_t port)
{

	(void)arg;
	(void)pcb;
	(void)addr;
	(void)port;
	if (p != NULL) {
		g_udp_bytes += (unsigned long)p->tot_len;
		g_udp_pkts++;
		pbuf_free(p);
	}
}

void wlan_dataplane_chain(void)
{
	unsigned int waited;
	unsigned int phase;
	unsigned int peer;

	if (dataplane_up() != 0) {
		return;
	}

	/* the gateway reachability: the contrib ping (socket mode, its own
	 * thread); the judge counts the echo replies from its console lines */
	LOG_INFO("wlan: ping gw window=%u s\n",
	    (unsigned)(WLAN_PING_WAIT_MS / 1000));
	ping_init(&g_netif.gw);
	for (waited = 0; waited < WLAN_PING_WAIT_MS; waited += 100U) {
		osDelay(100U);
	}
	ping_stop();
	LOG_ANCHOR("wlan: ping window done\n");

	/* the measurement phase's server: the generic-TCP lwiperf server every
	 * TCP port 5001 client can drive (the round's runner plays the host
	 * side against it) */
	LOCK_TCPIP_CORE();
	if (lwiperf_start_tcp_server_default(NULL, NULL) == NULL) {
		UNLOCK_TCPIP_CORE();
		board_case_fail("the iperf server did not start");
		return;
	}
	UNLOCK_TCPIP_CORE();
	LOG_ANCHOR("wlan: iperf server ready port=%u\n",
	    LWIPERF_TCP_PORT_DEFAULT);

	/* the UDP ceiling probe's sink (the peer blasts datagrams at it; the
	 * receive path drains them with no acknowledgement clock in the way) */
	LOCK_TCPIP_CORE();
	g_udp_pcb = udp_new();
	if (g_udp_pcb == NULL ||
	    udp_bind(g_udp_pcb, IP_ANY_TYPE, WLAN_UDP_SINK_PORT) != ERR_OK) {
		UNLOCK_TCPIP_CORE();
		board_case_fail("the UDP sink did not bind");
		return;
	}
	udp_recv(g_udp_pcb, udp_sink_recv, NULL);
	UNLOCK_TCPIP_CORE();
	LOG_ANCHOR("wlan: udp sink ready port=%u\n", WLAN_UDP_SINK_PORT);

	/* the downlink phases: the peer's client drives each window, and the
	 * ready line above is what it waits for before it starts.  The sink's
	 * counters reset with the phase, so a UDP phase's burst is its own.
	 *
	 * The census resets with the phase too and prints when it closes: one
	 * phase's `svc` (armed: the device's share of the transfer path) and
	 * `gap` (idle with work queued: the host's share) partition that phase,
	 * so a downlink number comes with its own attribution instead of only a
	 * total.  Both lines are per phase, never per frame. */
	for (phase = 1U; phase <= WLAN_IPERF_PHASES; phase++) {
		g_udp_pkts = 0U;
		g_udp_bytes = 0UL;
		wlan_census_reset();
		LOG_ANCHOR("wlan: downlink phase %u ready window=%u s\n",
		    phase, (unsigned)(WLAN_IPERF_WINDOW_MS / 1000));
		for (waited = 0; waited < WLAN_IPERF_WINDOW_MS;
		    waited += 100U) {
			(void)osDelay(100U);
		}
		LOG_ANCHOR("wlan: downlink phase %u done\n", phase);
		LOG_ANCHOR("wlan: udp sink pkts=%u bytes=%lu\n", g_udp_pkts,
		    g_udp_bytes);
		wlan_census_print();
	}

	/* the uplink phases: this image is the client and the peer is the
	 * server.  With no peer named the stage says why and leaves the verdict
	 * the downlink half earned - a regression boot configures none - while a
	 * configured window that moved no bytes is a failure. */
	if (wlan_uplink_peer_get(&peer) != 0) {
		LOG_ANCHOR("wlan: uplink skipped reason=no-peer\n");
	} else {
		for (phase = 1U; phase <= WLAN_UPLINK_PHASES; phase++) {
			wlan_census_reset();
			if (uplink_phase_run(phase, peer) != 0)
				return;
			wlan_census_print();
		}
	}
}

BOARD_TEST("wlan-dataplane", wlan_dataplane_chain);

/* ------------------------------------------------------------------ */
/* the console's measurement session                                   */
/* ------------------------------------------------------------------ */

/* `wlan iperf <ip>` / `wlan iperf server` / `wlan iperf stop`: the same
 * lwiperf engine and the same argv convention as the wired line's command
 * (tests/board/threadx-rk3568-regression/tests/gmac_dataplane.c:385-441).
 * The client session lasts its built-in 10 s and reports on the console;
 * the server lets the host drive the other direction. */
static struct lwiperf_state_tcp *g_client_session;

void wlan_dataplane_iperf_cmd(chry_shell_t *csh, int argc, char **argv)
{

	if (argc < 2) {
		csh_printf(csh, "usage: wlan iperf <ip> | server | stop\n");
		return;
	}
	if (strcmp(argv[1], "stop") == 0) {
		LOCK_TCPIP_CORE();
		if (g_client_session != NULL) {
			lwiperf_abort(g_client_session);
			g_client_session = NULL;
		}
		UNLOCK_TCPIP_CORE();
		csh_printf(csh, "iperf: stop requested\n");
		return;
	}
	if (strcmp(argv[1], "server") == 0) {
		LOCK_TCPIP_CORE();
		g_client_session = NULL;
		if (lwiperf_start_tcp_server_default(NULL, NULL) == NULL) {
			UNLOCK_TCPIP_CORE();
			csh_printf(csh, "iperf: server start FAILED\n");
			return;
		}
		UNLOCK_TCPIP_CORE();
		csh_printf(csh, "iperf: server on port %u\n",
		    LWIPERF_TCP_PORT_DEFAULT);
		return;
	}

	{
		ip_addr_t remote;

		if (!ipaddr_aton(argv[1], &remote)) {
			csh_printf(csh, "iperf: bad address %s\n", argv[1]);
			return;
		}
		LOCK_TCPIP_CORE();
		g_client_session = lwiperf_start_tcp_client(&remote,
		    LWIPERF_TCP_PORT_DEFAULT, LWIPERF_CLIENT, NULL, NULL);
		UNLOCK_TCPIP_CORE();
		if (g_client_session == NULL) {
			csh_printf(csh, "iperf: client start FAILED\n");
			return;
		}
		csh_printf(csh, "iperf: client vs %s started (10 s)\n", argv[1]);
	}
}
