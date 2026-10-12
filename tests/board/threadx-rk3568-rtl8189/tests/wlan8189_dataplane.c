/**
 * @file    wlan8189_dataplane.c
 * @brief   Case wlan8189-dataplane: the lwIP netif over the RTL8189FTV driver's
 *          net device, DHCP on the association, a ping to the gateway, and the
 *          iperf server put up for the measurement phase.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  pipeline: lwIP stack bring-up (the shared helper, tests/board/common/
 *          lwip_stack.c), then the presentation layer over this driver's net
 *          device (ports/lwip/lkpinetif.h, the stack half; the netdev half is
 *          ports/lwip/lkpinetif.c), with the association feeding it through the
 *          supplicant backend's forwarding seam
 *          (lkpi_wpa_cfg80211_netif_bind). The case is the WLAN line's dataplane
 *          (tests/board/threadx-rk3568-wlan/tests/wlan_dataplane.c:1-194) with
 *          the net80211 bridge's netif replaced by this line's adapter. The
 *          address comes from DHCP on the association; the case waits for the
 *          lease, proves the gateway answers, prints one diagnostic line from
 *          the adapter's counters so a loss localizes, and leaves the
 *          generic-TCP server on 5001 for the round's measurement.
 *
 * This file lives in lwIP's header world, which owns names the driver world also
 * defines (lwIP's htons against the contract header's), so it never includes the
 * driver's headers: the device lookup and the seam calls are reached by name and
 * through the two headers that cross that boundary (lkpinetif.h,
 * backend_cfg80211.h).
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
#include "wlan8189_cases.h"
#include "lkpinetif.h"
#include "lwip_stack.h"
#include "wlan8189_wpa_obs.h"
#include "backend_cfg80211.h"

/* the substitute's device lookup, reached by name: this file lives in lwIP's
 * header world and must not include the driver's headers */
struct wiphy;
struct wiphy *lkpicfg_wiphy(void);

#define WLAN8189_DHCP_WAIT_MS	45000U
#define WLAN8189_PING_WAIT_MS	9000U

static struct netif g_netif;

/* the presentation layer's link steps, handed to the backend's forwarding slot */
static void
netif_on_up(void *netif)
{

	lkpinetif_note_link((struct netif *)netif, 1);
}

static void
netif_on_down(void *netif)
{

	lkpinetif_note_link((struct netif *)netif, 0);
}

