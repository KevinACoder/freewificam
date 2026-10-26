/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtl8xxxu_join.c
 * @brief  Case rtl8xxxu_join: associate with the bench AP (open) through the
 *         net80211 bridge and wait for the stack to reach RUN.
 *
 * The join is the bridge's own station MLME: sta_join runs AUTH -> ASSOC, the
 * management frames ride the seam's transmit path (lkpi80211_tx -> rtl8xxxu's
 * .tx), and the driver learns about the association through
 * vif_cfg_changed / link_info_changed.  The case therefore does not build
 * frames; it asks the bridge for the join and pumps the stack's 1 Hz tick
 * (the auth/assoc transaction timers) while it waits.
 *
 * The AP is the one the scan case matched: its BSSID is looked up from the
 * sweep's table by SSID, because the bench AP is restarted between arms and its
 * BSSID is an arm fact while the SSID is the controlled variable.  Pinning a
 * BSSID in the image would make the ladder fail on the first bench restart -
 * the rtw89 image's hardcoded BSSID is the trap this case does not copy.
 *
 * Anchors:
 *   rtl8xxxu: join bssid=... ssid=... freq=... (the scan-table lookup)
 *   rtl8xxxu: join start ssid=... bssid=... state=<n> rc=<n> attempt=<n>
 *   rtl8xxxu: join wait t=<s> state=<n> mgt_tx=<n> txc=<n> txfail=<n> rx=<n>
 *           rx_age=<ms> bcnT=<n> bad_auth=<n> wq=<ptr>
 *   rtl8xxxu: join lkpi tx=<n> scan=<n> wq=<n> rx_in=<n> rx_ref=<n>
 *           rx_drop=<n> rx_frames=<n> rx_q=<n>
 *   rtl8xxxu: join usb final submits=<n> completes=<n> refused=<n>
 *           stage_overflow=<n> slots_exhausted=<n> rx=<n> rx_err=<n> rx_last=<n>
 *   rtl8xxxu: join ok state=4 mgt_tx=<n> txc=<n> rx=<n>
 *   rtl8xxxu: join PASS
 *
 * A timeout is `rtl8xxxu: join FAIL <reason>` with the same discriminators, so
 * "no frame left the bridge" (mgt_tx=0), "the driver refused or never completed
 * the frame" (mgt_tx>0, txc=0), "the AP is not on this channel" (bcnT=0) and
 * "the answer arrived in the wrong state" (bad_auth>0) are told apart on the
 * spot.  `txfail` counts driver completions without an ACK, and the USB ledger
 * separates "the submit was refused" (refused/stage_overflow/slots_exhausted)
 * from "the transfer left and nothing came back".
 */

#include <stdint.h>
#include <string.h>

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
/* the receive path's boundaries: handed up by the driver, refused by
 * linearize, dropped at the queue, delivered to the bridge, still queued */
void lkpi80211_rx_boundary_counters(uint32_t *in, uint32_t *refused,
    uint32_t *drops, uint32_t *frames, uint32_t *qlen);

#ifndef RTL8XXXU_AP_SSID
#define RTL8XXXU_AP_SSID	"labwifi-mtk24"
#endif
#ifndef RTL8XXXU_AP_FREQ
#define RTL8XXXU_AP_FREQ	2432
#endif

#define RTL8XXXU_JOIN_TIMEOUT_MS	30000u
#define RTL8XXXU_JOIN_POLL_MS		200u
#define RTL8XXXU_JOIN_PULSE_MS		2000u
#define RTL8XXXU_JOIN_NWID_LEN		32u

#define RTL8XXXU_JOIN_SSID	RTL8XXXU_AP_SSID
#define RTL8XXXU_JOIN_SSID_LEN	(sizeof(RTL8XXXU_JOIN_SSID) - 1u)
#define RTL8XXXU_JOIN_FREQ	((uint32_t)RTL8XXXU_AP_FREQ)
#define RTL8XXXU_JOIN_RUN	4

struct rtl8xxxu_join_rec {
	unsigned int	rows;
	unsigned int	hit;
	int		freq;
	uint8_t		bssid[6];
	int		rssi;
};

