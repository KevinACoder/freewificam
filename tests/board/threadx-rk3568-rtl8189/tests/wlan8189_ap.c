/**
 * @file    wlan8189_ap.c
 * @brief   Case wlan8189-ap: run the interface as an open access point (the
 *          driver's own AP MLME), hold a window for the peer, and report the
 *          station events.
 * @author  zhugengyu
 * @date    15.10.2026
 * @design  case shape from the sibling cases (wlan8189_dataplane.c): a chain
 *          function registered with BOARD_TEST, a bounded wait with a named
 *          failure, and anchors a judge reads. The beacon is assembled by the
 *          shared wlan8189_ap_beacon.c (a capture-derived data table, also
 *          pinned byte for byte by the host suite); this file is only the
 *          sequence: start the access point, give the interface the static
 *          address the peer will talk to, wait for a station, hold the window,
 *          stop.
 *
 * The station events are this case's own consumer: the driver reports a station
 * joining or leaving the access point it runs (ioctl_cfg80211.c:4327/:4381) and
 * the substitute forwards both to whoever registered - here, lines the judge
 * anchors on, so "someone associated" is visible on the board's own console.
 *
 * A boot with no peer is still a boot worth judging: the access point coming up
 * and its beacon reaching the air is the case's own claim, and a station that
 * never arrives is printed as such rather than passed off as one.
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

#include "board_suite.h"
#include "wlan8189_cases.h"
#include "wlan8189_census.h"
#include "wlan8189_ap_beacon.h"
#include "backend_cfg80211.h"

/* the substitute's device lookup and the AP seam, reached by name: this file
 * lives in lwIP's header world and must not include the driver's headers (the
 * signatures mirror components/linuxkpi/linuxkpi_cfg80211.h; the station events
 * come through the backend's own bind below, which is the slot's owner) */
struct wiphy;
struct wiphy *lkpicfg_wiphy(void);
int lkpicfg_ap_start(struct wiphy *wiphy, const unsigned char *ssid,
    unsigned long ssid_len, const unsigned char *beacon_head,
    unsigned long head_len, const unsigned char *beacon_tail,
    unsigned long tail_len);
int lkpicfg_ap_stop(struct wiphy *wiphy);

/* the network name and channel this case runs the access point on */
#define WLAN8189_AP_SSID		"FWC-AP-8189"
#define WLAN8189_AP_CHANNEL		1u

/* how long the case waits for a station before saying none came: long enough
 * for the peer to associate by hand or under the runner's control */
#define WLAN8189_AP_JOIN_WAIT_MS	60000U

/* the window a peer gets to ping and measure in, once it is associated */
#define WLAN8189_AP_WINDOW_MS		40000U

/* how long the case waits, inside the window, for the runner to name the far
 * end of the reverse direction (the same console parameter the dataplane
 * sheet's uplink phases read); nothing named means the reverse is skipped */
#define WLAN8189_AP_PEER_WAIT_MS	20000U

/* the access point's own address (test-plane static, per the round's spec: the
 * hotspot has no DHCP server, and the peer takes the same subnet by hand) */
#define WLAN8189_AP_ADDR_0	192
#define WLAN8189_AP_ADDR_1	168
#define WLAN8189_AP_ADDR_2	99
#define WLAN8189_AP_ADDR_3	1

static unsigned int g_ap_sta_joined;
static unsigned int g_ap_sta_left;

static void
ap_sta_joined(const uint8_t mac[6])
{

	g_ap_sta_joined++;
	LOG_ANCHOR("wlan8189: ap sta joined mac=%02x:%02x:%02x:%02x:%02x:%02x "
	    "n=%u\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
	    g_ap_sta_joined);
}

static void
ap_sta_left(const uint8_t mac[6])
{

	g_ap_sta_left++;
	LOG_ANCHOR("wlan8189: ap sta left mac=%02x:%02x:%02x:%02x:%02x:%02x "
	    "n=%u\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
	    g_ap_sta_left);
}

/* the beacon buffers: one head, one tail, built once per start */
static unsigned char g_ap_head[WLAN8189_AP_BEACON_HEAD_LEN];
static unsigned char g_ap_tail[256];

int
wlan8189_ap_start(const char *ssid, unsigned int channel)
{
	struct wiphy *wiphy = lkpicfg_wiphy();
	unsigned int head_len;
	unsigned int tail_len;

	if (wiphy == NULL)
		return -1;
	head_len = wlan8189_ap_beacon_head(g_ap_head, sizeof(g_ap_head),
	    WLAN8189_AP_INTERVAL_TU, WLAN8189_AP_CAPABILITY);
	tail_len = wlan8189_ap_beacon_tail(g_ap_tail, sizeof(g_ap_tail),
	    (const unsigned char *)ssid, (unsigned int)strlen(ssid), channel);
	if (head_len == 0u || tail_len == 0u)
		return -1;
	return lkpicfg_ap_start(wiphy, (const unsigned char *)ssid,
	    (unsigned long)strlen(ssid), g_ap_head, (unsigned long)head_len,
	    g_ap_tail, (unsigned long)tail_len);
}

int
wlan8189_ap_stop(void)
{
	struct wiphy *wiphy = lkpicfg_wiphy();

	if (wiphy == NULL)
		return -1;
	return lkpicfg_ap_stop(wiphy);
}

