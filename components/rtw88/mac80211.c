// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include "main.h"
#include "sec.h"
#include "tx.h"
#include "fw.h"
#include "mac.h"
#include "coex.h"
#include "ps.h"
#include "reg.h"
#include "bf.h"
#include "debug.h"

/* [S9 D-3] console flood gate for the per-probe forensic prints
 * (default off; the A/B detune round drowned the console without it) */
int rtw_d3_verbose;

void rtw_d3_verbose_set(int on)
{
	rtw_d3_verbose = on;
}
#include "wow.h"
#include "sar.h"

static void rtw_ops_tx(struct ieee80211_hw *hw,
		       struct ieee80211_tx_control *control,
		       struct sk_buff *skb)
{
	struct rtw_dev *rtwdev = hw->priv;

	if (!test_bit(RTW_FLAG_RUNNING, rtwdev->flags)) {
		ieee80211_free_txskb(hw, skb);
		return;
	}

	/* [S9 D-3] board forensics: mgmt TX fires against the chip's live
	 * channel view - an auth sent on a stale channel never gets ACKed
	 * (sn=60 st=64 retries-exhausted, board 2026-10-04).  Gated behind
	 * rtw_d3_verbose: the per-probe flood drowned the console during
	 * the detune A/B (board 2026-10-05). */
	if (rtw_d3_verbose &&
	    ieee80211_is_mgmt(((struct ieee80211_hdr *)skb->data)->frame_control))
		printf("rtw88u: ops_tx mgmt len %u chan %u prio %u scan %u\n",
		    (unsigned)skb->len, rtwdev->hal.current_channel,
		    skb->priority,
		    (unsigned)test_bit(RTW_FLAG_SCANNING, rtwdev->flags));

	/* [S9 D-3] the first auth TX after boot snapshots the WMAC state
	 * inline (shell-independent) - the E4 auth-point register set. */
	{
		static int d3_regs_auth_done;

		if (!d3_regs_auth_done &&
		    ieee80211_is_mgmt(((struct ieee80211_hdr *)skb->data)->frame_control) &&
		    ((((struct ieee80211_hdr *)skb->data)->frame_control) & 0xf0) == 0xb0) {
			d3_regs_auth_done = 1;
			rtw_d3_regs_dump();
		}
	}

	rtw_tx(rtwdev, control, skb);
}

static void rtw_ops_wake_tx_queue(struct ieee80211_hw *hw,
				  struct ieee80211_txq *txq)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_txq *rtwtxq = (struct rtw_txq *)txq->drv_priv;

	if (!test_bit(RTW_FLAG_RUNNING, rtwdev->flags))
		return;

	spin_lock_bh(&rtwdev->txq_lock);
	if (list_empty(&rtwtxq->list))
		list_add_tail(&rtwtxq->list, &rtwdev->txqs);
	spin_unlock_bh(&rtwdev->txq_lock);

	/* ensure to dequeue EAPOL (4/4) at the right time */
	if (txq->ac == IEEE80211_AC_VO)
		__rtw_tx_work(rtwdev);
	else
		queue_work(rtwdev->tx_wq, &rtwdev->tx_work);
}

/* D-3 diagnostics (DIVERGENCES.md D-3, removal = 单播 RX 断链结案):
 * the quiet/RCR helper has no hw handle from the shell face; stash the
 * one instance this stack runs. */
static struct rtw_dev *d3_quiet_rtwdev;

void rtw_d3_rcr_quiet(int on)
{
	struct rtw_dev *rtwdev = d3_quiet_rtwdev;
	u32 before;

	if (rtwdev == NULL) {
		printf("rtw88u: d3 quiet: driver not started\n");
		return;
	}

	mutex_lock(&rtwdev->mutex);
	before = rtwdev->hal.rcr;
	if (on)
		rtwdev->hal.rcr &= ~(BIT_AM | BIT_AB);
	else
		rtwdev->hal.rcr |= (BIT_AM | BIT_AB);
	rtw_write32(rtwdev, REG_RCR, rtwdev->hal.rcr);
	printf("rtw88u: d3 quiet %s rcr %08x -> %08x (rb %08x)\n",
	    on ? "ON" : "OFF", before, rtwdev->hal.rcr,
	    rtw_read32(rtwdev, REG_RCR));
	mutex_unlock(&rtwdev->mutex);
}

void rtw_d3_rcr_promisc(int on)
{
	struct rtw_dev *rtwdev = d3_quiet_rtwdev;
	u32 before;

	if (rtwdev == NULL) {
		printf("rtw88u: d3 promisc: driver not started\n");
		return;
	}

	mutex_lock(&rtwdev->mutex);
	before = rtwdev->hal.rcr;
	if (on)
		rtwdev->hal.rcr |= BIT_AAP;
	else
		rtwdev->hal.rcr &= ~BIT_AAP;
	rtw_write32(rtwdev, REG_RCR, rtwdev->hal.rcr);
	printf("rtw88u: d3 promisc %s rcr %08x -> %08x (rb %08x)\n",
	    on ? "ON" : "OFF", before, rtwdev->hal.rcr,
	    rtw_read32(rtwdev, REG_RCR));
	mutex_unlock(&rtwdev->mutex);
}

/* [S9 D-3] WMAC state snapshot at the auth moment: filters, port
 * address/BSSID registers, TX gates, security config.  The dongle's
 * registers are only reachable through the driver; the GT lane dumps
 * the same list from a patched out-of-tree module for the offline
 * diff.  All reads are MAC config regs the vendor driver reads too
 * (REG_SECCFG = 0x0680 on 8821c, not the 0x150 of older chips). */
