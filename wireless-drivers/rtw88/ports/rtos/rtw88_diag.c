/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    rtw88/ports/rtos/rtw88_diag.c
 * @brief   Read-only TX-power probe for the RTL8821CU RTOS arm.
 *
 * @design The driver is byte-identical to the GT pin, so the probe lives in a
 *         first-party ports/ unit compiled in the driver's flag world, not in
 *         the driver bytes.  It reads the driver's own state after attach - the
 *         efuse power indices and the computed tx_pwr_tbl - and prints them.
 *         It writes nothing.
 *
 *         Why: the 2026-11-14 EHCI round measured the board's uplink at
 *         -48 dBm at the AP while the GT on the same card/port reads -24 dBm,
 *         with the wiphy's regulatory cap at 20 dBm on both sides and a clean
 *         USB ledger (submits/completes 402/401, tx_fail=0).  The remaining
 *         candidates are the efuse power table (read wrong -> low indices) and
 *         the rate/TX-status path (right power, wrong rate).  The efuse
 *         indices and tx_pwr_tbl separate the two: a correct table with a weak
 *         AP signal points at the rate/status side; zero/garbage indices point
 *         at the efuse read.
 *
 *         The GT's own values are the reference; the card is the same, so a
 *         correct read must land in the same index band (the rtw88 efuse power
 *         indices for a 2.4 GHz 1T1R 8821CU are in the 0x20-0x3f range, and
 *         tx_pwr_tbl is the resulting dBm per rate).
 *
 *         2026-11-14 second round: the first round's report line never reached
 *         the console (the case's sink forwards only lines containing
 *         "rtw88_diag", and the line was printed as eight fragments) and its
 *         sn mapping was the driver's *opposite* branch (the driver reads V0 on
 *         the plain C2H id and V1 on the HALMAC ext id; this file had them
 *         swapped), so "the report's sn does not match the queue" had no
 *         evidence behind it.  The wrapper now mirrors the driver's mapping and
 *         prints one complete line.  `rtw88_diag_state()` adds the link's
 *         power-save state and the full per-rate power table, which the first
 *         round's twelve-entry print left as a blind spot (the HT entries are
 *         past index 11).
 *
 *         The C2H inline seam is no longer registered here: the round ran it
 *         as an arm and it did not change the purge count, so the baseline this
 *         probe observes is the driver's own deferred work (the same shape GT
 *         Linux runs).
 *
 *         Not a delivery feature: only the diagnostic profile/case calls it.
 *         The type is void* so the stack-world case does not include the
 *         driver headers.
 * @date    14.11.2026
 */

#include <linux/types.h>
#include <linux/etherdevice.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

/* mac80211's internal local: the wiphy's private area, whose first member is
 * the driver-visible ieee80211_hw (ieee80211_i.h:1448-1452). */
#include "ieee80211_i.h"

#include "main.h"
#include "efuse.h"
#include "fw.h"
#include "tx.h"
#include "linuxkpi_core.h"

static void rtw88_diag_amsdu_caps(struct rtw_dev *rtwdev,
				  struct ieee80211_hw *hw);

