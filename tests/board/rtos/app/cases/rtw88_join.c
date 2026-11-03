/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtw88_join.c
 * @brief  Case rtw88_join: associate with the open AP through the net80211
 *         bridge on the RTL8821CS and wait for the stack to reach RUN.
 *
 * The route is the bridge's own station MLME, not the cfg80211 face: rtw88 is
 * a SoftMAC driver and its ops table (wireless-drivers/rtw88/mac80211.c:958)
 * has no `.connect`, so `lkpicfg_connect()` (linuxkpi_cfg80211.c) has nothing
 * to call and would fail by design.  `wlan_bridge_join()` looks the candidate
 * up by SSID in the sweep's table (a pinned BSSID is the rtw89 image's trap -
 * the owner's AP is not guaranteed to keep its BSSID between boots), runs
 * AUTH -> ASSOC through the seam's transmit path, and the driver learns about
 * the association through vif_cfg_changed / link_info_changed.
 *
 * The case pumps what this port has no thread for: wlan_bridge_mgmt_tick()
 * (the auth retry, at this case's 200 ms poll cadence) and
 * wlan_bridge_watchdog() (the stack's 1 Hz tick).
 *
 * What a pass claims: the stack reached RUN, a management frame left the
 * bridge, the driver completed it, the AP answered the auth (auth_answers),
 * and beacons from the join target reached the stack (the radio sits on the
 * AP's channel and the chip's address filter passes the AP).  A RUN alone is
 * not enough: it can be reached with every frame lost on the radio.
 *
 * `bad_auth` is printed but is not a gate: the stack counts an auth response
 * that arrives after it has already moved on (a delayed duplicate of the first
 * attempt, for instance) there, so a healthy join can carry it.  It is the
 * discriminator for "the AP answered but the stack refused the answer".
 *
 * Anchors:
 *   rtw88: join bssid=<bssid> ssid=<s> freq=<f> rssi=<r> hits=<n> rows=<n>
 *   rtw88: join start ssid=<s> bssid=<bssid> state=<n> rc=<n> attempt=<n>
 *   rtw88: join wait t=<s> state=<S> mgt_tx=<n> txc=<n> txfail=<n> rx=<n>
 *           rx_age=<ms> bcnT=<n> bad_auth=<n> auth_retries=<n>
 *           auth_answers=<n> txq=<n> stopped=<n> wq=<ptr>          (INFO)
 *   rtw88: join lkpi tx=<n> scan=<n> wq=<n> rx_in=<n> rx_ref=<n>
 *           rx_drop=<n> rx_frames=<n> rx_q=<n>
 *   rtw88: join sdio final clock=<n> width=<n> blk=<n>/<n> io_direct=<n>
 *           io_ext=<n> errors=<n>                              (INFO)
 *   rtw88: join mci final transfers=<n> errors=<n> cmd52=<n> cmd53=<n> ...
 *   rtw88: join settle <ms> bcnT=<n> rx=<n>                     (INFO)
 *   rtw88: join counters state=<n> mgt_tx=<n> txc=<n> txfail=<n> rx=<n>
 *           rx_data=<n> bcnT=<n> auth_retries=<n> auth_answers=<n>
 *           bad_auth=<n> txq=<n> stopped=<n> tune=<n>/<n> freq=<MHz>
 *   rtw88: join ok state=4 mgt_tx=<n> txc=<n> auth=<n> bcnT=<n>
 *   rtw88: join PASS
 *
 * A failure is `rtw88: join FAIL <reason>` with the same discriminators, so
 * "no frame left the bridge" (mgt_tx=0), "the driver never completed it"
 * (mgt_tx>0, txc=0), "the radio is not on the AP's channel" (bcnT=0 after the
 * settle window), "the AP never answered the auth" (auth_answers=0) and "the
 * answer was refused above the radio" (bad_auth>0) are told apart on the spot.
 * The beacon count is read after a bounded settle (the first beacon can be up
 * to one beacon interval away from the assoc response): the join's own claim
 * is the RUN state plus the AP's auth answer, the beacon is the channel
 * evidence, and one boot of this round read zero at the instant RUN appeared
 * while the data plane worked immediately afterwards.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "wlan_bridge.h"

#if defined(RTOS_CASE_RTW88_JOIN)

#include "linuxkpi_sdio.h"
#include "sdmmc_port.h"
#include "mci.h"

/* The SSID is a profile fact (the bench changes APs between rounds): it comes
 * from the generated configuration header, CONFIG_RTW88_AP_SSID.  Kconfig always
 * defines the symbol, so the fail-closed check is on its value: an image that
 * selects this case without naming an AP does not compile. */
#ifndef CONFIG_RTW88_AP_SSID
#error "CONFIG_RTW88_AP_SSID must name the AP this image joins (a profile fact: the bench changes APs between rounds)"
#endif
_Static_assert(sizeof(CONFIG_RTW88_AP_SSID) > 1,
	       "CONFIG_RTW88_AP_SSID is empty: name the AP in the defconfig");

/* The substitute's completion counters are declared in the linuxkpi world's
 * header, which a net80211 case must not include (the two worlds own the same
 * names), so the scalars this case reads are repeated here exactly as
 * rtl8xxxu_join.c does. */
void lkpi80211_completion_counters(uint32_t *scan, uint32_t *tx);
uint32_t lkpi80211_tx_failures(void);
void lkpi80211_rx_boundary_counters(uint32_t *in, uint32_t *refused,
    uint32_t *drops, uint32_t *frames, uint32_t *qlen);

#define RTW88_JOIN_TIMEOUT_MS	30000u
#define RTW88_JOIN_POLL_MS	200u
#define RTW88_JOIN_PULSE_MS	2000u
#define RTW88_JOIN_SCAN_MS	90000u
#define RTW88_JOIN_SETTLE_MS	2000u
#define RTW88_JOIN_ATTEMPTS	2u

#define RTW88_JOIN_SSID		CONFIG_RTW88_AP_SSID
#define RTW88_JOIN_SSID_LEN	(sizeof(RTW88_JOIN_SSID) - 1u)
#define RTW88_JOIN_RUN		4

struct rtw88_join_rec {
	unsigned int	rows;
	unsigned int	hit;
	int		freq;
	uint8_t		bssid[6];
	int		rssi;
};

static void
rtw88_join_row(const uint8_t bssid[6], const uint8_t *ssid, size_t ssid_len,
    uint32_t freq, int rssi, void *arg)
{
	struct rtw88_join_rec *rec = arg;

	rec->rows++;
	if (ssid == NULL || ssid_len != RTW88_JOIN_SSID_LEN)
		return;
	if (memcmp(ssid, RTW88_JOIN_SSID, RTW88_JOIN_SSID_LEN) != 0)
		return;
	rec->hit++;
	/* the strongest sighting: the same SSID can be carried by more than one
	 * radio (a mesh node repeats it), and the RSSI tells which one is here */
	if (rec->hit == 1u || rssi > rec->rssi) {
		memcpy(rec->bssid, bssid, 6);
		rec->freq = (int)freq;
		rec->rssi = rssi;
	}
}

static const char *
rtw88_join_state_txt(int state)
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

/*
 * The bus's own account around the join: the transfer shape (direct register
 * accesses vs buffered transfers, and how many of either failed) and the
 * controller's last-command state.  A join whose frames never completed is
 * read here first: `errors` and the controller's `last_err` say whether the
 * transfers themselves failed.
 */
static void
rtw88_join_sdio_anchor(const char *where)
{
	struct lkpi_sdio_stats stats;
	struct lkpi_sdio_bus_info bus;
	struct mci_host *hc = sdmmc_port_mci_host();

	memset(&stats, 0, sizeof(stats));
	memset(&bus, 0, sizeof(bus));
	lkpi_sdio_stats_get(&stats);
	lkpi_sdio_bus_info_get(&bus);
	CASE_INFO("rtw88: join sdio %s clock=%u width=%u blk=%u/%u io_direct=%u "
	    "io_ext=%u errors=%u\n", where, (unsigned)bus.clock_hz,
	    (unsigned)bus.bus_width, (unsigned)bus.blk_size,
	    (unsigned)bus.blk_max, stats.io_direct, stats.io_extended,
	    stats.errors);
	if (hc == NULL)
		return;
	{
		struct mci_state st;

		memset(&st, 0, sizeof(st));
		mci_state_get(hc, &st);
		CASE_INFO("rtw88: join mci %s transfers=%u errors=%u cmd52=%u "
		    "cmd53=%u last_cmd=%u last_err=%d data_ev=%08x\n", where,
		    (unsigned)st.transfers, (unsigned)st.errors,
		    (unsigned)st.cmd52, (unsigned)st.cmd53,
		    (unsigned)st.last_cmd, st.last_err, (unsigned)st.data_ev);
	}
}

/*
 * One bounded sweep, used only when the caller's image carries no scan table
 * (the rtw88_scan case normally owns this and leaves its table behind).  Same
 * route as that case: the software walk, advanced by the dwell entry, with
 * the 1 Hz tick pumped from here.
 */
static int
rtw88_join_sweep(void)
{
	unsigned int waited = 0u;
	unsigned int tick = 0u;
	int rc;

	while (wlan_bridge_scan_pending() && waited < RTW88_JOIN_SCAN_MS) {
		(void)osDelay(RTW88_JOIN_POLL_MS);
		waited += RTW88_JOIN_POLL_MS;
		(void)wlan_bridge_scan_dwell();
	}
	rc = wlan_bridge_scan_start();
	while (rc == -EBUSY && waited < RTW88_JOIN_SCAN_MS) {
		(void)osDelay(RTW88_JOIN_POLL_MS);
		waited += RTW88_JOIN_POLL_MS;
		rc = wlan_bridge_scan_start();
	}
	if (rc != 0)
		return rc;
	while (wlan_bridge_scan_active() && waited < RTW88_JOIN_SCAN_MS) {
		(void)osDelay(RTW88_JOIN_POLL_MS);
		waited += RTW88_JOIN_POLL_MS;
		tick += RTW88_JOIN_POLL_MS;
		(void)wlan_bridge_scan_dwell();
		if (tick >= 1000u) {
			tick = 0u;
			wlan_bridge_watchdog();
		}
	}
	return 0;
}

int
case_rtw88_join_run(void)
{
	struct wlan_bridge_counters c;
	struct rtw88_join_rec rec = { 0u, 0u, 0, { 0 }, 0 };
	unsigned int waited;
	unsigned int tick;
	unsigned int pulse;
	unsigned int last_rx;
	unsigned int last_rx_ms;
	u32 lkpi_scan = 0u;
	u32 lkpi_tx = 0u;
	u32 lkpi_txfail = 0u;
	u32 lkpi_wq = 0u;
	void *wq_now = NULL;
	int rc;

	wlan_bridge_stats(&c);
	if (c.attached == 0u) {
		CASE_ANCHOR("rtw88: join FAIL not attached\n");
		return 1;
	}

	/* the caller's image may still be in AUTO mode; MANUAL keeps the stack
	 * from joining a candidate on its own, so this case's join is the only
	 * actor (idempotent with the scan case) */
	wlan_bridge_set_manual(1);

	(void)wlan_bridge_scan_walk(rtw88_join_row, &rec);
	if (rec.rows == 0u) {
		CASE_INFO("rtw88: join scan table empty; one bounded sweep\n");
		rc = rtw88_join_sweep();
		if (rc != 0)
			CASE_INFO("rtw88: join sweep rc=%d\n", rc);
		rec.rows = 0u;
		rec.hit = 0u;
		(void)wlan_bridge_scan_walk(rtw88_join_row, &rec);
	}
	if (rec.hit == 0u) {
		CASE_ANCHOR("rtw88: join FAIL AP not in scan table (ssid=%s "
		    "rows=%u)\n", RTW88_JOIN_SSID, rec.rows);
		return 1;
	}
	CASE_ANCHOR("rtw88: join bssid=%02x:%02x:%02x:%02x:%02x:%02x ssid=%s "
	    "freq=%d rssi=%d hits=%u rows=%u\n", rec.bssid[0], rec.bssid[1],
	    rec.bssid[2], rec.bssid[3], rec.bssid[4], rec.bssid[5],
	    RTW88_JOIN_SSID, rec.freq, rec.rssi, rec.hit, rec.rows);

	rtw88_join_sdio_anchor("pre");

	/* The owner's AP may drop the first auth after a fresh association (the
	 * 8188EUS line measured the same first-join fallback on this bench), so
	 * the exchange gets two attempts from a clean state; an intermediate
	 * timeout is an INFO line, never the FAIL anchor, so a passing retry is
	 * not later read as a failure. */
	if (wlan_bridge_state() != RTW88_JOIN_RUN) {
		for (unsigned int attempt = 1u; attempt <= RTW88_JOIN_ATTEMPTS;
		     attempt++) {
			waited = 0u;
			tick = 0u;
			pulse = 0u;
			last_rx = 0u;
			last_rx_ms = 0u;
			rc = wlan_bridge_join((const uint8_t *)RTW88_JOIN_SSID,
			    RTW88_JOIN_SSID_LEN, rec.bssid, NULL, 0);
			CASE_ANCHOR("rtw88: join start ssid=%s "
			    "bssid=%02x:%02x:%02x:%02x:%02x:%02x state=%d "
			    "rc=%d attempt=%u\n", RTW88_JOIN_SSID,
			    rec.bssid[0], rec.bssid[1], rec.bssid[2],
			    rec.bssid[3], rec.bssid[4], rec.bssid[5],
			    wlan_bridge_state(), rc, attempt);
			if (rc != 0) {
				CASE_INFO("rtw88: join attempt %u call rc=%d "
				    "state=%s\n", attempt, rc,
				    rtw88_join_state_txt(wlan_bridge_state()));
				(void)osDelay(1000u);
				continue;
			}
			while (wlan_bridge_state() != RTW88_JOIN_RUN &&
			       waited < RTW88_JOIN_TIMEOUT_MS) {
				(void)osDelay(RTW88_JOIN_POLL_MS);
				waited += RTW88_JOIN_POLL_MS;
				tick += RTW88_JOIN_POLL_MS;
				pulse += RTW88_JOIN_POLL_MS;
				/* the auth retry; it no-ops unless an auth is
				 * pending, so the poll cadence is the caller's */
				wlan_bridge_mgmt_tick();
				if (tick >= 1000u) {
					tick = 0u;
					wlan_bridge_watchdog();
				}
				if (pulse < RTW88_JOIN_PULSE_MS)
					continue;
				pulse = 0u;
				wlan_bridge_stats(&c);
				/* `rx_age` is how long the receive count has
				 * been standing still; `bcnT` is the beacons
				 * the chip heard from the join target (the
				 * radio-off-channel signature). */
				if (c.rx_frames != last_rx) {
					last_rx = c.rx_frames;
					last_rx_ms = waited;
				}
				lkpi_txfail = lkpi80211_tx_failures();
				wq_now = lkpi_wq_current_work_func();
				CASE_INFO("rtw88: join wait t=%us state=%s "
				    "mgt_tx=%u txc=%u txfail=%u rx=%u "
				    "rx_age=%ums bcnT=%u bad_auth=%u "
				    "auth_retries=%u auth_answers=%u txq=%u "
				    "stopped=%u wq=%p\n", waited / 1000u,
				    rtw88_join_state_txt(wlan_bridge_state()),
				    c.mgt_tx, c.tx_completes,
				    (unsigned)lkpi_txfail, c.rx_frames,
				    waited - last_rx_ms, c.rx_beacon_tgt,
				    (unsigned)wlan_bridge_stat_bad_auth(),
				    c.auth_retries, c.auth_answers,
				    c.txq_len, c.tx_stopped, wq_now);
			}
			if (wlan_bridge_state() == RTW88_JOIN_RUN)
				break;
			CASE_INFO("rtw88: join attempt %u timeout state=%s "
			    "mgt_tx=%u txc=%u rx=%u bcnT=%u auth_answers=%u\n",
			    attempt, rtw88_join_state_txt(wlan_bridge_state()),
			    c.mgt_tx, c.tx_completes, c.rx_frames,
			    c.rx_beacon_tgt, c.auth_answers);
			(void)osDelay(1000u);
		}
	}

	/*
	 * The settle window.  RUN is reached by the assoc response, and the
	 * target-beacon counter is the "the radio really sits on the AP's
	 * channel" evidence - but the first beacon after the assoc response can
	 * be up to one beacon interval away.  One boot (the fifth of this
	 * round) read bcnT=0 at the instant RUN appeared while the AP answered
	 * auth and the data plane immediately worked, so the counter is given a
	 * bounded settle before it is judged: a radio that is off the channel
	 * still reads zero after it, an unlucky poll does not.
	 */
	if (wlan_bridge_state() == RTW88_JOIN_RUN) {
		unsigned int settle = 0u;

		wlan_bridge_stats(&c);
		while ((c.rx_beacon_tgt == 0u) && (settle < RTW88_JOIN_SETTLE_MS)) {
			(void)osDelay(RTW88_JOIN_POLL_MS);
			settle += RTW88_JOIN_POLL_MS;
			wlan_bridge_watchdog();
			wlan_bridge_stats(&c);
		}
		CASE_INFO("rtw88: join settle %u ms bcnT=%u rx=%u\n", settle,
		    c.rx_beacon_tgt, c.rx_frames);
	}

	wlan_bridge_stats(&c);
	lkpi80211_completion_counters(&lkpi_scan, &lkpi_tx);
	{
		struct lkpi_wq_stats wq;

		memset(&wq, 0, sizeof(wq));
		lkpi_wq_stats_get(&wq);
		lkpi_wq = wq.runs;
	}
	{
		uint32_t rx_in = 0u, rx_ref = 0u, rx_drop = 0u, rx_frames = 0u;
		uint32_t rx_q = 0u;

		lkpi80211_rx_boundary_counters(&rx_in, &rx_ref, &rx_drop,
		    &rx_frames, &rx_q);
		CASE_ANCHOR("rtw88: join lkpi tx=%u scan=%u wq=%u rx_in=%u "
		    "rx_ref=%u rx_drop=%u rx_frames=%u rx_q=%u\n",
		    (unsigned)lkpi_tx, (unsigned)lkpi_scan, (unsigned)lkpi_wq,
		    (unsigned)rx_in, (unsigned)rx_ref, (unsigned)rx_drop,
		    (unsigned)rx_frames, (unsigned)rx_q);
	}
	rtw88_join_sdio_anchor("final");
	CASE_ANCHOR("rtw88: join counters state=%d mgt_tx=%u txc=%u "
	    "txfail=%u rx=%u rx_data=%u bcnT=%u auth_retries=%u "
	    "auth_answers=%u bad_auth=%u txq=%u stopped=%u tune=%u/%u "
	    "freq=%u\n", wlan_bridge_state(), c.mgt_tx, c.tx_completes,
	    (unsigned)lkpi_txfail, c.rx_frames, c.rx_data, c.rx_beacon_tgt,
	    c.auth_retries, c.auth_answers,
	    (unsigned)wlan_bridge_stat_bad_auth(), c.txq_len, c.tx_stopped,
	    c.tune_calls, c.tune_fails, (unsigned)c.tuned_freq);

	if (wlan_bridge_state() != RTW88_JOIN_RUN) {
		CASE_ANCHOR("rtw88: join FAIL timeout state=%s mgt_tx=%u "
		    "txc=%u txfail=%u rx=%u bcnT=%u bad_auth=%u "
		    "auth_retries=%u auth_answers=%u tune=%u/%u freq=%u\n",
		    rtw88_join_state_txt(wlan_bridge_state()), c.mgt_tx,
		    c.tx_completes, (unsigned)lkpi_txfail, c.rx_frames,
		    c.rx_beacon_tgt, (unsigned)wlan_bridge_stat_bad_auth(),
		    c.auth_retries, c.auth_answers, c.tune_calls,
		    c.tune_fails, (unsigned)c.tuned_freq);
		return 1;
	}

	CASE_ANCHOR("rtw88: join ok state=%d mgt_tx=%u txc=%u auth=%u "
	    "bcnT=%u\n", wlan_bridge_state(), c.mgt_tx, c.tx_completes,
	    c.auth_answers, c.rx_beacon_tgt);
	if (c.mgt_tx == 0u) {
		/* RUN with no management frame handed to the driver is a stack
		 * state without an exchange; report it rather than calling the
		 * join proven */
		CASE_ANCHOR("rtw88: join FAIL RUN without a management frame\n");
		return 1;
	}
	if (c.tx_completes == 0u) {
		CASE_ANCHOR("rtw88: join FAIL no transmit completion "
		    "(lkpi=%u wq=%u mgt_tx=%u)\n", (unsigned)lkpi_tx,
		    (unsigned)lkpi_wq, c.mgt_tx);
		return 1;
	}
	if (c.auth_answers == 0u) {
		/* the AP-originated half of the verdict: the stack can only
		 * reach RUN here by receiving an auth response, so a zero count
		 * with RUN is a bookkeeping fault; a zero count without RUN
		 * never got this far */
		CASE_ANCHOR("rtw88: join FAIL no auth answer from the AP "
		    "(bcnT=%u bad_auth=%u)\n", c.rx_beacon_tgt,
		    (unsigned)wlan_bridge_stat_bad_auth());
		return 1;
	}
	if (c.rx_beacon_tgt == 0u) {
		CASE_ANCHOR("rtw88: join FAIL no beacon from the join target "
		    "(tune=%u/%u freq=%u)\n", c.tune_calls, c.tune_fails,
		    (unsigned)c.tuned_freq);
		return 1;
	}
	CASE_ANCHOR("rtw88: join PASS\n");
	return 0;
}

#endif /* RTOS_CASE_RTW88_JOIN */