void rtw_d3_regs_dump(void)
{
	struct rtw_dev *rtwdev = d3_quiet_rtwdev;

	if (rtwdev == NULL) {
		printf("rtw88u: d3 regs: driver not started\n");
		return;
	}
	printf("rtw88u: d3 regs CR=%08x fwtcq=%08x tcr=%08x rcr=%08x\n",
	    rtw_read32(rtwdev, REG_CR),
	    rtw_read32(rtwdev, REG_FWHW_TXQ_CTRL),
	    rtw_read32(rtwdev, REG_TCR),
	    rtw_read32(rtwdev, REG_RCR));
	printf("rtw88u: d3 regs rrsr=%08x port0mac=%08x_%08x "
	    "port0bssid=%08x_%08x\n",
	    rtw_read32(rtwdev, REG_RRSR),
	    rtw_read32(rtwdev, 0x0610), rtw_read32(rtwdev, 0x0614),
	    rtw_read32(rtwdev, 0x0618), rtw_read32(rtwdev, 0x061C));
	printf("rtw88u: d3 regs flt0=%04x flt1=%04x flt2=%04x "
	    "seccfg=%08x\n",
	    rtw_read16(rtwdev, REG_RXFLTMAP0),
	    rtw_read16(rtwdev, REG_RXFLTMAP1),
	    rtw_read16(rtwdev, REG_RXFLTMAP2),
	    rtw_read32(rtwdev, 0x0680)); /* REG_SECCFG (vendor hal_com_reg.h:477; not in rtw88 reg.h) */
	/* [S9 D-3] second shell of WMAC state (codex E4 expansion): FW
	 * packet parsing / status filter, RPFM payload maps, CAM ports,
	 * WMAC validation options, WSEC option.  Widths follow the
	 * vendor register map (0x601/0x602/0x7da are sub-32-bit). */
	printf("rtw88u: d3 regs fwpkt=%02x fwstsfilt=%02x rpfm=%04x_%04x "
	    "camcmd=%08x camdata=%08x\n",
	    rtw_read8(rtwdev, 0x0601), rtw_read8(rtwdev, 0x0602),
	    rtw_read16(rtwdev, 0x0644), rtw_read16(rtwdev, 0x0646),
	    rtw_read32(rtwdev, 0x0648), rtw_read32(rtwdev, 0x064C));
	printf("rtw88u: d3 regs wmac7d0=%08x 7d4=%08x 7d8=%08x "
	    "rxfn=%04x wsec=%08x\n",
	    rtw_read32(rtwdev, 0x07D0), rtw_read32(rtwdev, 0x07D4),
	    rtw_read32(rtwdev, 0x07D8), rtw_read16(rtwdev, 0x07DA),
	    rtw_read32(rtwdev, 0x07EC));
}

/* [S9 D-3] comparator detuning A/B (codex rank-1 discriminator): point
 * the port address comparator at M' while the bridge keeps transmitting
 * SA=M.  Responses addressed to M then reach USB only via AAP - if the
 * exact da_us counter ticks with the detuned comparator and not with
 * the matched one, receive is local-match-dependent (H6).
 * STICKY mode: scan_start/complete rewrite the comparator
 * (PORT_SET_MAC_ADDR), so the detune re-applies itself after every one
 * of them (rtw_d3_cmp_reapply, called from core_scan_start/complete). */
static u8 d3_cmp_save[6];
static u8 d3_cmp_m2[6];
static int rtw_d3_cmp_sticky;

static void rtw_d3_cmp_write_locked(const u8 *m)
{
	int i;

	for (i = 0; i < 6; i++)
		rtw_write8(d3_quiet_rtwdev, 0x0610 + i, m[i]);
}

void rtw_d3_cmp_detune(const u8 *m2)
{
	struct rtw_dev *rtwdev = d3_quiet_rtwdev;
	int i;

	if (rtwdev == NULL) {
		printf("rtw88u: d3 cmp: driver not started\n");
		return;
	}
	mutex_lock(&rtwdev->mutex);
	for (i = 0; i < 6; i++) {
		d3_cmp_save[i] = rtw_read8(rtwdev, 0x0610 + i);
		d3_cmp_m2[i] = m2[i];
	}
	rtw_d3_cmp_write_locked(m2);
	rtw_d3_cmp_sticky = 1;
	printf("rtw88u: d3 cmp detune(sticky) M=%02x:%02x:%02x:%02x:%02x:%02x -> "
	    "M'=%02x:%02x:%02x:%02x:%02x:%02x rb %02x:%02x:%02x:%02x:%02x:%02x\n",
	    d3_cmp_save[0], d3_cmp_save[1], d3_cmp_save[2],
	    d3_cmp_save[3], d3_cmp_save[4], d3_cmp_save[5],
	    m2[0], m2[1], m2[2], m2[3], m2[4], m2[5],
	    rtw_read8(rtwdev, 0x0610), rtw_read8(rtwdev, 0x0611),
	    rtw_read8(rtwdev, 0x0612), rtw_read8(rtwdev, 0x0613),
	    rtw_read8(rtwdev, 0x0614), rtw_read8(rtwdev, 0x0615));
	mutex_unlock(&rtwdev->mutex);
}

void rtw_d3_cmp_restore(void)
{
	struct rtw_dev *rtwdev = d3_quiet_rtwdev;
	int i;

	if (rtwdev == NULL) {
		printf("rtw88u: d3 cmp: driver not started\n");
		return;
	}
	mutex_lock(&rtwdev->mutex);
	rtw_d3_cmp_sticky = 0;
	for (i = 0; i < 6; i++)
		rtw_write8(rtwdev, 0x0610 + i, d3_cmp_save[i]);
	printf("rtw88u: d3 cmp restore rb %02x:%02x:%02x:%02x:%02x:%02x\n",
	    rtw_read8(rtwdev, 0x0610), rtw_read8(rtwdev, 0x0611),
	    rtw_read8(rtwdev, 0x0612), rtw_read8(rtwdev, 0x0613),
	    rtw_read8(rtwdev, 0x0614), rtw_read8(rtwdev, 0x0615));
	mutex_unlock(&rtwdev->mutex);
}

