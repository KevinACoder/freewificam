/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtw89_join.c
 * @brief  Case rtw89_join: associate with the bench AP (open) through the
 *         net80211 bridge and wait for the stack to reach RUN.
 *
 * The join is the bridge's own station MLME: sta_join runs AUTH -> ASSOC, the
 * management frames ride the seam's transmit path (lkpi80211_tx -> rtw89's
 * usb.c), and the driver learns about the association through
 * vif_cfg_changed / link_info_changed.  The case therefore does not build
 * frames; it asks the bridge for the join and pumps the stack's 1 Hz tick
 * (the auth/assoc transaction timers) while it waits.
 *
 * The AP is pinned by BSSID: the bench has more than one AP on the SSID, and
 * the association's identity is the BSSID the exchange completed with.
 *
 * Anchors:
 *   rtw89: join start ssid=labwifi-brmc bssid=... state=<n> rc=<n>
 *   rtw89: join wait t=<s> state=<n> mgt_tx=<n> txc=<n> txfail=<n> rx=<n>
 *           rx_age=<ms> bcnT=<n> bad_auth=<n> wq=<ptr>
 *   rtw89: join lkpi tx=<n> scan=<n> wq=<n>
 *   rtw89: join usb final submits=<n> completes=<n> refused=<n>
 *           stage_overflow=<n> slots_exhausted=<n>
 *   rtw89: join ok state=4 mgt_tx=<n> txc=<n> rx=<n>
 *   rtw89: join PASS
 *
 * A timeout is `rtw89: join FAIL <reason>` with the same discriminators, so
 * "no frame left the bridge" (mgt_tx=0), "the driver refused or never
 * completed the frame" (mgt_tx>0, txc=0), "the AP is not on this channel"
 * (bcnT=0) and "the answer arrived in the wrong state" (bad_auth>0) are told
 * apart on the spot.  `txfail` counts driver completions without an ACK, and
 * the USB ledger separates "the submit was refused" (refused/stage_overflow/
 * slots_exhausted) from "the transfer left and nothing came back".
 */

#include <stdint.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "wlan_bridge.h"
#include "lkpi_usb_cherryusb.h"

/* The substitute's completion counters: only scalars cross the call, so the
 * signature is repeated rather than pulling the driver's mac80211 header world
 * into a net80211 case. */
void lkpi80211_completion_counters(uint32_t *scan, uint32_t *tx);
uint32_t lkpi80211_tx_failures(void);

#define RTW89_JOIN_TIMEOUT_MS	30000u
#define RTW89_JOIN_POLL_MS	200u
#define RTW89_JOIN_PULSE_MS	2000u

#define RTW89_JOIN_SSID		"labwifi-brmc"
#define RTW89_JOIN_SSID_LEN	12u
#define RTW89_JOIN_RUN		4

/* the bench AP (brcmfmac wlan0, open, ch5) */
static const uint8_t rtw89_join_target[6] = {
	0xd8, 0x3a, 0xdd, 0x26, 0x91, 0x1e
};

static const char *
rtw89_join_state_txt(int state)
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

static void
rtw89_usb_stats_anchor(const char *where)
{
	struct lkpi_usb_chusb_stats st;

	lkpi_usb_cherryusb_stats(&st);
	CASE_ANCHOR("rtw89: join usb %s submits=%u completes=%u refused=%u "
	    "stage_overflow=%u slots_exhausted=%u rx=%u rx_err=%u rx_last=%u\n",
	    where, st.submits, st.completed, st.refused, st.stage_overflow,
	    st.slots_exhausted, st.rx_completes, st.rx_errors, st.rx_last_len);
}

