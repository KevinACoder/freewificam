/**
 * @file    iwlwifi_join.c
 * @brief   The join case: wait for the supplicant's WPA2-PSK association and
 *          judge it on the message anchors and the stack's own state.
 * @author  zhugengyu
 * @date    13.10.2026
 * @design  case shape from the sibling cases (iwlwifi_probe.c/iwlwifi_scan.c):
 *          a chain function that fails with a named line, registered as the
 *          BOARD_TEST iwlwifi-join. The case does not type the connect command -
 *          the supplicant's control face is the console (the spec's own form,
 *          `wpa connect <ssid> <psk> [bssid]`), and the runner sends it while
 *          this case waits. The wait is bounded and the failure says which
 *          piece was missing, so "nothing happened" is never silent.
 *
 *          Evidence the judgment stands on: the supplicant's own MSG_INFO
 *          anchors (`wlan0: WPA: Key negotiation completed ... [PTK=CCMP
 *          GTK=CCMP]`, `CTRL-EVENT-CONNECTED`), captured by the observer in
 *          main.c from the supplicant's message callback, plus the stack
 *          reaching RUN (its own state machine answered the auth/assoc). The
 *          AP's BSSID is pinned by the runner's connect command: this bench has
 *          more than one AP on the SSID (same-SSID-multi-AP lesson), and the
 *          judgment's identity is the BSSID the association completed with.
 */

#include <stdint.h>
#include <stdio.h>

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#include "wlan_bridge.h"

#include "board_suite.h"
#include "wlan_wpa_obs.h"
/* the diagnostic half of the join wait: the skbuff pool's own forensics
 * (double frees, stray frees, use-after-free writes) sit next to the radio
 * counters because a recycled RX buffer is indistinguishable from bad air
 * until the pool's accounting says which it was */
#include "skbuff/skbuff_pool.h"

/* how long the case waits for the console's connect to complete: the console
 * may arrive while the sweep is still in flight, the sweep must finish, the
 * 4-way needs a few round trips - 120 s is a generous ceiling, not a budget */
#define WLAN_JOIN_WAIT_MS	120000U
#define WLAN_JOIN_POLL_MS	200U

static const char *join_state_txt(int state)
{

	/* the imported state numbering: 0 INIT, 1 SCAN, 2 AUTH, 3 ASSOC,
	 * 4 RUN (net80211/_ieee80211.h) */
	switch (state) {
	case 0:
		return "INIT";
	case 1:
		return "SCAN";
	case 2:
		return "AUTH";
	case 3:
		return "ASSOC";
	case 4:
		return "RUN";
	default:
		return "?";
	}
}

void iwlwifi_join_chain(void)
{
	struct wlan_bridge_counters c;
	unsigned int waited = 0;

	wlan_bridge_stats(&c);
	if (!c.attached) {
		board_case_fail("the interface is not attached; the join needs a "
		    "probed driver (run iwlwifi-probe first)");
		return;
	}

	LOG_ANCHOR("wlan: join waiting for the supplicant (send 'wpa connect "
	    "<ssid> <psk> [bssid]' on the console)\n");

	while (waited < WLAN_JOIN_WAIT_MS) {
		if (wlan_wpa_obs_connected() &&
		    wlan_wpa_obs_negotiation_completed()) {
			break;
		}
		osDelay(WLAN_JOIN_POLL_MS);
		waited += WLAN_JOIN_POLL_MS;
		/* a periodic pulse so a stalled join says where it sits: the
		 * stack's state, the management frames that left, the
		 * supplicant's latches (10 s period; the anchor table ignores
		 * these lines) */
		if ((waited % 10000U) == 0) {
			struct wlan_bridge_counters c;
			struct lkpi_skb_pool_stats ps;

			wlan_bridge_stats(&c);
			lkpi_skb_pool_stats(&ps);
			/* the discriminator family rides the pulse: target
			 * beacons flowing (bcnT) says the radio is on the AP's
			 * channel and the chip's address filter passes the
			 * AP's frames; fcs says how much of what arrives is
			 * flagged garbage; txc says whether the transmit
			 * handoffs actually completed below the driver
			 * (mgt_tx counts handoffs only) */
			LOG_INFO("wlan: join wait t=%us state=%s mgt_tx=%u "
			    "txc=%u rx=%u bad_auth=%u bcn=%u bcnT=%u prsp=%u "
			    "fcs=%u pool(df/sf/uaf/af)=%u/%u/%u/%u "
			    "assoc=%u connected=%u\n",
			    waited / 1000U, join_state_txt(wlan_bridge_state()),
			    c.mgt_tx, c.tx_completes, c.rx_frames,
			    wlan_bridge_stat_bad_auth(), c.rx_beacon,
			    c.rx_beacon_tgt, c.rx_probersp, c.rx_fcs_bad,
			    ps.double_free, ps.stray_free, ps.uaf_write,
			    ps.alloc_fail, wlan_wpa_obs_assoc_seen(),
			    wlan_wpa_obs_connected());
		}
	}

	if (!wlan_wpa_obs_connected() ||
	    !wlan_wpa_obs_negotiation_completed()) {
		static char reason[160];

		snprintf(reason, sizeof(reason), "no completed WPA2 association "
		    "within %u s (connect=%u negotiation=%u) - was 'wpa "
		    "connect' sent?", (unsigned)(WLAN_JOIN_WAIT_MS / 1000),
		    wlan_wpa_obs_connected(),
		    wlan_wpa_obs_negotiation_completed());
		board_case_fail(reason);
		return;
	}

	/* the stack's own state machine must agree: the association the
	 * supplicant reported is the one net80211 reached RUN on */
	wlan_bridge_stats(&c);
	if (wlan_bridge_state() != 4 /* RUN */) {
		static char reason[96];

		snprintf(reason, sizeof(reason), "the supplicant completed but "
		    "the stack is not in RUN (state=%s)",
		    join_state_txt(wlan_bridge_state()));
		board_case_fail(reason);
		return;
	}

	LOG_ANCHOR("wlan: join-ok bssid=%s state=RUN mgt_tx=%u data_tx=%u rx=%u\n",
	    wlan_wpa_obs_bssid_txt(), c.mgt_tx, c.data_tx, c.rx_frames);
}

BOARD_TEST("iwlwifi-join", iwlwifi_join_chain);