static void
rtl8xxxu_join_row(const uint8_t bssid[6], const uint8_t *ssid, size_t ssid_len,
    uint32_t freq, int rssi, void *arg)
{
	struct rtl8xxxu_join_rec *rec = arg;

	rec->rows++;
	if (ssid == NULL || ssid_len != RTL8XXXU_JOIN_SSID_LEN)
		return;
	if (memcmp(ssid, RTL8XXXU_JOIN_SSID, RTL8XXXU_JOIN_SSID_LEN) != 0)
		return;
	rec->hit++;
	if (rec->hit == 1u) {
		memcpy(rec->bssid, bssid, 6);
		rec->freq = (int)freq;
		rec->rssi = rssi;
	}
}

static const char *
rtl8xxxu_join_state_txt(int state)
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
rtl8xxxu_usb_stats_anchor(const char *where)
{
	struct lkpi_usb_chusb_stats st;

	lkpi_usb_cherryusb_stats(&st);
	CASE_ANCHOR("rtl8xxxu: join usb %s submits=%u completes=%u refused=%u "
	    "stage_overflow=%u slots_exhausted=%u rx=%u rx_err=%u rx_last=%u\n",
	    where, st.submits, st.completed, st.refused, st.stage_overflow,
	    st.slots_exhausted, st.rx_completes, st.rx_errors, st.rx_last_len);
}

