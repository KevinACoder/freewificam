/**
 * @file    iwlwifi_dataplane.c
 * @brief   The dataplane case: the WLAN netif over the bridge, DHCP on the
 *          association, a ping to the gateway, and the iperf server put up
 *          for the measurement phase.
 * @author  zhugengyu
 * @date    13.10.2026
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

#include "ping.h"

#include "board_suite.h"
#include "wlannetif.h"
#include "lwip_stack.h"
#include "wlan_wpa_obs.h"
/* the lease-hunt pulse reads the bridge's data counters */
#include "wlan_bridge.h"

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

static struct netif g_netif;

/* The bound dataplane interface, for the cases that run after this one (a
 * throughput window pings the peer over the same netif).  NULL until DHCP has
 * filled the address: a caller that sees NULL must not send. */
struct netif *iwlwifi_dataplane_netif(void)
{

	return ip4_addr_isany_val(g_netif.ip_addr) ? NULL : &g_netif;
}

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

void iwlwifi_dataplane_chain(void)
{
	unsigned int waited;

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
	LOG_INFO("wlan: iperf server ready port=%u\n", LWIPERF_TCP_PORT_DEFAULT);
}

BOARD_TEST("iwlwifi-dataplane", iwlwifi_dataplane_chain);

/* ------------------------------------------------------------------ */
/* the console's measurement session                                   */
/* ------------------------------------------------------------------ */

/* `wlan iperf <ip>` / `wlan iperf server` / `wlan iperf stop`: the same
 * lwiperf engine and the same argv convention as the wired line's command
 * (tests/board/threadx-rk3568-regression/tests/gmac_dataplane.c:385-441).
 * The client session lasts its built-in 10 s and reports on the console;
 * the server lets the host drive the other direction. */
static struct lwiperf_state_tcp *g_client_session;

void iwlwifi_dataplane_iperf_cmd(chry_shell_t *csh, int argc, char **argv)
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