void rtw_d3_cmp_reapply(void)
{
	struct rtw_dev *rtwdev = d3_quiet_rtwdev;

	if (rtw_d3_cmp_sticky && rtwdev != NULL) {
		mutex_lock(&rtwdev->mutex);
		rtw_d3_cmp_write_locked(d3_cmp_m2);
		mutex_unlock(&rtwdev->mutex);
	}
}

static int rtw_ops_start(struct ieee80211_hw *hw)
{
	struct rtw_dev *rtwdev = hw->priv;
	int ret;

	d3_quiet_rtwdev = rtwdev;

	mutex_lock(&rtwdev->mutex);
	ret = rtw_core_start(rtwdev);
	mutex_unlock(&rtwdev->mutex);

	return ret;
}

static void rtw_ops_stop(struct ieee80211_hw *hw, bool suspend)
{
	struct rtw_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	rtw_core_stop(rtwdev);
	mutex_unlock(&rtwdev->mutex);
}

static int rtw_ops_config(struct ieee80211_hw *hw, int radio_idx, u32 changed)
{
	struct rtw_dev *rtwdev = hw->priv;
	int ret = 0;

	/* let previous ips work finish to ensure we don't leave ips twice */
	cancel_work_sync(&rtwdev->ips_work);

	mutex_lock(&rtwdev->mutex);

	rtw_leave_lps_deep(rtwdev);

	if ((changed & IEEE80211_CONF_CHANGE_IDLE) &&
	    !(hw->conf.flags & IEEE80211_CONF_IDLE)) {
		ret = rtw_leave_ips(rtwdev);
		if (ret) {
			rtw_err(rtwdev, "failed to leave idle state\n");
			goto out;
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL)
		rtw_set_channel(rtwdev);

	if ((changed & IEEE80211_CONF_CHANGE_IDLE) &&
	    (hw->conf.flags & IEEE80211_CONF_IDLE) &&
	    !test_bit(RTW_FLAG_SCANNING, rtwdev->flags))
		rtw_enter_ips(rtwdev);

out:
	mutex_unlock(&rtwdev->mutex);
	return ret;
}

static const struct rtw_vif_port rtw_vif_port[] = {
	[0] = {
		.mac_addr	= {.addr = 0x0610},
		.bssid		= {.addr = 0x0618},
		.net_type	= {.addr = 0x0100, .mask = 0x30000},
		.aid		= {.addr = 0x06a8, .mask = 0x7ff},
		.bcn_ctrl	= {.addr = 0x0550, .mask = 0xff},
	},
	[1] = {
		.mac_addr	= {.addr = 0x0700},
		.bssid		= {.addr = 0x0708},
		.net_type	= {.addr = 0x0100, .mask = 0xc0000},
		.aid		= {.addr = 0x0710, .mask = 0x7ff},
		.bcn_ctrl	= {.addr = 0x0551, .mask = 0xff},
	},
	[2] = {
		.mac_addr	= {.addr = 0x1620},
		.bssid		= {.addr = 0x1628},
		.net_type	= {.addr = 0x1100, .mask = 0x3},
		.aid		= {.addr = 0x1600, .mask = 0x7ff},
		.bcn_ctrl	= {.addr = 0x0578, .mask = 0xff},
	},
	[3] = {
		.mac_addr	= {.addr = 0x1630},
		.bssid		= {.addr = 0x1638},
		.net_type	= {.addr = 0x1100, .mask = 0xc},
		.aid		= {.addr = 0x1604, .mask = 0x7ff},
		.bcn_ctrl	= {.addr = 0x0579, .mask = 0xff},
	},
	[4] = {
		.mac_addr	= {.addr = 0x1640},
		.bssid		= {.addr = 0x1648},
		.net_type	= {.addr = 0x1100, .mask = 0x30},
		.aid		= {.addr = 0x1608, .mask = 0x7ff},
		.bcn_ctrl	= {.addr = 0x057a, .mask = 0xff},
	},
};

static int rtw_ops_add_interface(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;
	enum rtw_net_type net_type;
	u32 config = 0;
	u8 port;
	u8 bcn_ctrl = 0;

	if (rtw_fw_feature_check(&rtwdev->fw, FW_FEATURE_BCN_FILTER))
		vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER |
				     IEEE80211_VIF_SUPPORTS_CQM_RSSI;
	rtwvif->stats.tx_unicast = 0;
	rtwvif->stats.rx_unicast = 0;
	rtwvif->stats.tx_cnt = 0;
	rtwvif->stats.rx_cnt = 0;
	rtwvif->scan_req = NULL;
	memset(&rtwvif->bfee, 0, sizeof(struct rtw_bfee));
	rtw_txq_init(rtwdev, vif->txq);
	INIT_LIST_HEAD(&rtwvif->rsvd_page_list);

	mutex_lock(&rtwdev->mutex);

	rtwvif->mac_id = rtw_acquire_macid(rtwdev);
	if (rtwvif->mac_id >= RTW_MAX_MAC_ID_NUM) {
		mutex_unlock(&rtwdev->mutex);
		return -ENOSPC;
	}

	port = find_first_zero_bit(rtwdev->hw_port, RTW_PORT_NUM);
	if (port >= RTW_PORT_NUM) {
		mutex_unlock(&rtwdev->mutex);
		return -EINVAL;
	}
	set_bit(port, rtwdev->hw_port);

	rtwvif->port = port;
	rtwvif->conf = &rtw_vif_port[port];
	rtw_leave_lps_deep(rtwdev);

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
		rtw_add_rsvd_page_bcn(rtwdev, rtwvif);
		net_type = RTW_NET_AP_MODE;
		bcn_ctrl = BIT_EN_BCN_FUNCTION | BIT_DIS_TSF_UDT;
		break;
	case NL80211_IFTYPE_ADHOC:
		rtw_add_rsvd_page_bcn(rtwdev, rtwvif);
		net_type = RTW_NET_AD_HOC;
		bcn_ctrl = BIT_EN_BCN_FUNCTION | BIT_DIS_TSF_UDT;
		break;
	case NL80211_IFTYPE_STATION:
		rtw_add_rsvd_page_sta(rtwdev, rtwvif);
		net_type = RTW_NET_NO_LINK;
		bcn_ctrl = BIT_EN_BCN_FUNCTION;
		break;
	default:
		WARN_ON(1);
		clear_bit(rtwvif->port, rtwdev->hw_port);
		mutex_unlock(&rtwdev->mutex);
		return -EINVAL;
	}

	ether_addr_copy(rtwvif->mac_addr, vif->addr);
	config |= PORT_SET_MAC_ADDR;
	rtwvif->net_type = net_type;
	config |= PORT_SET_NET_TYPE;
	rtwvif->bcn_ctrl = bcn_ctrl;
	config |= PORT_SET_BCN_CTRL;
	rtw_vif_port_config(rtwdev, rtwvif, config);
	rtw_core_port_switch(rtwdev, vif);
	rtw_recalc_lps(rtwdev, vif);

	/* [S9 D-3] unicast RX dies at the chip filter unless the port
	 * MACID (RCR BIT_APM comparator) actually landed: read the port
	 * address back plus the live RCR / mgmt filter maps. */
	printf("rtw88u: port %u macid rb %02x:%02x:%02x:%02x:%02x:%02x "
	    "rcr %08x flt0 %04x flt1 %04x flt2 %04x\n",
	    rtwvif->port,
	    rtw_read8(rtwdev, 0x0610), rtw_read8(rtwdev, 0x0611),
	    rtw_read8(rtwdev, 0x0612), rtw_read8(rtwdev, 0x0613),
	    rtw_read8(rtwdev, 0x0614), rtw_read8(rtwdev, 0x0615),
	    rtw_read32(rtwdev, REG_RCR),
	    rtw_read16(rtwdev, REG_RXFLTMAP0),
	    rtw_read16(rtwdev, REG_RXFLTMAP1),
	    rtw_read16(rtwdev, REG_RXFLTMAP2));
	rtw_d3_set_da(rtwvif->mac_addr);

	mutex_unlock(&rtwdev->mutex);

	rtw_dbg(rtwdev, RTW_DBG_STATE, "start vif %pM mac_id %d on port %d\n",
		vif->addr, rtwvif->mac_id, rtwvif->port);
	return 0;
}