int
case_rtw89_join_run(void)
{
	struct wlan_bridge_counters c;
	unsigned int waited = 0;
	unsigned int tick = 0;
	unsigned int pulse = 0;
	unsigned int last_rx = 0;
	unsigned int last_rx_ms = 0;
	u32 lkpi_scan = 0u;
	u32 lkpi_tx = 0u;
	u32 lkpi_txfail = 0u;
	u32 lkpi_wq = 0u;
	void *wq_now = NULL;
	int rc;

	wlan_bridge_stats(&c);
	if (c.attached == 0u) {
		CASE_ANCHOR("rtw89: join FAIL not attached\n");
		return 1;
	}

	/*
	 * Two attempts.  The bench AP occasionally does not answer the first
	 * auth after a fresh association (measured twice: AUTH left, mgt/txc
	 * counted, no reply, net80211's 5 s management timeout returns the
	 * stack to SCAN).  A second join is the same exchange from a clean
	 * state; the intermediate timeout is an INFO line, never the case's
	 * FAIL anchor, so a passing retry is not later read as a failure.
	 */
	for (unsigned int attempt = 1; attempt <= 2u; attempt++) {
		waited = 0;
		tick = 0;
		pulse = 0;
		last_rx = 0;
		last_rx_ms = 0;
		rc = wlan_bridge_join((const uint8_t *)RTW89_JOIN_SSID,
		    RTW89_JOIN_SSID_LEN, rtw89_join_target, NULL, 0);
		CASE_ANCHOR("rtw89: join start ssid=%s "
		    "bssid=%02x:%02x:%02x:%02x:%02x:%02x state=%d rc=%d "
		    "attempt=%u\n", RTW89_JOIN_SSID, rtw89_join_target[0],
		    rtw89_join_target[1], rtw89_join_target[2],
		    rtw89_join_target[3], rtw89_join_target[4],
		    rtw89_join_target[5], wlan_bridge_state(), rc, attempt);
		if (rc != 0) {
			CASE_INFO("rtw89: join attempt %u call rc=%d state=%s\n",
			    attempt, rc,
			    rtw89_join_state_txt(wlan_bridge_state()));
			osDelay(1000);
			continue;
		}

	while (wlan_bridge_state() != RTW89_JOIN_RUN &&
	       waited < RTW89_JOIN_TIMEOUT_MS) {
		osDelay(RTW89_JOIN_POLL_MS);
		waited += RTW89_JOIN_POLL_MS;
		tick += RTW89_JOIN_POLL_MS;
		pulse += RTW89_JOIN_POLL_MS;
		if (tick >= 1000u) {
			tick = 0u;
			wlan_bridge_watchdog();
		}
		if (pulse >= RTW89_JOIN_PULSE_MS) {
			pulse = 0u;
			wlan_bridge_stats(&c);
			/*
			 * `rx_age` is how long the bridge's receive count has
			 * been standing still: a receive path that stops
			 * delivering dates itself against the join's own clock
			 * without a new instrument.  `bcnT` is the beacons the
			 * chip heard from the target BSSID, which says whether
			 * the radio is on the AP's channel at all.
			 */
			if (c.rx_frames != last_rx) {
				last_rx = c.rx_frames;
				last_rx_ms = waited;
			}
			lkpi_txfail = lkpi80211_tx_failures();
			wq_now = lkpi_wq_current_work_func();
			CASE_INFO("rtw89: join wait t=%us state=%s mgt_tx=%u "
			    "txc=%u txfail=%u rx=%u rx_age=%ums bcnT=%u "
			    "bad_auth=%u wq=%p\n", waited / 1000u,
			    rtw89_join_state_txt(wlan_bridge_state()), c.mgt_tx,
			    c.tx_completes, (unsigned)lkpi_txfail,
			    c.rx_frames, waited - last_rx_ms, c.rx_beacon_tgt,
			    wlan_bridge_stat_bad_auth(), wq_now);
		}
	}

		if (wlan_bridge_state() == RTW89_JOIN_RUN)
			break;
		CASE_INFO("rtw89: join attempt %u timeout state=%s mgt_tx=%u "
		    "txc=%u rx=%u bcnT=%u\n", attempt,
		    rtw89_join_state_txt(wlan_bridge_state()), c.mgt_tx,
		    c.tx_completes, c.rx_frames, c.rx_beacon_tgt);
		osDelay(1000);
	}

	wlan_bridge_stats(&c);
	lkpi80211_completion_counters(&lkpi_scan, &lkpi_tx);
	{
		struct lkpi_wq_stats wq;

		lkpi_wq_stats_get(&wq);
		lkpi_wq = (u32)wq.runs;
	}
	CASE_ANCHOR("rtw89: join lkpi tx=%u scan=%u wq=%u\n", (unsigned)lkpi_tx,
	    (unsigned)lkpi_scan, (unsigned)lkpi_wq);
	rtw89_usb_stats_anchor("final");

	if (wlan_bridge_state() != RTW89_JOIN_RUN) {
		CASE_ANCHOR("rtw89: join FAIL timeout state=%s mgt_tx=%u "
		    "txc=%u txfail=%u rx=%u bcnT=%u bad_auth=%u auth_retries=%u "
		    "wq=%p\n", rtw89_join_state_txt(wlan_bridge_state()),
		    c.mgt_tx, c.tx_completes, (unsigned)lkpi_txfail,
		    c.rx_frames, c.rx_beacon_tgt, wlan_bridge_stat_bad_auth(),
		    c.auth_retries, wq_now);
		return 1;
	}

	CASE_ANCHOR("rtw89: join ok state=%d mgt_tx=%u txc=%u rx=%u\n",
	    wlan_bridge_state(), c.mgt_tx, c.tx_completes, c.rx_frames);
	if (c.mgt_tx == 0u) {
		/* RUN with no management frame handed to the driver is a stack
		 * state without an exchange; report it rather than calling the
		 * join proven */
		CASE_ANCHOR("rtw89: join FAIL RUN without a management frame\n");
		return 1;
	}
	if (c.tx_completes == 0u) {
		CASE_ANCHOR("rtw89: join FAIL no transmit completion (lkpi=%u "
		    "wq=%u mgt_tx=%u)\n", (unsigned)lkpi_tx, (unsigned)lkpi_wq,
		    c.mgt_tx);
		return 1;
	}
	CASE_ANCHOR("rtw89: join PASS\n");
	return 0;
}
