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
 *         Not a delivery feature: only the diagnostic profile/case calls it.
 *         `rtw88_diag_txpower(wdev->wiphy)` is the whole interface; the type is
 *         void* so the stack-world case does not include the driver headers.
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
#include "linux80211/c2h_inline.h"

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

	/* Name the driver's C2H work for the inline seam (read-only: the
	 * function pointer is the driver's own; the seam runs it inline so its
	 * rate reports land inside the purge window). */
	lkpi_c2h_inline_register(rtwdev->c2h_work.func);

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
 * and whether their sn matches the driver's queue.  Counting the raw C2H
 * packets and the report handler's sn/status answers it without touching the
 * driver bytes.  The raw-packet line is capped (the BT/coex C2H can be
 * chatty); the report lines are few and all printed.
 */
static unsigned int diag_c2h_pkts;
static unsigned int diag_txrpt_calls[2];

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
	u8 sn = 0, st = 0;
	unsigned int idx = (src == C2H_CCX_TX_RPT) ? 1U : 0U;

	diag_txrpt_calls[idx]++;
	c2h = get_c2h_from_skb(skb);
	if (src == C2H_CCX_TX_RPT) {
		sn = GET_CCX_REPORT_SEQNUM_V1(c2h->payload);
		st = GET_CCX_REPORT_STATUS_V1(c2h->payload);
	} else {
		sn = GET_CCX_REPORT_SEQNUM_V0(c2h->payload);
		st = GET_CCX_REPORT_STATUS_V0(c2h->payload);
	}
	{
		/* The report's sn and the queue's pending sns in one line: a
		 * mismatch is the purge.  payload[0..11] locates the real sn
		 * byte when the driver's V0 offset does not. */
		unsigned char qsn[8];
		struct sk_buff *cur;
		int qn = 0, i;

		skb_queue_walk(&rtwdev->tx_report.queue, cur) {
			if (qn >= (int)sizeof(qsn))
				break;
			qsn[qn++] = *(u8 *)IEEE80211_SKB_CB(cur)
				->status.status_driver_data;
		}
		lkpi_printf(ULOG_INFO_LEVEL,
		    "rtw88_diag: txrpt #%u src=%d sn=0x%02x st=0x%02x "
		    "v1=%u v0=%u qn=%d qsn=",
		    diag_txrpt_calls[idx], src, sn, st,
		    diag_txrpt_calls[1], diag_txrpt_calls[0], qn);
		for (i = 0; i < qn; i++)
			lkpi_printf(ULOG_INFO_LEVEL, "%s0x%02x",
				    i ? "," : "", qsn[i]);
		lkpi_printf(ULOG_INFO_LEVEL, " payload=");
		for (i = 0; i < 12; i++)
			lkpi_printf(ULOG_INFO_LEVEL, "%s%02x",
				    i ? "," : "", c2h->payload[i]);
		lkpi_printf(ULOG_INFO_LEVEL, "\n");
	}
	__real_rtw_tx_report_handle(rtwdev, skb, src);
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