static void rtw_ops_remove_interface(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;
	u32 config = 0;

	rtw_dbg(rtwdev, RTW_DBG_STATE, "stop vif %pM mac_id %d on port %d\n",
		vif->addr, rtwvif->mac_id, rtwvif->port);

	mutex_lock(&rtwdev->mutex);

	rtw_leave_lps_deep(rtwdev);

	rtw_txq_cleanup(rtwdev, vif->txq);
	rtw_remove_rsvd_page(rtwdev, rtwvif);

	eth_zero_addr(rtwvif->mac_addr);
	config |= PORT_SET_MAC_ADDR;
	rtwvif->net_type = RTW_NET_NO_LINK;
	config |= PORT_SET_NET_TYPE;
	rtwvif->bcn_ctrl = 0;
	config |= PORT_SET_BCN_CTRL;
	rtw_vif_port_config(rtwdev, rtwvif, config);
	clear_bit(rtwvif->port, rtwdev->hw_port);
	rtw_release_macid(rtwdev, rtwvif->mac_id);
	rtw_recalc_lps(rtwdev, NULL);

	mutex_unlock(&rtwdev->mutex);
}

static int rtw_ops_change_interface(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    enum nl80211_iftype type, bool p2p)
{
	struct rtw_dev *rtwdev = hw->priv;

	rtw_dbg(rtwdev, RTW_DBG_STATE, "change vif %pM (%d)->(%d), p2p (%d)->(%d)\n",
		vif->addr, vif->type, type, vif->p2p, p2p);

	rtw_ops_remove_interface(hw, vif);

	vif->type = type;
	vif->p2p = p2p;

	return rtw_ops_add_interface(hw, vif);
}

static void rtw_ops_configure_filter(struct ieee80211_hw *hw,
				     unsigned int changed_flags,
				     unsigned int *new_flags,
				     u64 multicast)
{
	struct rtw_dev *rtwdev = hw->priv;

	*new_flags &= FIF_ALLMULTI | FIF_OTHER_BSS | FIF_FCSFAIL |
		      FIF_BCN_PRBRESP_PROMISC | FIF_CONTROL;

	mutex_lock(&rtwdev->mutex);

	rtw_leave_lps_deep(rtwdev);

	if (changed_flags & FIF_CONTROL) {
		if (*new_flags & FIF_CONTROL)
			rtw_write16(rtwdev, REG_RXFLTMAP1, 0xffff);
		else
			rtw_write16(rtwdev, REG_RXFLTMAP1, rtwdev->hal.rxfltmap1);
	}
	if (changed_flags & FIF_ALLMULTI) {
		if (*new_flags & FIF_ALLMULTI)
			rtwdev->hal.rcr |= BIT_AM;
		else
			rtwdev->hal.rcr &= ~(BIT_AM);
	}
	if (changed_flags & FIF_FCSFAIL) {
		if (*new_flags & FIF_FCSFAIL)
			rtwdev->hal.rcr |= BIT_ACRC32;
		else
			rtwdev->hal.rcr &= ~(BIT_ACRC32);
	}
	if (changed_flags & FIF_OTHER_BSS) {
		if (*new_flags & FIF_OTHER_BSS)
			rtwdev->hal.rcr |= BIT_AAP;
		else
			rtwdev->hal.rcr &= ~(BIT_AAP);
	}
	if (changed_flags & FIF_BCN_PRBRESP_PROMISC) {
		if (*new_flags & FIF_BCN_PRBRESP_PROMISC)
			rtwdev->hal.rcr &= ~(BIT_CBSSID_BCN | BIT_CBSSID_DATA);
		else
			rtwdev->hal.rcr |= BIT_CBSSID_BCN;
	}

	rtw_dbg(rtwdev, RTW_DBG_RX,
		"config rx filter, changed=0x%08x, new=0x%08x, rcr=0x%08x\n",
		changed_flags, *new_flags, rtwdev->hal.rcr);

	rtw_write32(rtwdev, REG_RCR, rtwdev->hal.rcr);

	mutex_unlock(&rtwdev->mutex);
}

