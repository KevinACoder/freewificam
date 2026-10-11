/**
 * @file    rtw89_join_raw.c
 * @brief   Case rtw89-join-raw: the driver's own open-auth exchange with the
 *          supplicant out of the picture.
 * @author  zhugengyu
 * @date    10.10.2026
 * @design  case shape from the sibling cases (rtw89_scan.c/rtw89_join.c): a
 *          chain function that fails with a named line, registered as the
 *          BOARD_TEST rtw89-join-raw. It exists to split one question the
 *          supplicant-driven join cannot answer on its own: when the auth
 *          response arrives late, is the driver slow, or is the supplicant's
 *          own recovery (its 10 s timeout re-arming a sweep that forces the
 *          stack back to SCAN) what loses it? The case drives the join
 *          directly through wlan_bridge_join() - no console command, no
 *          supplicant - so the only actor left is the driver.
 *
 *          The judgment is on latency, not just completion: the working line
 *          (the rtl8xxxu image) answers the same open auth in 9 ms, so a join
 *          that only reaches ASSOC after seconds is the fault this case names,
 *          and the printed elapsed time is the measurement.
 *
 *          The second attempt holds the chip's receive filter promiscuous for
 *          the whole exchange: an answer that only shows up then was received
 *          by the chip and dropped by its filter, not lost in the air or the
 *          host - which is the discriminator the bridge's own scan filter
 *          cannot provide (it is only up during a sweep).
 */

#include <stdint.h>
#include <stdio.h>

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#include "wlan_bridge.h"

#include "board_suite.h"

/* the joiner's SSID, the same one the sweep's ground truth carries */
#define JOIN_RAW_SSID		"QiQiJia"
#define JOIN_RAW_SSID_LEN	7
/* the target AP (QiQiJia on channel 1), the same row rtw89-scan matches */
static const uint8_t join_raw_target[6] = { 0x14, 0xa3, 0x2f, 0x18, 0x07, 0x2c };

/* the radio pump's dwell period and the ceiling the sweep loop watches.
 * The walk's own pacing is invisible from here: a chip-side register stall
 * can stretch one channel's dwell by seconds (measured 2026-10-10: a c2h
 * timeout held the walk ~2.3 s and pushed the sweep past a 32-dwell
 * ceiling), and a join attempted while the walk is still on the air is
 * aborted by the walk's own channel transitions (state AUTH -> SCAN within
 * 100 ms, measured) - and then the chip delivers no further receives at all
 * (rx frozen for the rest of that boot). 150 dwells = 30 s is the outer
 * bound; past it the case fails named rather than joining mid-walk. */
#define JOIN_RAW_DWELL_MS	200U
#define JOIN_RAW_MAX_CHANS	150U
/* a healthy driver answers in milliseconds (the rtl8xxxu line: 9 ms); this is
 * the ceiling above which the join is judged slow, not merely late */
#define JOIN_RAW_PROMPT_MS	3000U
/* how long one attempt waits for the exchange before calling it stalled */
#define JOIN_RAW_WAIT_MS	15000U
#define JOIN_RAW_POLL_MS	100U

static const char *join_raw_state_txt(int state)
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

static unsigned int join_raw_rows;

static void
join_raw_row(const uint8_t bssid[6], const uint8_t *ssid, size_t ssid_len,
    uint32_t freq, int rssi, void *arg)
{

	(void)bssid;
	(void)ssid;
	(void)ssid_len;
	(void)freq;
	(void)rssi;
	(void)arg;
	join_raw_rows++;
}

/* One sweep to fill the table (the join needs a candidate to lock onto). */
static int
join_raw_fill_table(void)
{
	unsigned int wait = 0;
	unsigned int chans = 0;
	int rc;

	while (wait < 30000U) {
		if (!wlan_bridge_scan_active())
			break;
		osDelay(100);
		wait += 100;
	}
	while ((rc = wlan_bridge_scan_start()) != 0 && wait < 30000U) {
		osDelay(100);
		wait += 100;
	}
	if (rc != 0)
		return rc;
	while (wlan_bridge_scan_active() && chans < JOIN_RAW_MAX_CHANS) {
		osDelay(JOIN_RAW_DWELL_MS);
		chans++;
	}
	if (wlan_bridge_scan_active()) {
		LOG_WARNING("wlan: join-raw sweep did not close within %u dwells; "
		    "the join is refused (a mid-walk join is aborted by the walk "
		    "and stalls the chip's receive path)\n", chans);
		return -2;
	}
	join_raw_rows = 0;
	(void)wlan_bridge_scan_walk(join_raw_row, NULL);
	LOG_INFO("wlan: join-raw %s table rows=%u chans=%u\n",
	    "sweep", join_raw_rows, chans);
	return join_raw_rows == 0 ? -1 : 0;
}

