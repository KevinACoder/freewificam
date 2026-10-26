/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtl8xxxu_scan.c
 * @brief  Case rtl8xxxu_scan: sweep the 2.4 GHz band through the net80211
 *         bridge on the RTL8188EUS and match the bench AP.
 *
 * rtl8xxxu has no hardware scan: its ops table carries `.sw_scan_start` /
 * `.sw_scan_complete` and no `.hw_scan`, which is the 8051-class division of
 * labour - scan runs in the host driver, not in the firmware (TRM `01`).
 * The bridge therefore walks the channel list itself: the case pumps the dwell
 * entry (which tunes the next channel through the driver's `.config`) and the
 * stack's 1 Hz tick (the node timers), and the window closes when the walk runs
 * out of channels and the driver reports its completion.
 *
 * The case runs after `rtl8xxxu` (which owns the USB enumeration, the firmware
 * download and the attach); it never repeats the bring-up.  A missing attach is
 * a named failure, not a hang.
 *
 * The target is the bench AP by SSID and frequency, not by BSSID: the bench's
 * AP is restarted between arms and its BSSID is an arm fact, while the SSID and
 * channel are the controlled variables.  The hit line prints the BSSID it
 * matched, which is what the run records.
 *
 * Anchors:
 *   rtl8xxxu: scan start state=<n>
 *   rtl8xxxu: scan bss <bssid> ssid=<s> freq=<f> rssi=<r>
 *   rtl8xxxu: scan done rows=<n> bcn=<n> bcnT=<n> prsp=<n> fcs=<n>
 *               scan=<n>/<n> state=<n> entered=<n> close=<n> rx=<n>
 *   rtl8xxxu: scan hit bssid=<bssid> ssid=<s> freq=<f> rssi=<r>
 *   rtl8xxxu: scan lkpi completions=<n> tx=<n> wq=<n>
 *   rtl8xxxu: scan PASS
 *
 * `close` is 2 when the walk ran out of channels and 4 when the device reported
 * its own completion through `.sw_scan_complete`; both are a closed window, and
 * the final check accepts either - but it also requires a sweep end (the
 * driver's completion counter or the walk's own close), so a watchdog that
 * merely gave up cannot pass as a sweep.
 *
 * A missing attach, a refused start, an empty table or a table without the
 * bench AP is `rtl8xxxu: scan FAIL <reason>`.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "wlan_bridge.h"

/* The substitute's completion counters.  Its own header carries the driver's
 * mac80211 world into a net80211 case - the include mixture the seam exists to
 * keep apart - and only scalars cross this call, so the signature is repeated
 * here (the same reason the dataplane case repeats the port's receive seam). */
void lkpi80211_completion_counters(uint32_t *scan, uint32_t *tx);

#ifndef RTL8XXXU_AP_SSID
#define RTL8XXXU_AP_SSID	"labwifi-mtk24"
#endif
#ifndef RTL8XXXU_AP_FREQ
#define RTL8XXXU_AP_FREQ	2432
#endif

#define RTL8XXXU_SCAN_TIMEOUT_MS	60000u
#define RTL8XXXU_SCAN_POLL_MS		200u
#define RTL8XXXU_SCAN_NWID_LEN		32u

#define RTL8XXXU_SCAN_SSID	RTL8XXXU_AP_SSID
#define RTL8XXXU_SCAN_SSID_LEN	(sizeof(RTL8XXXU_SCAN_SSID) - 1u)
#define RTL8XXXU_SCAN_FREQ	((uint32_t)RTL8XXXU_AP_FREQ)

struct rtl8xxxu_scan_rec {
	unsigned int	rows;
	unsigned int	hit;
	int		hit_freq;
	int		hit_rssi;
	uint8_t		hit_bssid[6];
};

static void
rtl8xxxu_scan_row(const uint8_t bssid[6], const uint8_t *ssid, size_t ssid_len,
    uint32_t freq, int rssi, void *arg)
{
	struct rtl8xxxu_scan_rec *rec = arg;
	char name[RTL8XXXU_SCAN_NWID_LEN + 1];
	size_t n = ssid_len;
	size_t i;
	int hit;

	if (ssid == NULL)
		n = 0;
	if (n > RTL8XXXU_SCAN_NWID_LEN)
		n = RTL8XXXU_SCAN_NWID_LEN;
	for (i = 0; i < n; i++)
		name[i] = (char)ssid[i];
	name[n] = '\0';

	rec->rows++;
	CASE_INFO("rtl8xxxu: scan bss %02x:%02x:%02x:%02x:%02x:%02x ssid=%s "
	    "freq=%u rssi=%d\n", bssid[0], bssid[1], bssid[2], bssid[3],
	    bssid[4], bssid[5], name, (unsigned)freq, rssi);

	hit = (n == RTL8XXXU_SCAN_SSID_LEN) &&
	    (memcmp(ssid, RTL8XXXU_SCAN_SSID, RTL8XXXU_SCAN_SSID_LEN) == 0);
	if (hit) {
		rec->hit++;
		rec->hit_freq = (int)freq;
		rec->hit_rssi = rssi;
		memcpy(rec->hit_bssid, bssid, 6);
		CASE_ANCHOR("rtl8xxxu: scan hit bssid=%02x:%02x:%02x:%02x:%02x:%02x "
		    "ssid=%s freq=%u rssi=%d\n", bssid[0], bssid[1], bssid[2],
		    bssid[3], bssid[4], bssid[5], name, (unsigned)freq, rssi);
	}
}

int
case_rtl8xxxu_scan_run(void)
{
	struct wlan_bridge_counters c;
	struct rtl8xxxu_scan_rec rec = { 0 };
	unsigned int waited = 0;
	unsigned int tick = 0;
	u32 lkpi_completions = 0u;
	u32 lkpi_tx = 0u;
	u32 lkpi_wq = 0u;
	int rc;

	wlan_bridge_stats(&c);
	if (c.attached == 0u) {
		CASE_ANCHOR("rtl8xxxu: scan FAIL not attached\n");
		return 1;
	}

	/* MANUAL: the stack reports the sweep instead of joining the first
	 * candidate the matcher likes (the join case decides) */
	wlan_bridge_set_manual(1);

	/* one sweep at a time; a supplicant sweep can never be armed in this
	 * image, but the bridge's own bookkeeping is what the wait is for */
	while (wlan_bridge_scan_pending() && waited < RTL8XXXU_SCAN_TIMEOUT_MS) {
		osDelay(RTL8XXXU_SCAN_POLL_MS);
		waited += RTL8XXXU_SCAN_POLL_MS;
	}

	rc = wlan_bridge_scan_start();
	while (rc == -EBUSY && waited < RTL8XXXU_SCAN_TIMEOUT_MS) {
		osDelay(RTL8XXXU_SCAN_POLL_MS);
		waited += RTL8XXXU_SCAN_POLL_MS;
		rc = wlan_bridge_scan_start();
	}
	if (rc != 0) {
		CASE_ANCHOR("rtl8xxxu: scan FAIL start rc=%d state=%d\n", rc,
		    wlan_bridge_state());
		return 1;
	}
	CASE_ANCHOR("rtl8xxxu: scan start state=%d\n", wlan_bridge_state());

	/*
	 * The software route: the dwell entry is what tunes the next channel,
	 * and the stack's 1 Hz tick is what ages the scan's node timers.  Both
	 * run in this loop; the window closes when the walk runs out of
	 * channels (the driver's `.sw_scan_complete` then reports it).
	 */
	while (wlan_bridge_scan_active() && waited < RTL8XXXU_SCAN_TIMEOUT_MS) {
		osDelay(RTL8XXXU_SCAN_POLL_MS);
		waited += RTL8XXXU_SCAN_POLL_MS;
		tick += RTL8XXXU_SCAN_POLL_MS;
		(void)wlan_bridge_scan_dwell();
		if (tick >= 1000u) {
			tick = 0u;
			wlan_bridge_watchdog();
		}
	}

	(void)wlan_bridge_scan_walk(rtl8xxxu_scan_row, &rec);
	wlan_bridge_stats(&c);
	/*
	 * The sweep's own verdict line: how many candidates the table held,
	 * how the stack closed the window, and what the receive path saw.
	 * `entered=0` is a sweep that never opened; `close=2` is the walk
	 * running out of channels; `rx=0` with rows>0 would mean the table came
	 * from somewhere other than the air.
	 */
	CASE_ANCHOR("rtl8xxxu: scan done rows=%u bcn=%u bcnT=%u prsp=%u fcs=%u "
	    "scan=%u/%u state=%d entered=%u close=%u rx=%u\n", rec.rows,
	    c.rx_beacon, c.rx_beacon_tgt, c.rx_probersp, c.rx_fcs_bad,
	    c.scan_starts, c.scan_completes, wlan_bridge_state(),
	    c.scan_entered, c.scan_close, c.rx_frames);

	/*
	 * The driver's half of the completion, beside the bridge's.  The sweep's
	 * own line above says the window closed; this one says whether the
	 * driver's completion ran at all (`completions`, the driver's own
	 * ieee80211_scan_completed()) and whether the deferred-work executor
	 * that carries it exists (`wq`, the system queue's runs - `0` beside a
	 * filled table is a completion with no executor).
	 */
	lkpi80211_completion_counters(&lkpi_completions, &lkpi_tx);
	{
		struct lkpi_wq_stats wq;

		lkpi_wq_stats_get(&wq);
		lkpi_wq = (u32)wq.runs;
	}
	CASE_ANCHOR("rtl8xxxu: scan lkpi completions=%u tx=%u wq=%u\n",
	    (unsigned)lkpi_completions, (unsigned)lkpi_tx,
	    (unsigned)lkpi_wq);

	if (rec.rows == 0u) {
		CASE_ANCHOR("rtl8xxxu: scan FAIL empty table (state=%d entered=%u "
		    "close=%u rx=%u)\n", wlan_bridge_state(), c.scan_entered,
		    c.scan_close, c.rx_frames);
		return 1;
	}
	if (rec.hit == 0u) {
		CASE_ANCHOR("rtl8xxxu: scan FAIL target not found (rows=%u)\n",
		    rec.rows);
		return 1;
	}
	if (wlan_bridge_scan_active()) {
		CASE_ANCHOR("rtl8xxxu: scan FAIL window still open (state=%d)\n",
		    wlan_bridge_state());
		return 1;
	}
	if (lkpi_completions == 0u && c.scan_close != 2u) {
		/* neither the driver's completion nor the walk closed the
		 * window: the bridge's state check or its watchdog did, and
		 * neither is a completed sweep */
		CASE_ANCHOR("rtl8xxxu: scan FAIL closed without a sweep end "
		    "(lkpi=0 close=%u bridge=%u/%u wq=%u)\n",
		    (unsigned)c.scan_close, (unsigned)c.scan_starts,
		    (unsigned)c.scan_completes, (unsigned)lkpi_wq);
		return 1;
	}
	CASE_ANCHOR("rtl8xxxu: scan PASS\n");
	return 0;
}