/* Only have one group of EDCA parameters now */
static const u32 ac_to_edca_param[IEEE80211_NUM_ACS] = {
	[IEEE80211_AC_VO] = REG_EDCA_VO_PARAM,
	[IEEE80211_AC_VI] = REG_EDCA_VI_PARAM,
	[IEEE80211_AC_BE] = REG_EDCA_BE_PARAM,
	[IEEE80211_AC_BK] = REG_EDCA_BK_PARAM,
};

static u8 rtw_aifsn_to_aifs(struct rtw_dev *rtwdev,
			    struct rtw_vif *rtwvif, u8 aifsn)
{
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);
	u8 slot_time;
	u8 sifs;

	slot_time = vif->bss_conf.use_short_slot ? 9 : 20;
	sifs = rtwdev->hal.current_band_type == RTW_BAND_5G ? 16 : 10;

	return aifsn * slot_time + sifs;
}

static void __rtw_conf_tx(struct rtw_dev *rtwdev,
			  struct rtw_vif *rtwvif, u16 ac)
{
	struct ieee80211_tx_queue_params *params = &rtwvif->tx_params[ac];
	u32 edca_param = ac_to_edca_param[ac];
	u8 ecw_max, ecw_min;
	u8 aifs;

	/* 2^ecw - 1 = cw; ecw = log2(cw + 1) */
	ecw_max = ilog2(params->cw_max + 1);
	ecw_min = ilog2(params->cw_min + 1);
	aifs = rtw_aifsn_to_aifs(rtwdev, rtwvif, params->aifs);
	rtw_write32_mask(rtwdev, edca_param, BIT_MASK_TXOP_LMT, params->txop);
	rtw_write32_mask(rtwdev, edca_param, BIT_MASK_CWMAX, ecw_max);
	rtw_write32_mask(rtwdev, edca_param, BIT_MASK_CWMIN, ecw_min);
	rtw_write32_mask(rtwdev, edca_param, BIT_MASK_AIFS, aifs);
}

static void rtw_conf_tx(struct rtw_dev *rtwdev,
			struct rtw_vif *rtwvif)
{
	u16 ac;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
		__rtw_conf_tx(rtwdev, rtwvif, ac);
}

static void rtw_ops_bss_info_changed(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *conf,
				     u64 changed)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	u32 config = 0;

	/* [S9 D-3] the unicast-RX filter chain read back live: port MACID
	 * (APM comparator), RCR, mgmt filter maps, and the BSSID regs the
	 * BSS_CHANGED_BSSID write below targets. */
	printf("rtw88u: bss_info enter changed=%llx macid %02x:%02x:%02x:%02x:%02x:%02x "
	    "bssid %02x:%02x:%02x:%02x:%02x:%02x rcr %08x flt %04x/%04x/%04x\n",
	    changed,
	    rtw_read8(rtwdev, 0x0610), rtw_read8(rtwdev, 0x0611),
	    rtw_read8(rtwdev, 0x0612), rtw_read8(rtwdev, 0x0613),
	    rtw_read8(rtwdev, 0x0614), rtw_read8(rtwdev, 0x0615),
	    rtw_read8(rtwdev, 0x0618), rtw_read8(rtwdev, 0x0619),
	    rtw_read8(rtwdev, 0x061a), rtw_read8(rtwdev, 0x061b),
	    rtw_read8(rtwdev, 0x061c), rtw_read8(rtwdev, 0x061d),
	    rtw_read32(rtwdev, REG_RCR),
	    rtw_read16(rtwdev, REG_RXFLTMAP0),
	    rtw_read16(rtwdev, REG_RXFLTMAP1),
	    rtw_read16(rtwdev, REG_RXFLTMAP2));
	mutex_lock(&rtwdev->mutex);

	rtw_leave_lps_deep(rtwdev);

	if (changed & BSS_CHANGED_ASSOC) {
		rtw_vif_assoc_changed(rtwvif, conf);
		if (vif->cfg.assoc) {
			rtw_coex_connect_notify(rtwdev, COEX_ASSOCIATE_FINISH);

			rtw_fw_download_rsvd_page(rtwdev);
			rtw_send_rsvd_page_h2c(rtwdev);
			rtw_fw_default_port(rtwdev, rtwvif);
			rtw_coex_media_status_notify(rtwdev, vif->cfg.assoc);
			if (rtw_bf_support)
				rtw_bf_assoc(rtwdev, vif, conf);

			rtw_set_ampdu_factor(rtwdev, vif, conf);

			rtw_fw_beacon_filter_config(rtwdev, true, vif);
		} else {
			rtw_leave_lps(rtwdev);
			rtw_bf_disassoc(rtwdev, vif, conf);
			/* Abort ongoing scan if cancel_scan isn't issued
			 * when disconnected by peer
			 */
			if (test_bit(RTW_FLAG_SCANNING, rtwdev->flags))
				rtw_hw_scan_abort(rtwdev);

		}

		config |= PORT_SET_NET_TYPE;
		config |= PORT_SET_AID;
	}

	if (changed & BSS_CHANGED_BSSID) {
		ether_addr_copy(rtwvif->bssid, conf->bssid);
		config |= PORT_SET_BSSID;
		if (!rtw_core_check_sta_active(rtwdev))
			rtw_clear_op_chan(rtwdev);
		else
			rtw_store_op_chan(rtwdev, true);
	}

	if (changed & BSS_CHANGED_BEACON_INT) {
		if (ieee80211_vif_type_p2p(vif) == NL80211_IFTYPE_STATION)
			coex_stat->wl_beacon_interval = conf->beacon_int;
	}

	if (changed & BSS_CHANGED_BEACON) {
		rtw_set_dtim_period(rtwdev, conf->dtim_period);
		rtw_fw_download_rsvd_page(rtwdev);
		rtw_send_rsvd_page_h2c(rtwdev);
	}

	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		if (conf->enable_beacon)
			rtw_write32_set(rtwdev, REG_FWHW_TXQ_CTRL,
					BIT_EN_BCNQ_DL);
		else
			rtw_write32_clr(rtwdev, REG_FWHW_TXQ_CTRL,
					BIT_EN_BCNQ_DL);
	}
	if (changed & BSS_CHANGED_CQM)
		rtw_fw_beacon_filter_config(rtwdev, true, vif);

	if (changed & BSS_CHANGED_MU_GROUPS)
		rtw_chip_set_gid_table(rtwdev, vif, conf);

	if (changed & BSS_CHANGED_ERP_SLOT)
		rtw_conf_tx(rtwdev, rtwvif);

	if (changed & BSS_CHANGED_PS)
		rtw_recalc_lps(rtwdev, NULL);

	rtw_vif_port_config(rtwdev, rtwvif, config);

	/* [S9 D-3] the BSSID branch ran PORT_SET_BSSID through the port
	 * config above: read the port BSSID registers back so "did the AP
	 * BSSID actually land" is answered live, not inferred. */
	if (changed & BSS_CHANGED_BSSID)
		printf("rtw88u: bss_info exit changed=%llx bssid rb "
		    "%08x_%08x (port %u)\n", changed,
		    rtw_read32(rtwdev, 0x0618), rtw_read32(rtwdev, 0x061C),
		    rtwvif->port);

	mutex_unlock(&rtwdev->mutex);
}