void
rtw88_diag_txpower(void *wiphy_v)
{
	struct wiphy *wiphy = (struct wiphy *)wiphy_v;
	struct ieee80211_local *local;
	struct ieee80211_hw *hw;
	struct rtw_dev *rtwdev;
	struct rtw_2g_txpwr_idx *idx2g;
	unsigned int regd;
	char ratebuf[160];
	int i, off = 0;

	if (wiphy == NULL)
		return;
	local = (struct ieee80211_local *)wiphy_priv(wiphy);
	if (local == NULL)
		return;
	hw = &local->hw;
	rtwdev = (struct rtw_dev *)hw->priv;
	rtw88_diag_amsdu_caps(rtwdev, hw);
	if (rtwdev == NULL)
		return;

	idx2g = &rtwdev->efuse.txpwr_idx_table[0].pwr_idx_2g;

	lkpi_printf(ULOG_INFO_LEVEL,
	    "rtw88_diag: state chip=%u cut=%u rf_type=%u paths=%u ch=%u "
	    "bw=%u regd=%u pa2g=%u swing2g=%u\n",
	    (unsigned)rtwdev->chip->id, (unsigned)rtwdev->hal.cut_version,
	    (unsigned)rtwdev->hal.rf_type, (unsigned)rtwdev->hal.rf_path_num,
	    (unsigned)rtwdev->hal.current_channel,
	    (unsigned)rtwdev->hal.current_band_width,
	    (unsigned)rtwdev->efuse.regd,
	    (unsigned)rtwdev->efuse.pa_type_2g,
	    (unsigned)rtwdev->efuse.tx_bb_swing_setting_2g);

	lkpi_printf(ULOG_INFO_LEVEL,
	    "rtw88_diag: efuse addr=%02x:%02x:%02x:%02x:%02x:%02x "
	    "cck=%02x,%02x,%02x,%02x,%02x,%02x "
	    "bw40=%02x,%02x,%02x,%02x,%02x\n",
	    rtwdev->efuse.addr[0], rtwdev->efuse.addr[1],
	    rtwdev->efuse.addr[2], rtwdev->efuse.addr[3],
	    rtwdev->efuse.addr[4], rtwdev->efuse.addr[5],
	    idx2g->cck_base[0], idx2g->cck_base[1], idx2g->cck_base[2],
	    idx2g->cck_base[3], idx2g->cck_base[4], idx2g->cck_base[5],
	    idx2g->bw40_base[0], idx2g->bw40_base[1], idx2g->bw40_base[2],
	    idx2g->bw40_base[3], idx2g->bw40_base[4]);

	/* The first 12 entries are the 2.4 GHz CCK/OFDM entries; the HT entries
	 * follow.  Print the first path only: one path on this 1T1R part, and a
	 * second path of zeros would say the face is 2T2R, not the card's
	 * state. */
	for (i = 0; i < 12 && off < (int)sizeof(ratebuf) - 8; i++)
		off += snprintf(ratebuf + off, sizeof(ratebuf) - (size_t)off,
				"%d%s", rtwdev->hal.tx_pwr_tbl[0][i],
				(i == 11) ? "" : ",");
	lkpi_printf(ULOG_INFO_LEVEL, "rtw88_diag: tx_pwr_tbl path0=%s\n",
		    ratebuf);

	/* The efuse's regd is a small index; clamp before indexing the limit
	 * table so a corrupt efuse byte cannot make the probe itself fault. */
	regd = rtwdev->efuse.regd;
	if (regd >= RTW_REGD_MAX)
		regd = 0;
	{
		u8 p0[16], p1[16];
		int j, err = 0;

		for (j = 0; j < 16; j++) {
			p0[j] = 0;
			if (rtw_read8_physical_efuse(rtwdev, (u16)j, &p0[j]))
				err++;
		}
		for (j = 0; j < 16; j++) {
			p1[j] = 0;
			if (rtw_read8_physical_efuse(rtwdev, (u16)(0x100 + j),
						     &p1[j]))
				err++;
		}
		lkpi_printf(ULOG_INFO_LEVEL,
		    "rtw88_diag: efuse sizes size=%u phys=%u logical=%u "
		    "protect=%u read_err=%d\n",
		    rtwdev->efuse.size, rtwdev->efuse.physical_size,
		    rtwdev->efuse.logical_size, rtwdev->efuse.protect_size,
		    err);
		lkpi_printf(ULOG_INFO_LEVEL,
		    "rtw88_diag: efuse phys[0x000]=%02x,%02x,%02x,%02x,"
		    "%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n",
		    p0[0], p0[1], p0[2], p0[3], p0[4], p0[5], p0[6], p0[7],
		    p0[8], p0[9], p0[10], p0[11], p0[12], p0[13], p0[14],
		    p0[15]);
		lkpi_printf(ULOG_INFO_LEVEL,
		    "rtw88_diag: efuse phys[0x100]=%02x,%02x,%02x,%02x,"
		    "%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n",
		    p1[0], p1[1], p1[2], p1[3], p1[4], p1[5], p1[6], p1[7],
		    p1[8], p1[9], p1[10], p1[11], p1[12], p1[13], p1[14],
		    p1[15]);
	}

	lkpi_printf(ULOG_INFO_LEVEL,
	    "rtw88_diag: limit_2g regd=%u ch0=%d,%d,%d,%d ch1=%d,%d,%d,%d\n",
	    regd,
	    rtwdev->hal.tx_pwr_limit_2g[regd][0][0][0],
	    rtwdev->hal.tx_pwr_limit_2g[regd][0][1][0],
	    rtwdev->hal.tx_pwr_limit_2g[regd][0][2][0],
	    rtwdev->hal.tx_pwr_limit_2g[regd][0][3][0],
	    rtwdev->hal.tx_pwr_limit_2g[regd][0][0][1],
	    rtwdev->hal.tx_pwr_limit_2g[regd][0][1][1],
	    rtwdev->hal.tx_pwr_limit_2g[regd][0][2][1],
	    rtwdev->hal.tx_pwr_limit_2g[regd][0][3][1]);
}


