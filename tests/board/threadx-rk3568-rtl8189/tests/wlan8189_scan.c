/**
 * @file    wlan8189_scan.c
 * @brief   Case wlan8189-scan: the driver is asked for a scan through the
 *          cfg80211 substitute, and the table it reports back is checked against
 *          the access points this board's Linux lane recorded.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  case row of the suite's table (tests/board/common/board_suite.h). The
 *          case is the consumer the substitute's seam was written for
 *          (components/linuxkpi/linuxkpi_cfg80211.h): it registers the upward
 *          callbacks, asks for a scan through the downward entry point, waits
 *          for the driver's own completion notification, and reads the table the
 *          substitute collected from cfg80211_inform_bss_frame.
 *
 * Why the case is written this way: on the interface, "scan" is a request from
 * the host side and a stream of reports from the driver side, and the two are
 * separate calls. Driving the request and then reading what the driver reported
 * is the only shape that proves the whole path - a case that only called the
 * driver's scan op would prove nothing about the reports coming back.
 *
 * The ground truth is the same card measured on this board's Linux lane (the
 * lab round captured the target AP and its two neighbours on channel 1). The
 * comparison is on the identity a sweep cannot invent - the BSSID - and an
 * empty table fails: an empty table is the exact symptom a receive-filter or
 * radio-walk fault produces, and this case is where it shows.
 *
 * Anchors (the judge checks each on PASS):
 *   wlan8189: scan start ssid=<ssid>
 *   wlan8189: scan done rows=<n> chans=<n>
 *   wlan8189: gt-check matched=<n>/<n>
 *   wlan8189: scan ok
 */

#include <string.h>

#include "board_suite.h"
#include "wlan8189_cases.h"

#include "linuxkpi_cfg80211.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#define WLAN8189_SCAN_SSID	"QiQiJia"

/* the poll step and the bound while the driver walks its channels */
#define WLAN8189_SCAN_STEP_MS	50U
#define WLAN8189_SCAN_MAX_STEPS	600U

/* rows printed in full; the table may hold more, the count line carries the
 * total */
#define WLAN8189_SCAN_PRINT_MAX	24U

/* The ground truth (Linux lane, same card, same board): the target AP and the
 * two neighbours the capture recorded. BSSID bytes and nothing else. */
static const uint8_t wlan8189_gt[][6] = {
	{ 0x14, 0xa3, 0x2f, 0x18, 0x07, 0x2c },	/* QiQiJia */
	{ 0x14, 0xa3, 0x2f, 0x18, 0x07, 0x31 },
	{ 0x14, 0xa3, 0x2f, 0x18, 0x07, 0x2d },
};
#define WLAN8189_GT_ROWS	(sizeof( wlan8189_gt ) / sizeof( wlan8189_gt[0] ))

static volatile unsigned int scan_done_seen;
static volatile unsigned int scan_bss_reports;

static void on_scan_done( struct wiphy *wiphy )
{
	(void)wiphy;
	scan_done_seen = 1U;
}

static void on_bss( struct wiphy *wiphy, const struct lkpicfg_bss *bss )
{
	(void)wiphy;
	(void)bss;
	scan_bss_reports++;
}

static void on_attached( struct wiphy *wiphy )
{
	(void)wiphy;
}

static void on_connected( struct wiphy *wiphy, const uint8_t bssid[6], uint16_t status )
{
	(void)wiphy;
	(void)bssid;
	(void)status;
}

static void on_disconnected( struct wiphy *wiphy, uint16_t reason )
{
	(void)wiphy;
	(void)reason;
}

static const struct lkpicfg_consumer_ops wlan8189_consumer = {
	.attached = on_attached,
	.scan_done = on_scan_done,
	.bss = on_bss,
	.connected = on_connected,
	.disconnected = on_disconnected,
};

static unsigned int gt_match( const uint8_t bssid[6] )
{
	unsigned int i;

	for( i = 0U; i < WLAN8189_GT_ROWS; i++ ) {
		if( memcmp( bssid, wlan8189_gt[i], 6 ) == 0 ) {
			return 1U;
		}
	}

	return 0U;
}

void wlan8189_scan_chain( void )
{
	struct wiphy *wiphy = lkpicfg_wiphy();
	struct lkpicfg_bss bss;
	unsigned int rows;
	unsigned int chans = 0U;
	unsigned int matched = 0U;
	unsigned int printed = 0U;
	unsigned int i;
	unsigned int step;
	unsigned int prev_freq = 0U;

	if( wiphy == NULL ) {
		board_case_fail( "no registered wireless device (the probe case must run first)" );
		return;
	}

	scan_done_seen = 0U;
	scan_bss_reports = 0U;
	if( lkpicfg_consumer_register( &wlan8189_consumer, NULL ) != 0 ) {
		board_case_fail( "the scan consumer could not register" );
		return;
	}

	LOG_ANCHOR( "wlan8189: scan start ssid=%s\n", WLAN8189_SCAN_SSID );

	if( lkpicfg_scan_start( wiphy ) != 0 ) {
		board_case_fail( "the driver refused the scan request" );
		return;
	}

	/*
	 * The driver answers asynchronously: it walks its channels and reports each
	 * candidate as it hears it, then says it is done. The wait is bounded, so a
	 * driver that never answers is reported rather than waited on forever.
	 */
	for( step = 0U; step < WLAN8189_SCAN_MAX_STEPS; step++ ) {
		if( scan_done_seen != 0U ) {
			break;
		}
		(void)osDelay( WLAN8189_SCAN_STEP_MS );
	}

	if( scan_done_seen == 0U ) {
		board_case_fail( "the scan never completed (the driver reported no completion)" );
		return;
	}

	rows = lkpicfg_bss_count( wiphy );

	/* the channel count is the distinct frequencies the table holds: the driver
	 * reports a frequency per candidate, and the sweep is only a sweep if it
	 * visited more than the one the access point sits on */
	for( i = 0U; i < rows; i++ ) {
		if( lkpicfg_bss_at( wiphy, i, &bss ) != 0 ) {
			continue;
		}
		if( bss.freq != prev_freq ) {
			chans++;
			prev_freq = bss.freq;
		}
		if( gt_match( bss.bssid ) != 0U ) {
			matched++;
		}
		if( printed < WLAN8189_SCAN_PRINT_MAX ) {
			LOG_ANCHOR( "wlan8189: bss %02x:%02x:%02x:%02x:%02x:%02x "
				    "freq=%u rssi=%d ssid=%s\n",
				    bss.bssid[0], bss.bssid[1], bss.bssid[2],
				    bss.bssid[3], bss.bssid[4], bss.bssid[5],
				    (unsigned)bss.freq, bss.signal, bss.ssid );
			printed++;
		}
	}

	LOG_ANCHOR( "wlan8189: scan done rows=%u chans=%u\n", rows, chans );
	LOG_ANCHOR( "wlan8189: gt-check matched=%u/%u\n",
		    matched, (unsigned)WLAN8189_GT_ROWS );

	if( rows == 0U ) {
		board_case_fail( "the scan table is empty (the receive filter or the radio did not follow the walk)" );
		return;
	}
	if( matched == 0U ) {
		board_case_fail( "no ground-truth access point was reported" );
		return;
	}

	LOG_ANCHOR( "wlan8189: scan ok\n" );
}

BOARD_TEST( "wlan8189-scan", wlan8189_scan_chain );