static int rtw_ops_start_ap(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf)
{
	struct rtw_dev *rtwdev = hw->priv;
	const struct rtw_chip_info *chip = rtwdev->chip;

	mutex_lock(&rtwdev->mutex);
	rtw_write32_set(rtwdev, REG_TCR, BIT_TCR_UPDATE_HGQMD);
	rtwdev->ap_active = true;
	rtw_store_op_chan(rtwdev, true);
	chip->ops->phy_calibration(rtwdev);
	mutex_unlock(&rtwdev->mutex);

	return 0;
}

static void rtw_ops_stop_ap(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf)
{
	struct rtw_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	rtw_write32_clr(rtwdev, REG_TCR, BIT_TCR_UPDATE_HGQMD);
	rtwdev->ap_active = false;
	if (!rtw_core_check_sta_active(rtwdev))
		rtw_clear_op_chan(rtwdev);
	mutex_unlock(&rtwdev->mutex);
}

static int rtw_ops_conf_tx(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif,
			   unsigned int link_id, u16 ac,
			   const struct ieee80211_tx_queue_params *params)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;

	mutex_lock(&rtwdev->mutex);

	rtw_leave_lps_deep(rtwdev);

	rtwvif->tx_params[ac] = *params;
	__rtw_conf_tx(rtwdev, rtwvif, ac);

	mutex_unlock(&rtwdev->mutex);

	return 0;
}

static int rtw_ops_sta_add(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta)
{
	struct rtw_dev *rtwdev = hw->priv;
	int ret = 0;

	mutex_lock(&rtwdev->mutex);
	ret = rtw_sta_add(rtwdev, sta, vif);
	mutex_unlock(&rtwdev->mutex);

	return ret;
}

static int rtw_ops_sta_remove(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta)
{
	struct rtw_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	rtw_fw_beacon_filter_config(rtwdev, false, vif);
	rtw_sta_remove(rtwdev, sta, true);
	mutex_unlock(&rtwdev->mutex);

	return 0;
}

static int rtw_ops_set_tim(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
			   bool set)
{
	struct rtw_dev *rtwdev = hw->priv;

	ieee80211_queue_work(hw, &rtwdev->update_beacon_work);

	return 0;
}

static int rtw_ops_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			   struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			   struct ieee80211_key_conf *key)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_sec_desc *sec = &rtwdev->sec;
	u8 hw_key_type;
	u8 hw_key_idx;
	int ret = 0;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		hw_key_type = RTW_CAM_WEP40;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		hw_key_type = RTW_CAM_WEP104;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		hw_key_type = RTW_CAM_TKIP;
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		hw_key_type = RTW_CAM_AES;
		key->flags |= IEEE80211_KEY_FLAG_SW_MGMT_TX;
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		/* suppress error messages */
		return -EOPNOTSUPP;
	default:
		return -ENOTSUPP;
	}

	mutex_lock(&rtwdev->mutex);

	rtw_leave_lps_deep(rtwdev);

	if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) {
		hw_key_idx = rtw_sec_get_free_cam(sec);
	} else {
		/* multiple interfaces? */
		hw_key_idx = key->keyidx;
	}

	if (hw_key_idx > sec->total_cam_num) {
		ret = -ENOSPC;
		goto out;
	}

	switch (cmd) {
	case SET_KEY:
		/* need sw generated IV */
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
		key->hw_key_idx = hw_key_idx;
		rtw_sec_write_cam(rtwdev, sec, sta, key,
				  hw_key_type, hw_key_idx);
		break;
	case DISABLE_KEY:
		rtw_hci_flush_all_queues(rtwdev, false);
		rtw_mac_flush_all_queues(rtwdev, false);
		rtw_sec_clear_cam(rtwdev, sec, key->hw_key_idx);
		break;
	}

	/* download new cam settings for PG to backup */
	if (rtw_get_lps_deep_mode(rtwdev) == LPS_DEEP_MODE_PG)
		rtw_fw_download_rsvd_page(rtwdev);

out:
	mutex_unlock(&rtwdev->mutex);

	return ret;
}