/* the static address on the interface the dataplane case assembled: the DHCP
 * client is stopped first, or its next attempt would overwrite the address the
 * peer is told to use */
static void
ap_set_static_addr(void)
{
	struct netif *n = wlan8189_dataplane_netif();
	ip4_addr_t ip, mask, gw;

	if (n == NULL)
		return;
	IP4_ADDR(&ip, WLAN8189_AP_ADDR_0, WLAN8189_AP_ADDR_1,
	    WLAN8189_AP_ADDR_2, WLAN8189_AP_ADDR_3);
	IP4_ADDR(&mask, 255, 255, 255, 0);
	IP4_ADDR(&gw, WLAN8189_AP_ADDR_0, WLAN8189_AP_ADDR_1,
	    WLAN8189_AP_ADDR_2, WLAN8189_AP_ADDR_3);
	LOCK_TCPIP_CORE();
	dhcp_stop(n);
	netif_set_addr(n, &ip, &mask, &gw);
	netif_set_up(n);
	UNLOCK_TCPIP_CORE();
	LOG_ANCHOR("wlan8189: ap addr ip=%u.%u.%u.%u/24\n",
	    WLAN8189_AP_ADDR_0, WLAN8189_AP_ADDR_1, WLAN8189_AP_ADDR_2,
	    WLAN8189_AP_ADDR_3);
}

void wlan8189_ap_chain( void )
{
	unsigned int waited;
	int rc;

	if( lkpicfg_wiphy() == NULL ) {
		board_case_fail( "the interface is not attached; the access "
		    "point needs a probed driver (run wlan8189-probe first)" );
		return;
	}
	if( wlan8189_dataplane_netif() == NULL ) {
		board_case_fail( "the dataplane case has not assembled the "
		    "interface; the access point has nothing to address" );
		return;
	}

	/* the station events come through the backend (it owns the substitute's
	 * consumer slot and forwards), so this case only binds its own sink */
	lkpi_wpa_cfg80211_ap_bind( ap_sta_joined, ap_sta_left );
	g_ap_sta_joined = 0U;
	g_ap_sta_left = 0U;

	rc = wlan8189_ap_start( WLAN8189_AP_SSID, WLAN8189_AP_CHANNEL );
	LOG_ANCHOR( "wlan8189: ap start ssid=%s ch=%u rc=%d\n",
	    WLAN8189_AP_SSID, WLAN8189_AP_CHANNEL, rc );
	if( rc != 0 ) {
		board_case_fail( "the driver refused to start the access "
		    "point" );
		return;
	}
	ap_set_static_addr();

	LOG_ANCHOR( "wlan8189: ap waiting for a station window=%u s\n",
	    (unsigned)( WLAN8189_AP_JOIN_WAIT_MS / 1000U ) );
	for( waited = 0; waited < WLAN8189_AP_JOIN_WAIT_MS; waited += 200U ) {
		if( g_ap_sta_joined != 0U ) {
			break;
		}
		(void)osDelay( 200U );
	}

	if( g_ap_sta_joined == 0U ) {
		/* no peer this boot: the access point coming up is still this
		 * case's claim, so it is said, not silently passed over */
		LOG_ANCHOR( "wlan8189: ap no station within %u s\n",
		    (unsigned)( WLAN8189_AP_JOIN_WAIT_MS / 1000U ) );
	} else {
		unsigned int peer = 0U;
		unsigned int peer_waited;

		LOG_ANCHOR( "wlan8189: ap window %u s\n",
		    (unsigned)( WLAN8189_AP_WINDOW_MS / 1000U ) );

		/* the reverse direction: the runner names the far end on the console
		 * (the same parameter the dataplane sheet's uplink phases read), and
		 * the board's own iperf client runs one window toward it - the
		 * receive direction of this access point is what the peer measures
		 * with its own client, the transmit direction is this one */
		for( peer_waited = 0; peer_waited < WLAN8189_AP_PEER_WAIT_MS;
		    peer_waited += 200U ) {
			if( wlan8189_uplink_peer_get( &peer ) == 0 ) {
				break;
			}
			(void)osDelay( 200U );
		}
		if( wlan8189_uplink_peer_get( &peer ) == 0 ) {
			LOG_ANCHOR( "wlan8189: ap peer=%u.%u.%u.%u uplink window\n",
			    (peer >> 24) & 0xffU, (peer >> 16) & 0xffU,
			    (peer >> 8) & 0xffU, peer & 0xffU );
			(void)wlan8189_uplink_window( peer, 1U );
		} else {
			LOG_ANCHOR( "wlan8189: ap no peer for the uplink window\n" );
		}

		for( waited = 0; waited < WLAN8189_AP_WINDOW_MS; waited += 200U ) {
			(void)osDelay( 200U );
		}
		LOG_ANCHOR( "wlan8189: ap window done joined=%u left=%u\n",
		    g_ap_sta_joined, g_ap_sta_left );
		wlan8189_census_print();
	}

	rc = wlan8189_ap_stop();
	LOG_ANCHOR( "wlan8189: ap stop rc=%d\n", rc );
	if( rc != 0 ) {
		board_case_fail( "the driver refused to stop the access "
		    "point" );
	}
}

BOARD_TEST( "wlan8189-ap", wlan8189_ap_chain );