static int
dataplane_up(void)
{
	ip4_addr_t ip, mask, gw;
	struct wiphy *wiphy;
	unsigned int waited = 0;

	/* 0.0.0.0 first: the interface starts empty and DHCP fills it; the gateway
	 * arrives with the lease */
	IP4_ADDR(&ip, 0, 0, 0, 0);
	IP4_ADDR(&mask, 0, 0, 0, 0);
	IP4_ADDR(&gw, 0, 0, 0, 0);

	if (lwip_stack_up() != 0) {
		board_case_fail("the TCP/IP stack did not come up");
		return -1;
	}

	/*
	 * Bind the adapter to the driver's primary interface. The concurrent-mode
	 * build registers two interfaces (a primary and a virtual one whose address
	 * is derived), and lkpicfg_wiphy() returns the first registered - the
	 * primary - which is the one the association and the eFuse address belong
	 * to.
	 */
	wiphy = lkpicfg_wiphy();
	if (wiphy == NULL || lkpinetif_bind(wiphy) != 0) {
		board_case_fail("the netif did not bind to the driver's interface");
		return -1;
	}
	if (lkpinetif_add(&g_netif, &ip, &mask, &gw) != ERR_OK) {
		board_case_fail("the netif did not assemble over the driver's interface");
		return -1;
	}
	/* the single-slot composition: the supplicant backend owns the substitute's
	 * consumer slot, so the netif's link state arrives through the backend's
	 * forwarding, and the slot is re-armed to the backend's sink */
	lkpi_wpa_cfg80211_netif_bind(&g_netif, netif_on_up, netif_on_down);
	lkpi_wpa_cfg80211_events_arm();

	/* the association normally precedes this case (the join case waited for
	 * it), so the link-up event has already fired into a netif that did not
	 * exist yet: replay it from the observer's latch, or the DHCP client would
	 * wait for a link state that has come and gone */
	if (wlan8189_wpa_obs_connected()) {
		netif_on_up(&g_netif);
	}

	lkpinetif_start(&g_netif);
	LOG_ANCHOR("wlan8189: dataplane up, dhcp started\n");

	while (waited < WLAN8189_DHCP_WAIT_MS) {
		if (!ip4_addr_isany_val(g_netif.ip_addr)) {
			break;
		}
		(void)osDelay(200U);
		waited += 200U;
		/* the lease-hunt pulse: the adapter's counters split "the client's
		 * discovers never reached the radio" (tx stays put) from "they left
		 * but nothing came back" (tx climbs while rx stays 0) */
		if ((waited % 10000U) == 0U) {
			struct lkpinetif_diag d;

			lkpinetif_get_diag(&d);
			LOG_INFO("wlan8189: dhcp wait t=%us rx=%lu eapol=%lu "
			    "tx=%lu tx_fail=%lu link=%u\n", waited / 1000U,
			    d.rx_frames, d.rx_eapol, d.tx_frames, d.tx_fail,
			    netif_is_link_up(&g_netif));
		}
	}
	if (ip4_addr_isany_val(g_netif.ip_addr)) {
		static char reason[160];

		(void)snprintf(reason, sizeof(reason), "no DHCP lease within %u s "
		    "(link=%u assoc=%u)", (unsigned)(WLAN8189_DHCP_WAIT_MS / 1000),
		    netif_is_link_up(&g_netif), wlan8189_wpa_obs_connected());
		board_case_fail(reason);
		return -1;
	}
	LOG_ANCHOR("wlan8189: dhcp bound ip=%u.%u.%u.%u/%u gw=%u.%u.%u.%u\n",
	    ip4_addr1_16(&g_netif.ip_addr), ip4_addr2_16(&g_netif.ip_addr),
	    ip4_addr3_16(&g_netif.ip_addr), ip4_addr4_16(&g_netif.ip_addr),
	    ip4_addr1_16(&g_netif.netmask) == 255 ? 24u : 0u,
	    ip4_addr1_16(&g_netif.gw), ip4_addr2_16(&g_netif.gw),
	    ip4_addr3_16(&g_netif.gw), ip4_addr4_16(&g_netif.gw));
	return 0;
}

void wlan8189_dataplane_chain( void )
{
	unsigned int waited;

	if( dataplane_up() != 0 ) {
		return;
	}

	/* the gateway reachability: the contrib ping (socket mode, its own
	 * thread); the judge counts the echo replies from its console lines */
	LOG_INFO( "wlan8189: ping gw window=%u s\n",
	    (unsigned)( WLAN8189_PING_WAIT_MS / 1000 ) );
	ping_init( &g_netif.gw );
	for( waited = 0; waited < WLAN8189_PING_WAIT_MS; waited += 100U ) {
		(void)osDelay( 100U );
	}
	ping_stop();
	LOG_ANCHOR( "wlan8189: ping window done\n" );

	/* the adapter's own accounting, printed once so a failure that got this
	 * far localizes: rx_frames/rx_eapol are what the driver handed over,
	 * tx_frames/tx_fail what the stack handed down */
	{
		struct lkpinetif_diag d;

		lkpinetif_get_diag( &d );
		LOG_ANCHOR( "wlan8189: diag rx=%lu eapol=%lu tx=%lu fail=%lu\n",
		    d.rx_frames, d.rx_eapol, d.tx_frames, d.tx_fail );
	}

	/* the measurement phase's server: the generic-TCP lwiperf server every
	 * TCP port 5001 client can drive (the round's runner plays the host side
	 * against it) */
	LOCK_TCPIP_CORE();
	if( lwiperf_start_tcp_server_default( NULL, NULL ) == NULL ) {
		UNLOCK_TCPIP_CORE();
		board_case_fail( "the iperf server did not start" );
		return;
	}
	UNLOCK_TCPIP_CORE();
	LOG_ANCHOR( "wlan8189: iperf server ready port=%u\n",
	    LWIPERF_TCP_PORT_DEFAULT );
}

BOARD_TEST( "wlan8189-dataplane", wlan8189_dataplane_chain );
