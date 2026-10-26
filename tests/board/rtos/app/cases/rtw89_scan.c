/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtw89_scan.c
 * @brief  Case rtw89_scan: sweep the 2.4 GHz band through the net80211
 *         bridge on the RTL8851BU and match the bench AP.
 *
 * rtw89's ops table carries both .hw_scan and .sw_scan_start, so the bridge
 * prefers the firmware route (the channel set goes down once, the device owns
 * the dwell, and the end arrives through the driver's scan completion) and
 * falls back to the software sweep when the driver refuses the request.  The
 * case therefore pumps both services while the window is open: the stack's
 * 1 Hz tick and the dwell entry, which is what advances the software
 * fallback's channel walk (on the firmware route the dwell entry only checks
 * that the stack is still in its scan state).
 *
 * The case runs after `rtw89` (which owns the USB enumeration, the firmware
 * download and the attach); it never repeats the bring-up.  A missing attach
 * is a named failure, not a hang.
 *
 * Anchors:
 *   rtw89: scan start state=<n>
 *   rtw89: scan bss <bssid> ssid=<s> freq=<f> rssi=<r>
 *   rtw89: scan done rows=<n> bcn=<n> bcnT=<n> prsp=<n> fcs=<n>
 *               scan=<n>/<n> state=<n> entered=<n> close=<n> rx=<n>
 *   rtw89: scan hit bssid=d8:3a:dd:26:91:1e ssid=labwifi-brmc freq=2432 rssi=<r>
 *   rtw89: scan lkpi completions=<n> tx=<n> wq=<n>
 *   rtw89: scan PASS
 *
 * `close` is 4 when the device reported its own completion and 2 when the
 * stack's walk ran out of channels (the software fallback); both are a
 * closed window, and the final check accepts either.
 *
 * A missing attach, a refused start, an empty table or a table without the
 * bench AP is `rtw89: scan FAIL <reason>`.
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

#define RTW89_SCAN_TIMEOUT_MS	60000u
#define RTW89_SCAN_POLL_MS	200u
#define RTW89_SCAN_NWID_LEN	32u

#define RTW89_SCAN_SSID		"labwifi-brmc"
#define RTW89_SCAN_SSID_LEN	12u
#define RTW89_SCAN_FREQ		2432u

/* the bench AP (brcmfmac wlan0, open, ch5) */
static const uint8_t rtw89_scan_target[6] = {
	0xd8, 0x3a, 0xdd, 0x26, 0x91, 0x1e
};

struct rtw89_scan_rec {
	unsigned int	rows;
	unsigned int	hit;
	int		hit_freq;
	int		hit_rssi;
};

static void
rtw89_scan_row(const uint8_t bssid[6], const uint8_t *ssid, size_t ssid_len,
    uint32_t freq, int rssi, void *arg)
{
	struct rtw89_scan_rec *rec = arg;
	char name[RTW89_SCAN_NWID_LEN + 1];
	size_t n = ssid_len;
	size_t i;
	int hit;

	if (ssid == NULL)
		n = 0;
	if (n > RTW89_SCAN_NWID_LEN)
		n = RTW89_SCAN_NWID_LEN;
	for (i = 0; i < n; i++)
		name[i] = (char)ssid[i];
	name[n] = '\0';

	rec->rows++;
	CASE_INFO("rtw89: scan bss %02x:%02x:%02x:%02x:%02x:%02x ssid=%s "
	    "freq=%u rssi=%d\n", bssid[0], bssid[1], bssid[2], bssid[3],
	    bssid[4], bssid[5], name, (unsigned)freq, rssi);

	hit = memcmp(bssid, rtw89_scan_target, sizeof(rtw89_scan_target)) == 0 &&
	    n == RTW89_SCAN_SSID_LEN &&
	    memcmp(ssid, RTW89_SCAN_SSID, RTW89_SCAN_SSID_LEN) == 0;
	if (hit) {
		rec->hit++;
		rec->hit_freq = (int)freq;
		rec->hit_rssi = rssi;
		CASE_ANCHOR("rtw89: scan hit bssid=%02x:%02x:%02x:%02x:%02x:%02x "
		    "ssid=%s freq=%u rssi=%d\n", bssid[0], bssid[1], bssid[2],
		    bssid[3], bssid[4], bssid[5], name, (unsigned)freq, rssi);
	} else if (memcmp(bssid, rtw89_scan_target, sizeof(rtw89_scan_target)) == 0) {
		/* the BSSID is the AP but the SSID is not the bench network:
		 * either another hostapd instance or a malformed SSID element */
		CASE_INFO("rtw89: scan target bssid ssid mismatch (%s)\n", name);
	}
}