/*
 * C2H / TX-report accounting (--wrap'd from wl80211.mk).
 *
 * The rate-control question is whether the firmware's CCX TX reports arrive
 * with the sn the driver put in the TX descriptor.  The driver reads two
 * layouts and picks by the C2H id it was called with:
 *
 *   src == C2H_CCX_TX_RPT  (the plain C2H, id 0x03)  -> V0 macros
 *   src == C2H_CCX_RPT     (the HALMAC ext, id 0xff, sub 0x0f) -> V1
 *   (tx.c:245-250)
 *
 * The first round's wrapper had those two branches swapped, so its printed
 * "sn" was the layout the driver does not read on that path and the
 * conclusion "the report's sn does not match" rested on the wrong byte.  This
 * wrapper mirrors the driver exactly, prints the other layout alongside it,
 * and prints the queue's stored sns and the report's whole 12-byte payload in
 * one line - one line because the case's log sink forwards only lines that
 * contain "rtw88_diag" (the first round printed eight fragments, so only the
 * first one ever reached the console).
 *
 * Read-only: the wrapped driver handler runs afterwards, unchanged.
 */
static unsigned int diag_c2h_pkts;
static unsigned int diag_txrpt_calls[2];	/* [0] = V0 path, [1] = V1 path */
static unsigned int diag_txrpt_q_empty;
static unsigned int diag_txrpt_sn_match;
static unsigned int diag_txrpt_alt_match;

void __real_rtw_fw_c2h_cmd_rx_irqsafe(struct rtw_dev *rtwdev, u32 pkt_offset,
				      struct sk_buff *skb);
void __real_rtw_tx_report_handle(struct rtw_dev *rtwdev, struct sk_buff *skb,
				 int src);

void
__wrap_rtw_fw_c2h_cmd_rx_irqsafe(struct rtw_dev *rtwdev, u32 pkt_offset,
				 struct sk_buff *skb)
{
	if (skb != NULL && pkt_offset + 2U < skb->len) {
		struct rtw_c2h_cmd *c2h =
			(struct rtw_c2h_cmd *)(skb->data + pkt_offset);
		u8 len = (u8)(skb->len - pkt_offset - 2U);

		diag_c2h_pkts++;
		if (diag_c2h_pkts <= 20U || (diag_c2h_pkts % 64U) == 0U)
			lkpi_printf(ULOG_INFO_LEVEL,
			    "rtw88_diag: c2h #%u id=0x%02x seq=0x%02x "
			    "sub=0x%02x len=%u\n", diag_c2h_pkts, c2h->id,
			    c2h->seq, c2h->payload[0], (unsigned)len);
	}
	__real_rtw_fw_c2h_cmd_rx_irqsafe(rtwdev, pkt_offset, skb);
}

