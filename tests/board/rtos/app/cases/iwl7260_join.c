/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   iwl7260_join.c
 * @brief  Case iwl7260_join: associate with the bench AP (open) through the
 *         net80211 bridge and wait for the stack to reach RUN.
 *
 * The join is the bridge's own station MLME: sta_join runs AUTH -> ASSOC, the
 * management frames ride the seam's transmit path (lkpi80211_tx -> mvm), and
 * the driver learns about the association through bss_info_changed.  The case
 * therefore does not build frames; it asks the bridge for the join and then
 * only pumps the stack's 1 Hz tick (the auth/assoc transaction timers) while
 * it waits.
 *
 * The AP is pinned by BSSID: the bench has more than one AP on the SSID, and
 * the association's identity is the BSSID the exchange completed with.
 *
 * Anchors:
 *   iwl7260: join start ssid=labwifi-brmc bssid=... state=<n> rc=<n>
 *   iwl7260: join wait t=<s> state=<n> mgt_tx=<n> txc=<n> rx=<n> bcnT=<n> bad_auth=<n>
 *   iwl7260: join ok state=4 mgt_tx=<n> txc=<n> rx=<n>
 *   iwl7260: join lkpi tx=<n> scan=<n> wq=<n>
 *   iwl7260: join PASS
 *
 * A timeout is `iwl7260: join FAIL <reason>` with the same discriminators, so
 * "no frame left the bridge" (mgt_tx=0), "the driver refused the frame"
 * (mgt_tx>0, txc=0), "the AP is not on this channel" (bcnT=0) and "the answer
 * arrived in the wrong state" (bad_auth>0) are told apart on the spot.
 */

#include <stdint.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "wlan_bridge.h"

/* The substitute's completion counters: only scalars cross the call, so the
 * signature is repeated rather than pulling the driver's mac80211 header world
 * into a net80211 case (see the scan case's note). */
void lkpi80211_completion_counters(uint32_t *scan, uint32_t *tx);

#define IWL_JOIN_TIMEOUT_MS	30000u
#define IWL_JOIN_POLL_MS	200u
#define IWL_JOIN_PULSE_MS	2000u

#define IWL_JOIN_SSID		"labwifi-brmc"
#define IWL_JOIN_SSID_LEN	12u
#define IWL_JOIN_RUN		4

/* the bench AP (brcmfmac wlan0, open, ch5) */
static const uint8_t iwl_join_target[6] = {
	0xd8, 0x3a, 0xdd, 0x26, 0x91, 0x1e
};

static const char *
iwl_join_state_txt(int state)
{

	/* the imported numbering: 0 INIT, 1 SCAN, 2 AUTH, 3 ASSOC, 4 RUN */
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

int
case_iwl7260_join_run(void)
{
	struct wlan_bridge_counters c;
	unsigned int waited = 0;
	unsigned int tick = 0;
	unsigned int pulse = 0;
	u32 lkpi_scan = 0u;
	u32 lkpi_tx = 0u;
	u32 lkpi_wq = 0u;
	int rc;

	wlan_bridge_stats(&c);
	if (c.attached == 0u) {
		CASE_ANCHOR("iwl7260: join FAIL not attached\n");
		return 1;
	}

	rc = wlan_bridge_join((const uint8_t *)IWL_JOIN_SSID, IWL_JOIN_SSID_LEN,
	    iwl_join_target, NULL, 0);
	CASE_ANCHOR("iwl7260: join start ssid=%s bssid=%02x:%02x:%02x:%02x:%02x:%02x "
	    "state=%d rc=%d\n", IWL_JOIN_SSID, iwl_join_target[0],
	    iwl_join_target[1], iwl_join_target[2], iwl_join_target[3],
	    iwl_join_target[4], iwl_join_target[5], wlan_bridge_state(), rc);
	if (rc != 0) {
		CASE_ANCHOR("iwl7260: join FAIL call rc=%d state=%s\n", rc,
		    iwl_join_state_txt(wlan_bridge_state()));
		return 1;
	}

	while (wlan_bridge_state() != IWL_JOIN_RUN && waited < IWL_JOIN_TIMEOUT_MS) {
		osDelay(IWL_JOIN_POLL_MS);
		waited += IWL_JOIN_POLL_MS;
		tick += IWL_JOIN_POLL_MS;
		pulse += IWL_JOIN_POLL_MS;
		if (tick >= 1000u) {
			tick = 0u;
			wlan_bridge_watchdog();
		}
		if (pulse >= IWL_JOIN_PULSE_MS) {
			pulse = 0u;
			wlan_bridge_stats(&c);
			CASE_INFO("iwl7260: join wait t=%us state=%s mgt_tx=%u "
			    "txc=%u rx=%u bcnT=%u bad_auth=%u\n",
			    waited / 1000u, iwl_join_state_txt(wlan_bridge_state()),
			    c.mgt_tx, c.tx_completes, c.rx_frames,
			    c.rx_beacon_tgt, wlan_bridge_stat_bad_auth());
		}
	}

	wlan_bridge_stats(&c);
	/*
	 * The driver's half, beside the bridge's.  `tx` counts the frames the
	 * driver reported complete into the substitute and `txc` above the ones
	 * the bridge then accounted for; a join that stays in AUTH with
	 * `mgt_tx>0` is therefore placed on the driver/queue side (`tx=0`, the
	 * frame was never read out or never completed) or on the seam side
	 * (`tx>0 txc=0`, the forward did not happen) on the spot.  `wq` is the
	 * deferred-work executor's run count, which is what allocates the
	 * station's queue in the first place (mvm/sta.c's add_stream_wk).
	 */
	lkpi80211_completion_counters(&lkpi_scan, &lkpi_tx);
	{
		struct lkpi_wq_stats wq;

		lkpi_wq_stats_get(&wq);
		lkpi_wq = (u32)wq.runs;
	}
	CASE_ANCHOR("iwl7260: join lkpi tx=%u scan=%u wq=%u\n",
	    (unsigned)lkpi_tx, (unsigned)lkpi_scan, (unsigned)lkpi_wq);

	if (wlan_bridge_state() != IWL_JOIN_RUN) {
		CASE_ANCHOR("iwl7260: join FAIL timeout state=%s mgt_tx=%u "
		    "txc=%u rx=%u bcnT=%u bad_auth=%u auth_retries=%u\n",
		    iwl_join_state_txt(wlan_bridge_state()), c.mgt_tx,
		    c.tx_completes, c.rx_frames, c.rx_beacon_tgt,
		    wlan_bridge_stat_bad_auth(), c.auth_retries);
		return 1;
	}

	CASE_ANCHOR("iwl7260: join ok state=%d mgt_tx=%u txc=%u rx=%u\n",
	    wlan_bridge_state(), c.mgt_tx, c.tx_completes, c.rx_frames);
	if (c.mgt_tx == 0u) {
		/* RUN with no management frame handed to the driver is a stack
		 * state without an exchange; report it rather than calling the
		 * join proven */
		CASE_ANCHOR("iwl7260: join FAIL RUN without a management frame\n");
		return 1;
	}
	if (c.tx_completes == 0u) {
		CASE_ANCHOR("iwl7260: join FAIL no transmit completion (lkpi=%u "
		    "wq=%u mgt_tx=%u)\n", (unsigned)lkpi_tx, (unsigned)lkpi_wq,
		    c.mgt_tx);
		return 1;
	}
	CASE_ANCHOR("iwl7260: join PASS\n");
	return 0;
}