/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtw88_scan.c
 * @brief  Case rtw88_scan: sweep the 2.4 GHz band through the net80211 bridge
 *         on the RTL8821CS and list what the air actually carries.
 *
 * This is the W6-01 round's E4 gate, and it runs after `rtw88` (which owns the
 * SDIO bring-up, the firmware download and the attach); it never repeats that
 * chain.  A missing attach is a named failure, not a hang.
 *
 * The route is the driver's choice, and for this chip it is the software one:
 * the Realtek firmware reports its feature word in its own header, and the
 * 8821C image this board ships carries SIG|LPS_C2H|LCLK with no SCAN_OFFLOAD
 * bit (feature=0x00000007, read from rtw8821c_fw.bin's header), so
 * `rtw_ops_hw_scan` (rtw88/mac80211.c:1180) answers 1 - mac80211's own "this
 * request cannot be taken as a hardware scan" - and the bridge falls back to
 * the channel walk (wlan_bridge.c:1794).  The case therefore pumps the stack's
 * 1 Hz tick and the dwell entry, which is what advances that walk.
 *
 * What a pass claims: the sweep opened, closed, and the table holds at least
 * one BSS whose SSID element parsed.  It deliberately does NOT require a
 * particular AP: the round's second exit is "scan around shows the surrounding
 * APs", and pinning one SSID would make the verdict depend on the bench's
 * router being powered.  The table is printed in full so the operator can
 * compare it against the AP side by eye, and the judge reads the row count.
 *
 * Anchors:
 *   rtw88: scan start state=<n>
 *   rtw88: scan bss <bssid> ssid=<s> freq=<f> rssi=<r>
 *   rtw88: scan done rows=<n> bcn=<n> prsp=<n> fcs=<n> scan=<n>/<n>
 *               state=<n> entered=<n> close=<n> rx=<n>
 *   rtw88: scan PASS
 *
 * `close` is 2 when the walk ran out of channels (the software route); `rx=0`
 * with rows>0 would mean the table came from somewhere other than the air.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "wlan_bridge.h"

#define RTW88_SCAN_TIMEOUT_MS	90000u
#define RTW88_SCAN_POLL_MS	200u
#define RTW88_SCAN_NWID_LEN	32u

struct rtw88_scan_rec {
	unsigned int	rows;
	unsigned int	named;		/* rows whose SSID was non-empty */
};

static void
rtw88_scan_row(const uint8_t bssid[6], const uint8_t *ssid, size_t ssid_len,
    uint32_t freq, int rssi, void *arg)
{
	struct rtw88_scan_rec *rec = arg;
	char name[RTW88_SCAN_NWID_LEN + 1u];
	size_t n = ssid_len;
	size_t i;

	if (ssid == NULL)
		n = 0u;
	if (n > RTW88_SCAN_NWID_LEN)
		n = RTW88_SCAN_NWID_LEN;
	for (i = 0u; i < n; i++)
		name[i] = (char)ssid[i];
	name[n] = '\0';

	rec->rows++;
	if (n != 0u)
		rec->named++;
	CASE_INFO("rtw88: scan bss %02x:%02x:%02x:%02x:%02x:%02x ssid=%s "
	    "freq=%u rssi=%d\n", bssid[0], bssid[1], bssid[2], bssid[3],
	    bssid[4], bssid[5], name, (unsigned)freq, rssi);
}

int
case_rtw88_scan_run(void)
{
	struct wlan_bridge_counters c;
	struct rtw88_scan_rec rec = { 0u, 0u };
	unsigned int waited = 0u;
	unsigned int tick = 0u;
	int rc;

	wlan_bridge_stats(&c);
	if (c.attached == 0u) {
		CASE_ANCHOR("rtw88: scan FAIL not attached\n");
		return 1;
	}

	/* MANUAL: the stack reports the sweep instead of joining the first
	 * candidate the matcher likes (this round does not connect) */
	wlan_bridge_set_manual(1);

	/* one sweep at a time; nothing else can arm one in this image, but the
	 * bridge's own bookkeeping is what the wait is for */
	while (wlan_bridge_scan_pending() && waited < RTW88_SCAN_TIMEOUT_MS) {
		osDelay(RTW88_SCAN_POLL_MS);
		waited += RTW88_SCAN_POLL_MS;
	}

	rc = wlan_bridge_scan_start();
	while (rc == -EBUSY && waited < RTW88_SCAN_TIMEOUT_MS) {
		osDelay(RTW88_SCAN_POLL_MS);
		waited += RTW88_SCAN_POLL_MS;
		rc = wlan_bridge_scan_start();
	}
	if (rc != 0) {
		CASE_ANCHOR("rtw88: scan FAIL start rc=%d state=%d\n", rc,
		    wlan_bridge_state());
		return 1;
	}
	CASE_ANCHOR("rtw88: scan start state=%d\n", wlan_bridge_state());

	/*
	 * The software walk: the dwell entry advances it, and the stack's 1 Hz
	 * tick keeps the node timers alive.  Both are pumped from here because
	 * this image has no other thread that would.
	 */
	while (wlan_bridge_scan_active() && waited < RTW88_SCAN_TIMEOUT_MS) {
		osDelay(RTW88_SCAN_POLL_MS);
		waited += RTW88_SCAN_POLL_MS;
		tick += RTW88_SCAN_POLL_MS;
		(void)wlan_bridge_scan_dwell();
		if (tick >= 1000u) {
			tick = 0u;
			wlan_bridge_watchdog();
		}
	}

	(void)wlan_bridge_scan_walk(rtw88_scan_row, &rec);
	wlan_bridge_stats(&c);
	/*
	 * The sweep's own verdict line.  `entered=0` is a sweep that never
	 * opened; `rx=0` with rows>0 would mean the table is not from the air.
	 */
	CASE_ANCHOR("rtw88: scan done rows=%u bcn=%u prsp=%u fcs=%u "
	    "scan=%u/%u state=%d entered=%u close=%u rx=%u\n", rec.rows,
	    c.rx_beacon, c.rx_probersp, c.rx_fcs_bad, c.scan_starts,
	    c.scan_completes, wlan_bridge_state(), c.scan_entered,
	    c.scan_close, c.rx_frames);

	if (wlan_bridge_scan_active()) {
		CASE_ANCHOR("rtw88: scan FAIL window still open (state=%d)\n",
		    wlan_bridge_state());
		return 1;
	}
	if (rec.rows == 0u) {
		CASE_ANCHOR("rtw88: scan FAIL empty table (state=%d entered=%u "
		    "close=%u rx=%u bcn=%u)\n", wlan_bridge_state(),
		    c.scan_entered, c.scan_close, c.rx_frames, c.rx_beacon);
		return 1;
	}
	if (rec.named == 0u) {
		CASE_ANCHOR("rtw88: scan FAIL no parseable SSID (rows=%u)\n",
		    rec.rows);
		return 1;
	}

	CASE_ANCHOR("rtw88: scan PASS\n");
	return 0;
}