void
__wrap_rtw_tx_report_handle(struct rtw_dev *rtwdev, struct sk_buff *skb,
			    int src)
{
	struct rtw_c2h_cmd *c2h;
	struct sk_buff *cur;
	char line[224];
	int n = 0, qn = 0, i;
	u8 sn = 0, st = 0, alt = 0;
	unsigned int idx = (src == C2H_CCX_TX_RPT) ? 0U : 1U;

	diag_txrpt_calls[idx]++;
	c2h = get_c2h_from_skb(skb);

	/* the driver's own mapping (tx.c:245-250), and the other layout as a
	 * cross-check for "the firmware reports the other revision" */
	if (src == C2H_CCX_TX_RPT) {
		sn = GET_CCX_REPORT_SEQNUM_V0(c2h->payload);
		st = GET_CCX_REPORT_STATUS_V0(c2h->payload);
		alt = GET_CCX_REPORT_SEQNUM_V1(c2h->payload);
	} else {
		sn = GET_CCX_REPORT_SEQNUM_V1(c2h->payload);
		st = GET_CCX_REPORT_STATUS_V1(c2h->payload);
		alt = GET_CCX_REPORT_SEQNUM_V0(c2h->payload);
	}

	n += snprintf(line + n, sizeof(line) - (size_t)n,
	    "rtw88_diag: txrpt #%u src=%d id=0x%02x seq=0x%02x sn=0x%02x "
	    "st=0x%02x alt_sn=0x%02x q=",
	    diag_txrpt_calls[0] + diag_txrpt_calls[1], src, c2h->id, c2h->seq,
	    sn, st, alt);

	skb_queue_walk(&rtwdev->tx_report.queue, cur) {
		u8 qsn;

		if (qn >= 6 || n >= (int)sizeof(line) - 24)
			break;
		qsn = *(u8 *)IEEE80211_SKB_CB(cur)->status.status_driver_data;
		if (qsn == sn)
			diag_txrpt_sn_match++;
		if (qsn == alt)
			diag_txrpt_alt_match++;
		n += snprintf(line + n, sizeof(line) - (size_t)n, "%s%02x",
			      qn ? "," : "", qsn);
		qn++;
	}
	if (qn == 0) {
		diag_txrpt_q_empty++;
		n += snprintf(line + n, sizeof(line) - (size_t)n, "empty");
	}
	n += snprintf(line + n, sizeof(line) - (size_t)n, " payload=");
	for (i = 0; i < 12 && n < (int)sizeof(line) - 4; i++)
		n += snprintf(line + n, sizeof(line) - (size_t)n, "%s%02x",
			      i ? "," : "", c2h->payload[i]);
	snprintf(line + n, sizeof(line) - (size_t)n, "\n");

	/* one call: the case's sink matches on the "rtw88_diag" prefix and a
	 * fragment without it is dropped before the console */
	lkpi_printf(ULOG_INFO_LEVEL, "%s", line);

	__real_rtw_tx_report_handle(rtwdev, skb, src);
}

/*
 * The link's state at the moment the case asks (2026-11-14, second round).
 *
 * Three faces the first round could not see:
 *   - power save: rtwdev->ps_enabled is what the driver's own LPS gate reads
 *     (ps.c:329) and vif->cfg.ps is what mac80211 sets from
 *     NL80211_CMD_SET_POWER_SAVE.  The round's RTT spikes were 105 ms, the
 *     driver's leave-LPS wait is 100 ms (ps.c:144), and the case's sink never
 *     forwarded the driver's "failed to leave lps state" line - so "PS off"
 *     was a command's return code, not a state.
 *   - the whole per-rate power table: the first probe printed 12 entries; the
 *     HT rates (the ones GT's link actually uses, 65.0 Mbit/s MCS7) start past
 *     index 11 and were never printed.
 *   - the report accounting above: whether any report's sn ever matched the
 *     queue over the whole boot.
 *
 * Read-only.
 */
