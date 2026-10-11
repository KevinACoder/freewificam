/**
 * @file    wlan_join_raw.c
 * @brief   Case wlan-join-raw: the driver's own open-auth exchange with the
 *          supplicant out of the picture (the rtl8xxxu image's control for the
 *          rtw89 case of the same name).
 * @author  zhugengyu
 * @date    10.10.2026
 * @design  the rtw89 suite's rtw89_join_raw.c verbatim through its own compile
 *          set, with one driver swapped. It is the control that keeps the
 *          instrument honest: this line is the working one (its supplicant join
 *          answers the open auth in 9 ms), so a raw join that is not prompt
 *          here means the case or the bridge is wrong, not the driver.
 *
 *          It drives wlan_bridge_join() directly - no console command, no
 *          supplicant - and judges the driver's own auth/assoc exchange on
 *          latency: a healthy driver answers in milliseconds, so a join that
 *          only reaches ASSOC after seconds is the fault this case names. The
 *          second attempt holds the receive filter promiscuous for the whole
 *          exchange, which tells a chip that received the answer and dropped it
 *          from one that never got it.
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
/* the target AP (QiQiJia on channel 1), the same row wlan-scan matches */
static const uint8_t join_raw_target[6] = { 0x14, 0xa3, 0x2f, 0x18, 0x07, 0x2c };

/* the radio pump's dwell period and the ceiling the sweep loop watches */
#define JOIN_RAW_DWELL_MS	200U
#define JOIN_RAW_MAX_CHANS	32U
/* a healthy driver answers in milliseconds; this is the ceiling above which
 * the join is judged slow, not merely late */
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
	join_raw_rows = 0;
	(void)wlan_bridge_scan_walk(join_raw_row, NULL);
	LOG_INFO("wlan: join-raw sweep table rows=%u chans=%u\n",
	    join_raw_rows, chans);
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

void wlan_join_raw_chain(void)
{
	struct wlan_bridge_counters c;
	int ms;

	wlan_bridge_stats(&c);
	if (!c.attached) {
		board_case_fail("the interface is not attached; the raw join "
		    "needs a probed driver (run wlan-probe first)");
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
		board_case_fail("the sweep recorded no candidate for the raw "
		    "join (the table is empty)");
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
	 * Attempt 2: the same join with the receive filter held promiscuous for
	 * the whole exchange. If the answer shows up promptly here but not
	 * above, the chip received it and its filter dropped it; if it is late
	 * both ways, the fault is not the filter.
	 */
	wlan_bridge_disconnect(3);
	osDelay(1000);
	if (join_raw_fill_table() != 0) {
		board_case_fail("the second sweep recorded no candidate");
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

BOARD_TEST("wlan-join-raw", wlan_join_raw_chain);