static int rtw_ops_ampdu_action(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_ampdu_params *params)
{
	struct ieee80211_sta *sta = params->sta;
	u16 tid = params->tid;
	struct ieee80211_txq *txq = sta->txq[tid];
	struct rtw_txq *rtwtxq = (struct rtw_txq *)txq->drv_priv;

	switch (params->action) {
	case IEEE80211_AMPDU_TX_START:
		return IEEE80211_AMPDU_TX_START_IMMEDIATE;
	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		clear_bit(RTW_TXQ_AMPDU, &rtwtxq->flags);
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		set_bit(RTW_TXQ_AMPDU, &rtwtxq->flags);
		break;
	case IEEE80211_AMPDU_RX_START:
	case IEEE80211_AMPDU_RX_STOP:
		break;
	default:
		WARN_ON(1);
		return -ENOTSUPP;
	}

	return 0;
}

static bool rtw_ops_can_aggregate_in_amsdu(struct ieee80211_hw *hw,
					   struct sk_buff *head,
					   struct sk_buff *skb)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_hal *hal = &rtwdev->hal;

	/* we don't want to enable TX AMSDU on 2.4G */
	if (hal->current_band_type == RTW_BAND_2G)
		return false;

	return true;
}

static void rtw_ops_sw_scan_start(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  const u8 *mac_addr)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;

	mutex_lock(&rtwdev->mutex);
	rtw_core_scan_start(rtwdev, rtwvif, mac_addr, false);
	mutex_unlock(&rtwdev->mutex);
}

static void rtw_ops_sw_scan_complete(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct rtw_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	rtw_core_scan_complete(rtwdev, vif, false);
	mutex_unlock(&rtwdev->mutex);
	if (rtw_d3_verbose)
		printf("rtw88u: scan_complete exit\n");
}

static void rtw_ops_mgd_prepare_tx(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_prep_tx_info *info)
{
	struct rtw_dev *rtwdev = hw->priv;

	printf("rtw88u: prepare_tx enter\n");
	mutex_lock(&rtwdev->mutex);
	rtw_leave_lps_deep(rtwdev);
	rtw_coex_connect_notify(rtwdev, COEX_ASSOCIATE_START);
	rtw_chip_prepare_tx(rtwdev);
	mutex_unlock(&rtwdev->mutex);
	printf("rtw88u: prepare_tx exit\n");
}

static int rtw_ops_set_rts_threshold(struct ieee80211_hw *hw, int radio_idx,
				     u32 value)
{
	struct rtw_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	rtwdev->rts_threshold = value;
	mutex_unlock(&rtwdev->mutex);

	return 0;
}

static void rtw_ops_sta_statistics(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta,
				   struct station_info *sinfo)
{
	struct rtw_sta_info *si = (struct rtw_sta_info *)sta->drv_priv;

	sinfo->txrate = si->ra_report.txrate;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
}

static void rtw_ops_flush(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif,
			  u32 queues, bool drop)
{
	struct rtw_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	rtw_leave_lps_deep(rtwdev);

	rtw_hci_flush_queues(rtwdev, queues, drop);
	rtw_mac_flush_queues(rtwdev, queues, drop);
	mutex_unlock(&rtwdev->mutex);
}

struct rtw_iter_bitrate_mask_data {
	struct rtw_dev *rtwdev;
	struct ieee80211_vif *vif;
	const struct cfg80211_bitrate_mask *mask;
};

static void rtw_ra_mask_info_update_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw_iter_bitrate_mask_data *br_data = data;
	struct rtw_sta_info *si = (struct rtw_sta_info *)sta->drv_priv;

	if (si->vif != br_data->vif)
		return;

	/* free previous mask setting */
	kfree(si->mask);
	si->mask = kmemdup(br_data->mask, sizeof(struct cfg80211_bitrate_mask),
			   GFP_ATOMIC);
	if (!si->mask) {
		si->use_cfg_mask = false;
		return;
	}

	si->use_cfg_mask = true;
	rtw_update_sta_info(br_data->rtwdev, si, true);
}

static void rtw_ra_mask_info_update(struct rtw_dev *rtwdev,
				    struct ieee80211_vif *vif,
				    const struct cfg80211_bitrate_mask *mask)
{
	struct rtw_iter_bitrate_mask_data br_data;

	br_data.rtwdev = rtwdev;
	br_data.vif = vif;
	br_data.mask = mask;
	rtw_iterate_stas(rtwdev, rtw_ra_mask_info_update_iter, &br_data);
}

static int rtw_ops_set_bitrate_mask(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    const struct cfg80211_bitrate_mask *mask)
{
	struct rtw_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	rtw_ra_mask_info_update(rtwdev, vif, mask);
	mutex_unlock(&rtwdev->mutex);

	return 0;
}

static int rtw_ops_set_antenna(struct ieee80211_hw *hw,
			       int radio_idx,
			       u32 tx_antenna,
			       u32 rx_antenna)
{
	struct rtw_dev *rtwdev = hw->priv;
	const struct rtw_chip_info *chip = rtwdev->chip;
	int ret;

	if (!chip->ops->set_antenna)
		return -EOPNOTSUPP;

	mutex_lock(&rtwdev->mutex);
	ret = chip->ops->set_antenna(rtwdev, -1, tx_antenna, rx_antenna);
	mutex_unlock(&rtwdev->mutex);

	return ret;
}

static int rtw_ops_get_antenna(struct ieee80211_hw *hw,
			       int radio_idx,
			       u32 *tx_antenna,
			       u32 *rx_antenna)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_hal *hal = &rtwdev->hal;

	*tx_antenna = hal->antenna_tx;
	*rx_antenna = hal->antenna_rx;

	return 0;
}

#ifdef CONFIG_PM
static int rtw_ops_suspend(struct ieee80211_hw *hw,
			   struct cfg80211_wowlan *wowlan)
{
	struct rtw_dev *rtwdev = hw->priv;
	int ret;