void
rtw88_diag_state(void *wiphy_v)
{
	struct wiphy *wiphy = (struct wiphy *)wiphy_v;
	struct ieee80211_local *local;
	struct ieee80211_hw *hw;
	struct ieee80211_sub_if_data *sdata;
	struct rtw_dev *rtwdev;
	struct rtw_lps_conf *lps;
	char tbl[192];
	unsigned int cfg_ps = 9U, assoc = 9U;
	int i, n = 0;

	if (wiphy == NULL)
		return;
	local = (struct ieee80211_local *)wiphy_priv(wiphy);
	if (local == NULL)
		return;
	hw = &local->hw;
	rtwdev = (struct rtw_dev *)hw->priv;
	if (rtwdev == NULL)
		return;
	lps = &rtwdev->lps_conf;

	for (i = 0; i < DESC_RATE_MAX && n < (int)sizeof(tbl) - 6; i++)
		n += snprintf(tbl + n, sizeof(tbl) - (size_t)n, "%s%d",
			      i ? "," : "", rtwdev->hal.tx_pwr_tbl[0][i]);
	lkpi_printf(ULOG_INFO_LEVEL,
	    "rtw88_diag: pwr ch=%u bw=%u max_idx=%u rate_tbl=%s\n",
	    rtwdev->hal.current_channel, rtwdev->hal.current_band_width,
	    (unsigned)rtwdev->chip->max_power_index, tbl);

	list_for_each_entry(sdata, &local->interfaces, list) {
		if (sdata->vif.type != NL80211_IFTYPE_STATION)
			continue;
		cfg_ps = sdata->vif.cfg.ps ? 1U : 0U;
		assoc = sdata->vif.cfg.assoc ? 1U : 0U;
		break;
	}
	lkpi_printf(ULOG_INFO_LEVEL,
	    "rtw88_diag: ps enabled=%u lps_state=%d lps_mode=%d awake=%u "
	    "smart_ps=%u cfg_ps=%u assoc=%u conf_ps=%u\n",
	    rtwdev->ps_enabled ? 1U : 0U, (int)lps->state, (int)lps->mode,
	    (unsigned)lps->awake_interval, (unsigned)lps->smart_ps, cfg_ps,
	    assoc, (hw->conf.flags & IEEE80211_CONF_PS) ? 1U : 0U);

	/* The firmware's own rate decision: C2H_RA_RPT (id 0x0c) carries it and
	 * rtw_fw_ra_report_handle stores it in dm_info.tx_rate (fw.c:202-218).
	 * The air capture cannot see this card's data frames on this bench (the
	 * co-located monitor sees the AP's beacons but not its unicast data), so
	 * this descriptor is the rate question's direct answer. */
	lkpi_printf(ULOG_INFO_LEVEL,
	    "rtw88_diag: rate desc=0x%02x fix=0x%02x\n",
	    (unsigned)rtwdev->dm_info.tx_rate,
	    (unsigned)rtwdev->dm_info.fix_rate);

	/* The station's own rate decision as the firmware reports it
	 * (fw.c:rtw_fw_ra_report_iter fills si->ra_report from the C2H RA
	 * report).  This is the uplink rate mac80211's stack does not pick:
	 * rtw88 leaves data frames to the firmware's rate adaptation
	 * (tx.c:rtw_tx_data_pkt_info_update sets use_rate only for the fix_rate
	 * arm), so this descriptor is the only in-band witness of what the card
	 * transmits at. */
	list_for_each_entry(sdata, &local->interfaces, list) {
		struct ieee80211_sta *sta;
		struct rtw_sta_info *si;

		if (sdata->vif.type != NL80211_IFTYPE_STATION)
			continue;
		sta = ieee80211_find_sta(&sdata->vif, sdata->vif.cfg.ap_addr);
		if (sta == NULL)
			continue;
		si = (struct rtw_sta_info *)sta->drv_priv;
		lkpi_printf(ULOG_INFO_LEVEL,
		    "rtw88_diag: sta txrate desc=0x%02x bitrate=%u mcs=%u "
		    "nss=%u flags=0x%x rate_id=%u bw=%u\n",
		    (unsigned)si->ra_report.desc_rate,
		    (unsigned)si->ra_report.bit_rate,
		    (unsigned)si->ra_report.txrate.mcs,
		    (unsigned)si->ra_report.txrate.nss,
		    (unsigned)si->ra_report.txrate.flags,
		    (unsigned)si->rate_id, (unsigned)si->bw_mode);
		break;
	}

	/* The PHY's digital metrics: DIG (dynamic initial gain) is the loop
	 * that keeps the receive gain usable after a transmission; it runs from
	 * the watchdog (main.c:rtw_watch_dog_work).  A frozen DM shows here as
	 * a zero igi_bitmap and an all-zero history while the link is up. */
	lkpi_printf(ULOG_INFO_LEVEL,
	    "rtw88_diag: dm igi_bitmap=0x%02x igi=%u,%u,%u,%u min_rssi=%u "
	    "damping=%u/%u pwr_trk=%u delta_pi=%d,%d\n",
	    (unsigned)rtwdev->dm_info.igi_bitmap,
	    (unsigned)rtwdev->dm_info.igi_history[0],
	    (unsigned)rtwdev->dm_info.igi_history[1],
	    (unsigned)rtwdev->dm_info.igi_history[2],
	    (unsigned)rtwdev->dm_info.igi_history[3],
	    (unsigned)rtwdev->dm_info.min_rssi,
	    rtwdev->dm_info.damping ? 1U : 0U,
	    (unsigned)rtwdev->dm_info.damping_cnt,
	    rtwdev->dm_info.pwr_trk_triggered ? 1U : 0U,
	    (int)rtwdev->dm_info.delta_power_index[0],
	    (int)rtwdev->dm_info.delta_power_index[1]);

	lkpi_printf(ULOG_INFO_LEVEL,
	    "rtw88_diag: txrpt total=%u v0=%u v1=%u sn_cur=%d qlen=%u "
	    "q_empty=%u sn_match=%u alt_match=%u seam=off\n",
	    diag_txrpt_calls[0] + diag_txrpt_calls[1], diag_txrpt_calls[0],
	    diag_txrpt_calls[1], (int)atomic_read(&rtwdev->tx_report.sn),
	    (unsigned)skb_queue_len(&rtwdev->tx_report.queue),
	    diag_txrpt_q_empty, diag_txrpt_sn_match, diag_txrpt_alt_match);
}


