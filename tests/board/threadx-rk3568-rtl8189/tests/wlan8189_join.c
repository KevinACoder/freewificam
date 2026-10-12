/**
 * @file    wlan8189_join.c
 * @brief   Case wlan8189-join: wait for the supplicant's WPA2-PSK association on
 *          the RTL8189FTV line and judge it on the message anchors.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  case shape from the sibling cases (wlan8189_probe.c/wlan8189_scan.c):
 *          a chain function that fails with a named line, registered as the
 *          BOARD_TEST wlan8189-join. The case is the WLAN line's join
 *          (tests/board/threadx-rk3568-wlan/tests/wlan_join.c:1-150) with the
 *          net80211 bridge's counters replaced by the substitute's own
 *          (ports/wpa_supplicant/backend_cfg80211.h) and the netif adapter's
 *          diagnostics (ports/lwip/lkpinetif_seam.h), because this driver is a
 *          FullMAC stack that carries its own MLME and reaches cfg80211 rather
 *          than the net80211 bridge.
 *
 * The case does not type the connect command - the supplicant's control face is
 * the console (the spec's own form, `wpa connect <ssid> <psk> [bssid]`), and the
 * runner sends it while this case waits. The wait is bounded and the failure
 * says which piece was missing, so "nothing happened" is never silent.
 *
 * Evidence the judgment stands on: the supplicant's own MSG_INFO anchors
 * (`wlan0: WPA: Key negotiation completed ... [PTK=CCMP GTK=CCMP]`,
 * `CTRL-EVENT-CONNECTED`), captured by the observer in main.c from the
 * supplicant's message callback. This line has no net80211 state machine to
 * read, so the association being up is itself the RUN state the line reports.
 */

#include <stdint.h>
#include <stdio.h>

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#include "board_suite.h"
#include "wlan8189_cases.h"
#include "wlan8189_wpa_obs.h"
#include "backend_cfg80211.h"
#include "lkpinetif_seam.h"

/* the substitute's device lookup, reached by name: this file shares the WPA
 * world's header set, which is not the linuxkpi header world */
struct wiphy;
struct wiphy *lkpicfg_wiphy(void);

/* how long the case waits for the console's connect to complete: the console
 * may arrive while the sweep is still in flight, the sweep must finish, the
 * 4-way needs a few round trips - 120 s is a generous ceiling, not a budget */
#define WLAN8189_JOIN_WAIT_MS	120000U
#define WLAN8189_JOIN_POLL_MS	200U

void wlan8189_join_chain( void )
{
	unsigned int waited = 0;

	if( lkpicfg_wiphy() == NULL ) {
		board_case_fail( "the interface is not attached; the join needs a "
		    "probed driver (run wlan8189-probe first)" );
		return;
	}

	LOG_ANCHOR( "wlan8189: join waiting for the supplicant (send 'wpa connect "
	    "<ssid> <psk>' on the console)\n" );

	while( waited < WLAN8189_JOIN_WAIT_MS ) {
		if( wlan8189_wpa_obs_connected() &&
		    wlan8189_wpa_obs_negotiation_completed() ) {
			break;
		}
		(void)osDelay( WLAN8189_JOIN_POLL_MS );
		waited += WLAN8189_JOIN_POLL_MS;
		/* a periodic pulse so a stalled join says where it sits: the
		 * supplicant's own EAPOL receive and the netif adapter's
		 * counters (10 s period; the anchor table ignores these lines) */
		if( ( waited % 10000U ) == 0U ) {
			unsigned int eapol = 0U, bad = 0U, bss = 0U;
			struct lkpinetif_diag d;

			lkpi_wpa_cfg80211_stats( &eapol, &bad, &bss );
			lkpinetif_get_diag( &d );
			LOG_INFO( "wlan8189: join wait t=%us eapol=%u bad=%u bss=%u "
			    "rx=%lu tx=%lu tx_fail=%lu connected=%u\n",
			    waited / 1000U, eapol, bad, bss, d.rx_frames,
			    d.tx_frames, d.tx_fail, wlan8189_wpa_obs_connected() );
		}
	}

	if( !wlan8189_wpa_obs_connected() ||
	    !wlan8189_wpa_obs_negotiation_completed() ) {
		static char reason[160];

		(void)snprintf( reason, sizeof( reason ), "no completed WPA2 "
		    "association within %u s (connect=%u negotiation=%u) - was "
		    "'wpa connect' sent?", (unsigned)( WLAN8189_JOIN_WAIT_MS / 1000 ),
		    wlan8189_wpa_obs_connected(),
		    wlan8189_wpa_obs_negotiation_completed() );
		board_case_fail( reason );
		return;
	}

	LOG_ANCHOR( "wlan8189: join-ok bssid=%s state=RUN\n",
	    wlan8189_wpa_obs_bssid_txt() );
}

BOARD_TEST( "wlan8189-join", wlan8189_join_chain );