/*
 * One open-auth join attempt. Returns the elapsed milliseconds when the stack
 * reached ASSOC (the auth exchange completed), or -1 when it did not within
 * the wait. Every state change is printed with the counters that name what the
 * radio was doing at that moment.
 */
static int
join_raw_attempt(const char *tag)
{
	unsigned int waited = 0;
	int last = -2;
	int rc;

	rc = wlan_bridge_join((const uint8_t *)JOIN_RAW_SSID, JOIN_RAW_SSID_LEN,
	    join_raw_target, NULL, 0);
	LOG_INFO("wlan: join-raw %s join rc=%d state=%s\n", tag, rc,
	    join_raw_state_txt(wlan_bridge_state()));
	if (rc != 0)
		return -1;

	while (waited < JOIN_RAW_WAIT_MS) {
		int st = wlan_bridge_state();

		if (st != last) {
			struct wlan_bridge_counters c;

			wlan_bridge_stats(&c);
			last = st;
			LOG_INFO("wlan: join-raw %s t=%ums state=%s bad_auth=%u "
			    "mgt_tx=%u txc=%u rx=%u bcnT=%u tuned=%u tunes=%u\n",
			    tag, waited, join_raw_state_txt(st),
			    wlan_bridge_stat_bad_auth(), c.mgt_tx,
			    c.tx_completes, c.rx_frames, c.rx_beacon_tgt,
			    c.tuned_freq, c.tune_calls);
		}
		if (st >= 3)	/* ASSOC: the auth exchange completed */
			return (int)waited;
		osDelay(JOIN_RAW_POLL_MS);
		waited += JOIN_RAW_POLL_MS;
	}
	return -1;
}

void rtw89_join_raw_chain(void)
{
	struct wlan_bridge_counters c;
	int ms;

	wlan_bridge_stats(&c);
	if (!c.attached) {
		board_case_fail("the interface is not attached; the raw join "
		    "needs a probed driver (run rtw89-probe first)");
		return;
	}

	/* The supplicant out of the way: MANUAL roaming (a sweep reports and
	 * stops instead of joining on its own) and the WPA flag on (a
	 * candidate's RSN IE is only recorded while it is set). */
	wlan_bridge_set_manual(1);
	wlan_bridge_set_wpa(1);
	if (wlan_bridge_set_ssid((const uint8_t *)JOIN_RAW_SSID,
	    JOIN_RAW_SSID_LEN) != 0) {
		board_case_fail("the desired SSID was refused");
		return;
	}

	if (join_raw_fill_table() != 0) {
		board_case_fail("the fill sweep did not produce a usable table "
		    "(see the join-raw line above)");
		return;
	}

	/*
	 * Attempt 1 (narrow): the join with the receive filter's promiscuous
	 * bit forced down - the pre-association state a chip with no BSSID yet
	 * would apply its own address/beacon filters in.  Forced explicitly so
	 * the measurement does not depend on what the bridge does by default.
	 */
	(void)wlan_bridge_filter_promisc(0);
	ms = join_raw_attempt("narrow");
	if (ms >= 0 && (unsigned int)ms <= JOIN_RAW_PROMPT_MS) {
		LOG_ANCHOR("wlan: join-raw-ok filter=narrow ms=%d\n", ms);
		wlan_bridge_disconnect(3);
		return;
	}
	LOG_WARNING("wlan: join-raw narrow slow or stalled (ms=%d, ceiling=%u)\n",
	    ms, (unsigned int)JOIN_RAW_PROMPT_MS);

	/*
	 * Attempt 2: the same join with the chip's receive filter held
	 * promiscuous for the whole exchange. If the answer shows up promptly
	 * here but not above, the chip received it and its filter dropped it;
	 * if it is late both ways, the fault is not the filter.
	 */
	wlan_bridge_disconnect(3);
	osDelay(1000);
	if (join_raw_fill_table() != 0) {
		board_case_fail("the second fill sweep did not produce a "
		    "usable table (see the join-raw line above)");
		return;
	}
	(void)wlan_bridge_filter_promisc(1);
	ms = join_raw_attempt("promisc");
	(void)wlan_bridge_filter_promisc(0);
	wlan_bridge_disconnect(3);

	if (ms >= 0 && (unsigned int)ms <= JOIN_RAW_PROMPT_MS) {
		LOG_ANCHOR("wlan: join-raw-ok filter=promisc ms=%d\n", ms);
		return;
	}

	{
		static char reason[160];

		snprintf(reason, sizeof(reason), "the raw open-auth join never "
		    "completed promptly (state=%s) with the filter narrow or "
		    "promiscuous - the fault is below the supplicant",
		    join_raw_state_txt(wlan_bridge_state()));
		board_case_fail(reason);
	}
}

BOARD_TEST("rtw89-join-raw", rtw89_join_raw_chain);