/*
 * HT/A-MSDU caps and the A-MSDU decode accounting (2026-11-14, C6T-2/3).
 *
 * The AP's downlink to the RTOS station is 0 while the same AP's station
 * record says tx_amsdu_enable=1.  mac80211's ieee80211_rx_h_amsdu decodes an
 * A-MSDU only when the AP's HT cap has IEEE80211_HT_CAP_MAX_AMSDU, and the AP
 * builds one only when the station's HT cap has it.  Log both caps and count
 * the decode calls; the wrapper is a read-only observation
 * (--wrap=ieee80211_amsdu_to_8023s), not a fix.
 */
static unsigned int diag_amsdu_calls;

void __real_ieee80211_amsdu_to_8023s(struct sk_buff *skb,
				     struct sk_buff_head *list,
				     const u8 *addr,
				     enum nl80211_iftype iftype,
				     const unsigned int extra_headroom,
				     const u8 *check_da, const u8 *check_sa,
				     u8 mesh_control);

void
__wrap_ieee80211_amsdu_to_8023s(struct sk_buff *skb, struct sk_buff_head *list,
				const u8 *addr, enum nl80211_iftype iftype,
				const unsigned int extra_headroom,
				const u8 *check_da, const u8 *check_sa,
				u8 mesh_control)
{
	diag_amsdu_calls++;
	if (diag_amsdu_calls <= 20U)
		lkpi_printf(ULOG_INFO_LEVEL,
		    "rtw88_diag: amsdu #%u len=%u iftype=%d\n",
		    diag_amsdu_calls, skb != NULL ? skb->len : 0U,
		    (int)iftype);
	__real_ieee80211_amsdu_to_8023s(skb, list, addr, iftype,
					extra_headroom, check_da, check_sa,
					mesh_control);
	if (diag_amsdu_calls <= 20U)
		lkpi_printf(ULOG_INFO_LEVEL,
		    "rtw88_diag: amsdu #%u subframes=%u\n",
		    diag_amsdu_calls, skb_queue_len(list));
}

static void
rtw88_diag_amsdu_caps(struct rtw_dev *rtwdev, struct ieee80211_hw *hw)
{
	struct ieee80211_local *local;
	struct ieee80211_supported_band *sband;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_sta *ap = NULL;
	static const u8 bssid[ETH_ALEN] = {0x1c, 0xa7, 0x70, 0xde, 0x29, 0xa1};
	u16 bcap = 0, acap = 0;

	(void)rtwdev;
	if (hw == NULL || hw->wiphy == NULL)
		return;
	local = (struct ieee80211_local *)wiphy_priv(hw->wiphy);
	if (local == NULL)
		return;
	sband = hw->wiphy->bands[NL80211_BAND_2GHZ];
	if (sband != NULL && sband->ht_cap.ht_supported)
		bcap = sband->ht_cap.cap;
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (sdata->vif.type != NL80211_IFTYPE_STATION)
			continue;
		ap = ieee80211_find_sta(&sdata->vif, bssid);
		if (ap != NULL) {
			acap = ap->deflink.ht_cap.cap;
			break;
		}
	}
	lkpi_printf(ULOG_INFO_LEVEL,
	    "rtw88_diag: amsdu_caps board_ht=0x%04x board_maxamsdu=%u "
	    "ap_ht=0x%04x ap_maxamsdu=%u calls=%u\n",
	    bcap, !!(bcap & IEEE80211_HT_CAP_MAX_AMSDU),
	    acap, !!(acap & IEEE80211_HT_CAP_MAX_AMSDU), diag_amsdu_calls);
}
