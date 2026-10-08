/**
 * @file    wlan_scan.c
 * @brief   Case wlan-scan: sweep the bands and compare the table with the
 *          ground truth captured on this board's Linux lane.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  case row of the suite's table (tests/board/common/board_suite.h).
 *          The dwell loop is the port's documented shape: this image has no
 *          callouts, so the case sleeps one dwell per channel and reports the
 *          channel finished through wlan_bridge_scan_dwell() - exactly what the
 *          bridge design says the port worker does when it lands.
 *
 * Anchors (the judge checks each on PASS):
 *   wlan: scan start                   - the filter went promiscuous and the
 *                                        stack entered its scan state
 *   wlan: bss <bssid> ssid=... ...     - one line per candidate found
 *   wlan: scan done rows=N             - the sweep ended and cleared the filter
 *   wlan: gt-match ... seen=1          - at least one ground-truth row matched
 *
 * The ground truth is the same card measured on this board's Linux lane
 * (the lab round captured QiQiJia and its neighbors on channel 1). If the
 * sweep finds nothing, or finds a table with none of those rows, the case
 * fails and names what it saw - an empty table is the exact symptom the
 * receive-filter obligation exists to prevent, and this case is where it
 * would show.
 */

#include "board_suite.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#include "wlan_bridge.h"

/* The SSID field's maximum length (802.11-2020 clause 9.4.2.2: the element's
 * length octet, SSID at most 32 octets). The driver-visible face carries no
 * net80211 constant, so the case spells its own, as the host suite does. */
#define WLAN_SCAN_NWID_LEN	32

#define WLAN_SCAN_SSID		"QiQiJia"
#define WLAN_SCAN_SSID_LEN	7
#define WLAN_SCAN_DWELL_MS	120U
#define WLAN_SCAN_MAX_CHANS	32U

/* The ground truth (Linux lane, same card, same board): the target AP and the
 * two neighbors the capture recorded. BSSID bytes, channel, and nothing else -
 * comparison is on the identity the sweep cannot invent. */
static const uint8_t gt_bssids[][6] = {
	{ 0x14, 0xa3, 0x2f, 0x18, 0x07, 0x2c },	/* QiQiJia */
	{ 0x14, 0xa3, 0x2f, 0x18, 0x07, 0x31 },
	{ 0x14, 0xa3, 0x2f, 0x18, 0x07, 0x2d },
};
#define GT_ROWS (sizeof(gt_bssids) / sizeof(gt_bssids[0]))

static struct {
	unsigned rows;
	unsigned gt_matches;
	unsigned gt_index[GT_ROWS];
} scan_rec;

static void scan_row(const uint8_t bssid[6], const uint8_t *ssid,
    size_t ssid_len, uint32_t freq, int rssi, void *arg)
{
	char essid[WLAN_SCAN_NWID_LEN + 1];
	size_t n = ssid_len;
	unsigned i;

	(void)arg;
	if (n > WLAN_SCAN_NWID_LEN)
		n = WLAN_SCAN_NWID_LEN;
	for (i = 0; i < n; i++)
		essid[i] = (char)ssid[i];
	essid[n] = '\0';

	scan_rec.rows++;
	board_log("wlan: bss %02x:%02x:%02x:%02x:%02x:%02x ssid=%s freq=%u "
	    "rssi=%d\n", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4],
	    bssid[5], essid, freq, rssi);

	for (i = 0; i < GT_ROWS; i++) {
		if (bssid[0] != gt_bssids[i][0] || bssid[1] != gt_bssids[i][1] ||
		    bssid[2] != gt_bssids[i][2] || bssid[3] != gt_bssids[i][3] ||
		    bssid[4] != gt_bssids[i][4] || bssid[5] != gt_bssids[i][5])
			continue;
		if (i == 0 && freq != 2412)	/* QiQiJia sits on channel 1 */
			continue;
		if (!scan_rec.gt_index[i]) {
			scan_rec.gt_index[i] = 1;
			scan_rec.gt_matches++;
		}
		board_log("wlan: gt-match idx=%u bssid=%02x:%02x:%02x:%02x:%02x:%02x "
		    "freq=%u seen=1\n", i, bssid[0], bssid[1], bssid[2], bssid[3],
		    bssid[4], bssid[5], freq);
	}
}

void wlan_scan_chain(void)
{
	struct wlan_bridge_counters c;
	unsigned chans = 0;

	wlan_bridge_stats(&c);
	if (!c.attached) {
		board_case_fail("the interface is not attached; the scan needs a "
		    "probed driver (run wlan-probe first)");
		return;
	}

	/* the joiner's step: the desired SSID the sweep's probes carry */
	if (wlan_bridge_set_ssid((const uint8_t *)WLAN_SCAN_SSID,
	    WLAN_SCAN_SSID_LEN) != 0) {
		board_case_fail("the desired SSID was refused");
		return;
	}

	if (wlan_bridge_scan_start() != 0) {
		board_case_fail("the sweep did not start (receive filter refused?)");
		return;
	}
	wlan_bridge_stats(&c);
	board_log("wlan: scan start ssid=%s mgt_tx=%u rx=%u\n", WLAN_SCAN_SSID,
	    c.mgt_tx, c.rx_frames);

	/* one dwell per channel; the stack arms the next channel inside the call
	 * and tells us when the sweep is over */
	while (chans < WLAN_SCAN_MAX_CHANS) {
		osDelay(WLAN_SCAN_DWELL_MS);
		if (wlan_bridge_scan_dwell() == 0)
			break;
		chans++;
	}

	scan_rec.rows = 0;
	scan_rec.gt_matches = 0;
	(void)wlan_bridge_scan_walk(scan_row, NULL);
	wlan_bridge_stats(&c);
	board_log("wlan: scan done rows=%u chans=%u mgt_tx=%u rx=%u "
	    "scan=%u/%u\n", scan_rec.rows, chans, c.mgt_tx, c.rx_frames,
	    c.scan_starts, c.scan_completes);

	if (scan_rec.rows == 0) {
		board_case_fail("the sweep finished with an empty table (the "
		    "receive filter or the radio did not follow the walk)");
		return;
	}
	if (scan_rec.gt_matches == 0) {
		board_case_fail("no ground-truth row matched (the table above is "
		    "the full sweep)");
		return;
	}
	board_log("wlan: gt-check matched=%u/%u rows=%u\n", scan_rec.gt_matches,
	    (unsigned)GT_ROWS, scan_rec.rows);
}

BOARD_TEST("wlan-scan", wlan_scan_chain);