int
case_rtl8xxxu_join_run(void)
{
	struct wlan_bridge_counters c;
	struct rtl8xxxu_join_rec rec = { 0 };
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
		CASE_ANCHOR("rtl8xxxu: join FAIL not attached\n");
		return 1;
	}

	/* The AP identity is the sweep's: look the BSSID up by SSID instead of
	 * pinning the bench's current one in the image. */
	(void)wlan_bridge_scan_walk(rtl8xxxu_join_row, &rec);
	if (rec.hit == 0u) {
		CASE_ANCHOR("rtl8xxxu: join FAIL AP not in scan table "
		    "(ssid=%s rows=%u)\n", RTL8XXXU_JOIN_SSID, rec.rows);
		return 1;
	}
	CASE_ANCHOR("rtl8xxxu: join bssid=%02x:%02x:%02x:%02x:%02x:%02x "
	    "ssid=%s freq=%d rssi=%d\n", rec.bssid[0], rec.bssid[1],
	    rec.bssid[2], rec.bssid[3], rec.bssid[4], rec.bssid[5],
	    RTL8XXXU_JOIN_SSID, rec.freq, rec.rssi);

	/*
	 * Two attempts.  The bench AP occasionally does not answer the first
	 * auth after a fresh association (L1-07 measured the same first-join
	 * fallback on this chip, TRM `99` #07).  A second join is the same
	 * exchange from a clean state; the intermediate timeout is an INFO
	 * line, never the case's FAIL anchor, so a passing retry is not later
	 * read as a failure.
	 */
	for (unsigned int attempt = 1; attempt <= 2u; attempt++) {
		waited = 0;
		tick = 0;
		pulse = 0;
		last_rx = 0;
		last_rx_ms = 0;
		rc = wlan_bridge_join((const uint8_t *)RTL8XXXU_JOIN_SSID,
		    RTL8XXXU_JOIN_SSID_LEN, rec.bssid, NULL, 0);
		CASE_ANCHOR("rtl8xxxu: join start ssid=%s "
		    "bssid=%02x:%02x:%02x:%02x:%02x:%02x state=%d rc=%d "
		    "attempt=%u\n", RTL8XXXU_JOIN_SSID, rec.bssid[0],
		    rec.bssid[1], rec.bssid[2], rec.bssid[3], rec.bssid[4],
		    rec.bssid[5], wlan_bridge_state(), rc, attempt);
		if (rc != 0) {
			CASE_INFO("rtl8xxxu: join attempt %u call rc=%d state=%s\n",
			    attempt, rc,
			    rtl8xxxu_join_state_txt(wlan_bridge_state()));
			osDelay(1000);
			continue;
		}

		while (wlan_bridge_state() != RTL8XXXU_JOIN_RUN &&
		       waited < RTL8XXXU_JOIN_TIMEOUT_MS) {
			osDelay(RTL8XXXU_JOIN_POLL_MS);
			waited += RTL8XXXU_JOIN_POLL_MS;
			tick += RTL8XXXU_JOIN_POLL_MS;
			pulse += RTL8XXXU_JOIN_POLL_MS;
			if (tick >= 1000u) {
				tick = 0u;
				wlan_bridge_watchdog();
			}
			if (pulse >= RTL8XXXU_JOIN_PULSE_MS) {
				pulse = 0u;
				wlan_bridge_stats(&c);
				/*
				 * `rx_age` is how long the bridge's receive
				 * count has been standing still: a receive path
				 * that stops delivering dates itself against the
				 * join's own clock without a new instrument.
				 * `bcnT` is the beacons the chip heard from the
				 * target BSSID, which says whether the radio is
				 * on the AP's channel at all.
				 */
				if (c.rx_frames != last_rx) {
					last_rx = c.rx_frames;
					last_rx_ms = waited;
				}
				lkpi_txfail = lkpi80211_tx_failures();
				wq_now = lkpi_wq_current_work_func();
				CASE_INFO("rtl8xxxu: join wait t=%us state=%s "
				    "mgt_tx=%u txc=%u txfail=%u rx=%u rx_age=%ums "
				    "bcnT=%u bad_auth=%u wq=%p\n", waited / 1000u,
				    rtl8xxxu_join_state_txt(wlan_bridge_state()),
				    c.mgt_tx, c.tx_completes, (unsigned)lkpi_txfail,
				    c.rx_frames, waited - last_rx_ms,
				    c.rx_beacon_tgt, wlan_bridge_stat_bad_auth(),
				    wq_now);
			}
		}

		if (wlan_bridge_state() == RTL8XXXU_JOIN_RUN)
			break;
		CASE_INFO("rtl8xxxu: join attempt %u timeout state=%s mgt_tx=%u "
		    "txc=%u rx=%u bcnT=%u\n", attempt,
		    rtl8xxxu_join_state_txt(wlan_bridge_state()), c.mgt_tx,
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
	{
		uint32_t rx_in = 0, rx_ref = 0, rx_drop = 0, rx_frames = 0;
		uint32_t rx_q = 0;

		lkpi80211_rx_boundary_counters(&rx_in, &rx_ref, &rx_drop,
		    &rx_frames, &rx_q);
		CASE_ANCHOR("rtl8xxxu: join lkpi tx=%u scan=%u wq=%u rx_in=%u "
		    "rx_ref=%u rx_drop=%u rx_frames=%u rx_q=%u\n",
		    (unsigned)lkpi_tx, (unsigned)lkpi_scan, (unsigned)lkpi_wq,
		    (unsigned)rx_in, (unsigned)rx_ref, (unsigned)rx_drop,
		    (unsigned)rx_frames, (unsigned)rx_q);
	}
	rtl8xxxu_usb_stats_anchor("final");

	if (wlan_bridge_state() != RTL8XXXU_JOIN_RUN) {
		CASE_ANCHOR("rtl8xxxu: join FAIL timeout state=%s mgt_tx=%u "
		    "txc=%u txfail=%u rx=%u bcnT=%u bad_auth=%u auth_retries=%u "
		    "wq=%p\n", rtl8xxxu_join_state_txt(wlan_bridge_state()),
		    c.mgt_tx, c.tx_completes, (unsigned)lkpi_txfail,
		    c.rx_frames, c.rx_beacon_tgt, wlan_bridge_stat_bad_auth(),
		    c.auth_retries, wq_now);
		return 1;
	}

	CASE_ANCHOR("rtl8xxxu: join ok state=%d mgt_tx=%u txc=%u rx=%u\n",
	    wlan_bridge_state(), c.mgt_tx, c.tx_completes, c.rx_frames);
	if (c.mgt_tx == 0u) {
		/* RUN with no management frame handed to the driver is a stack
		 * state without an exchange; report it rather than calling the
		 * join proven */
		CASE_ANCHOR("rtl8xxxu: join FAIL RUN without a management frame\n");
		return 1;
	}
	if (c.tx_completes == 0u) {
		CASE_ANCHOR("rtl8xxxu: join FAIL no transmit completion (lkpi=%u "
		    "wq=%u mgt_tx=%u)\n", (unsigned)lkpi_tx, (unsigned)lkpi_wq,
		    c.mgt_tx);
		return 1;
	}
	CASE_ANCHOR("rtl8xxxu: join PASS\n");
	return 0;
}