int
case_rtw89_scan_run(void)
{
	struct wlan_bridge_counters c;
	struct rtw89_scan_rec rec = { 0 };
	unsigned int waited = 0;
	unsigned int tick = 0;
	u32 lkpi_completions = 0u;
	u32 lkpi_tx = 0u;
	u32 lkpi_wq = 0u;
	int rc;

	wlan_bridge_stats(&c);
	if (c.attached == 0u) {
		CASE_ANCHOR("rtw89: scan FAIL not attached\n");
		return 1;
	}

	/* MANUAL: the stack reports the sweep instead of joining the first
	 * candidate the matcher likes (the join case decides) */
	wlan_bridge_set_manual(1);

	/* one sweep at a time; a supplicant sweep can never be armed in this
	 * image, but the bridge's own bookkeeping is what the wait is for */
	while (wlan_bridge_scan_pending() && waited < RTW89_SCAN_TIMEOUT_MS) {
		osDelay(RTW89_SCAN_POLL_MS);
		waited += RTW89_SCAN_POLL_MS;
	}

	rc = wlan_bridge_scan_start();
	while (rc == -EBUSY && waited < RTW89_SCAN_TIMEOUT_MS) {
		osDelay(RTW89_SCAN_POLL_MS);
		waited += RTW89_SCAN_POLL_MS;
		rc = wlan_bridge_scan_start();
	}
	if (rc != 0) {
		CASE_ANCHOR("rtw89: scan FAIL start rc=%d state=%d\n", rc,
		    wlan_bridge_state());
		return 1;
	}
	CASE_ANCHOR("rtw89: scan start state=%d\n", wlan_bridge_state());

	/*
	 * The hardware route: the device owns the dwell and reports its own
	 * completion, so there is no channel walk to advance here.  The 1 Hz
	 * tick still runs: the stack's node timers are what the join after
	 * this sweep depends on.
	 */
	while (wlan_bridge_scan_active() && waited < RTW89_SCAN_TIMEOUT_MS) {
		osDelay(RTW89_SCAN_POLL_MS);
		waited += RTW89_SCAN_POLL_MS;
		tick += RTW89_SCAN_POLL_MS;
		/* the software fallback's channel walk; a no-op check on the
		 * firmware route */
		(void)wlan_bridge_scan_dwell();
		if (tick >= 1000u) {
			tick = 0u;
			wlan_bridge_watchdog();
		}
	}

	(void)wlan_bridge_scan_walk(rtw89_scan_row, &rec);
	wlan_bridge_stats(&c);
	/*
	 * The sweep's own verdict line: how many candidates the table held,
	 * how the stack closed the window, and what the receive path saw.
	 * `entered=0` is a sweep that never opened; `close=4` is the device's
	 * own completion (WLAN_BRIDGE_SCAN_CLOSE_HW); `rx=0` with rows>0 would
	 * mean the table came from somewhere other than the air.
	 */
	CASE_ANCHOR("rtw89: scan done rows=%u bcn=%u bcnT=%u prsp=%u fcs=%u "
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
	CASE_ANCHOR("rtw89: scan lkpi completions=%u tx=%u wq=%u\n",
	    (unsigned)lkpi_completions, (unsigned)lkpi_tx,
	    (unsigned)lkpi_wq);

	if (rec.rows == 0u) {
		CASE_ANCHOR("rtw89: scan FAIL empty table (state=%d entered=%u "
		    "close=%u rx=%u)\n", wlan_bridge_state(), c.scan_entered,
		    c.scan_close, c.rx_frames);
		return 1;
	}
	if (rec.hit == 0u) {
		CASE_ANCHOR("rtw89: scan FAIL target not found (rows=%u)\n",
		    rec.rows);
		return 1;
	}
	if (wlan_bridge_scan_active()) {
		CASE_ANCHOR("rtw89: scan FAIL window still open (state=%d)\n",
		    wlan_bridge_state());
		return 1;
	}
	if (lkpi_completions == 0u && c.scan_close != 2u) {
		/* neither the driver's completion nor the walk closed the window:
		 * the bridge's state check or its watchdog did, and neither is a
		 * completed sweep */
		CASE_ANCHOR("rtw89: scan FAIL closed without a sweep end "
		    "(lkpi=0 close=%u bridge=%u/%u wq=%u)\n",
		    (unsigned)c.scan_close, (unsigned)c.scan_starts,
		    (unsigned)c.scan_completes, (unsigned)lkpi_wq);
		return 1;
	}
	CASE_ANCHOR("rtw89: scan PASS\n");
	return 0;
}