	mutex_lock(&rtwdev->mutex);
	ret = rtw_wow_suspend(rtwdev, wowlan);
	if (ret)
		rtw_err(rtwdev, "failed to suspend for wow %d\n", ret);
	mutex_unlock(&rtwdev->mutex);

	return ret ? 1 : 0;
}

static int rtw_ops_resume(struct ieee80211_hw *hw)
{
	struct rtw_dev *rtwdev = hw->priv;
	int ret;

	mutex_lock(&rtwdev->mutex);
	ret = rtw_wow_resume(rtwdev);
	if (ret)
		rtw_err(rtwdev, "failed to resume for wow %d\n", ret);
	mutex_unlock(&rtwdev->mutex);

	return ret ? 1 : 0;
}

static void rtw_ops_set_wakeup(struct ieee80211_hw *hw, bool enabled)
{
	struct rtw_dev *rtwdev = hw->priv;

	device_set_wakeup_enable(rtwdev->dev, enabled);
}
#endif

static void rtw_reconfig_complete(struct ieee80211_hw *hw,
				  enum ieee80211_reconfig_type reconfig_type)
{
	struct rtw_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	if (reconfig_type == IEEE80211_RECONFIG_TYPE_RESTART)
		clear_bit(RTW_FLAG_RESTARTING, rtwdev->flags);
	mutex_unlock(&rtwdev->mutex);
}

static int rtw_ops_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_scan_request *req)
{
	struct rtw_dev *rtwdev = hw->priv;
	int ret;

	if (!rtw_fw_feature_check(&rtwdev->fw, FW_FEATURE_SCAN_OFFLOAD))
		return 1;

	if (test_bit(RTW_FLAG_SCANNING, rtwdev->flags))
		return -EBUSY;

	mutex_lock(&rtwdev->mutex);
	rtw_hw_scan_start(rtwdev, vif, req);
	ret = rtw_hw_scan_offload(rtwdev, vif, true);
	if (ret) {
		rtw_hw_scan_abort(rtwdev);
		rtw_err(rtwdev, "HW scan failed with status: %d\n", ret);
	}
	mutex_unlock(&rtwdev->mutex);

	return ret;
}

static void rtw_ops_cancel_hw_scan(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif)
{
	struct rtw_dev *rtwdev = hw->priv;

	if (!rtw_fw_feature_check(&rtwdev->fw, FW_FEATURE_SCAN_OFFLOAD))
		return;

	if (!test_bit(RTW_FLAG_SCANNING, rtwdev->flags))
		return;

	mutex_lock(&rtwdev->mutex);
	rtw_hw_scan_abort(rtwdev);
	mutex_unlock(&rtwdev->mutex);
}

static int rtw_ops_set_sar_specs(struct ieee80211_hw *hw,
				 const struct cfg80211_sar_specs *sar)
{
	struct rtw_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	rtw_set_sar_specs(rtwdev, sar);
	mutex_unlock(&rtwdev->mutex);

	return 0;
}

static void rtw_ops_sta_rc_update(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_link_sta *link_sta,
				  u32 changed)
{
	struct ieee80211_sta *sta = link_sta->sta;
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_sta_info *si = (struct rtw_sta_info *)sta->drv_priv;

	if (changed & IEEE80211_RC_BW_CHANGED)
		ieee80211_queue_work(rtwdev->hw, &si->rc_work);
}

const struct ieee80211_ops rtw_ops = {
	.add_chanctx = ieee80211_emulate_add_chanctx,
	.remove_chanctx = ieee80211_emulate_remove_chanctx,
	.change_chanctx = ieee80211_emulate_change_chanctx,
	.switch_vif_chanctx = ieee80211_emulate_switch_vif_chanctx,
	.tx			= rtw_ops_tx,
	.wake_tx_queue		= rtw_ops_wake_tx_queue,
	.start			= rtw_ops_start,
	.stop			= rtw_ops_stop,
	.config			= rtw_ops_config,
	.add_interface		= rtw_ops_add_interface,
	.remove_interface	= rtw_ops_remove_interface,
	.change_interface	= rtw_ops_change_interface,
	.configure_filter	= rtw_ops_configure_filter,
	.bss_info_changed	= rtw_ops_bss_info_changed,
	.start_ap		= rtw_ops_start_ap,
	.stop_ap		= rtw_ops_stop_ap,
	.conf_tx		= rtw_ops_conf_tx,
	.sta_add		= rtw_ops_sta_add,
	.sta_remove		= rtw_ops_sta_remove,
	.set_tim		= rtw_ops_set_tim,
	.set_key		= rtw_ops_set_key,
	.ampdu_action		= rtw_ops_ampdu_action,
	.can_aggregate_in_amsdu	= rtw_ops_can_aggregate_in_amsdu,
	.sw_scan_start		= rtw_ops_sw_scan_start,
	.sw_scan_complete	= rtw_ops_sw_scan_complete,
	.mgd_prepare_tx		= rtw_ops_mgd_prepare_tx,
	.set_rts_threshold	= rtw_ops_set_rts_threshold,
	.sta_statistics		= rtw_ops_sta_statistics,
	.flush			= rtw_ops_flush,
	.set_bitrate_mask	= rtw_ops_set_bitrate_mask,
	.set_antenna		= rtw_ops_set_antenna,
	.get_antenna		= rtw_ops_get_antenna,
	.reconfig_complete	= rtw_reconfig_complete,
	.hw_scan		= rtw_ops_hw_scan,
	.cancel_hw_scan		= rtw_ops_cancel_hw_scan,
	.link_sta_rc_update	= rtw_ops_sta_rc_update,
	.set_sar_specs          = rtw_ops_set_sar_specs,
#ifdef CONFIG_PM
	.suspend		= rtw_ops_suspend,
	.resume			= rtw_ops_resume,
	.set_wakeup		= rtw_ops_set_wakeup,
#endif
};
EXPORT_SYMBOL(rtw_ops);
