/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    rtl8xxxu/ports/rtos/rtl8xxxu_diag.c
 * @brief   Read-only AP-role probes for the RTL8188EUS RTOS arm.
 *
 * @design First-party units, not driver bytes: the two exported functions and
 *         the two -Wl,--wrap shims below observe the vendored driver from
 *         outside it.  The driver tree carries two recorded patches for the AP
 *         role (wireless-drivers/rtl8xxxu/patches/), and this unit answers "did
 *         the beacon path actually run" without touching those bytes.
 *
 *         Why: the first AP boot completed A2/A3 (the Pi associated and pinged
 *         both ways) but the monitor's 12 s capture after hostap start carried
 *         zero beacons from the board, while it did carry the board's probe
 *         responses, auth and assoc frames.  The 8188EU's beacon is built and
 *         sent by the host (update_beacon_work -> rtl8xxxu_send_beacon_frame ->
 *         ieee80211_beacon_get + TX queue BEACON), unlike the rtw88/rtw89 arms'
 *         firmware beacons, so the discriminator is: did the host compose a
 *         beacon (beacon_get), did it reach the TX descriptor (fill_txdesc), and
 *         is the chip's beacon function armed (registers)?
 *
 *         Signals:
 *           - `rtl8xxxu_diag_ap_state()`: one line with the vif's
 *             enable_beacon/beacon_int and the beacon registers:
 *             REG_BEACON_CTRL (0x0550), REG_FWHW_TXQ_CTRL (0x0420, EN_BCNQ_DL),
 *             REG_TBTT_PROHIBIT (0x0540), REG_BCN_INTERVAL (0x0554),
 *             REG_TDECTRL (0x0208, BCN_VALID) and REG_DUAL_TSF_RST (0x0553).
 *             `start_tx_beacon` sets EN_BCNQ_DL, `add_interface`'s AP branch
 *             sets BEACON_FUNCTION_ENABLE, and a successful download sets
 *             BCN_VALID; the line separates "notification never came" from
 *             "beacon downloaded but not transmitted".
 *           - `rtl8xxxu_diag_beacon_counters()`: how many times the host asked
 *             for a beacon (and how many came back NULL) and how many beacon
 *             frames reached `rtl8xxxu_fill_txdesc_v3`, with the macid and the
 *             txdw1 value the descriptor got.  The 8188e's macid field is
 *             txdw1[5:0]; a beacon must carry 0 (broadcast).
 *
 *         Not a delivery feature: read-only, and the AP case calls it.  The
 *         types are void* so the stack-world case does not include the driver
 *         headers.
 * @date    14.11.2026
 */

#include <linux/types.h>
#include <linux/etherdevice.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

/* mac80211's internal local: the wiphy's private area, whose first member is
 * the driver-visible ieee80211_hw (the same access rtw88_diag.c uses). */
#include "ieee80211_i.h"

/* Relative to this file: the include path carries aarch64/regs.h earlier
 * than the driver directory, so a bare "regs.h" would be the ARM header. */
#include "../../rtl8xxxu.h"
#include "../../regs.h"

/* Read-only counters filled by the wrappers at the bottom. */
static unsigned int diag_fill_total;
static unsigned int diag_data_txdesc;
static unsigned int diag_data_dumped;
static unsigned int diag_beacon_get;
static unsigned int diag_beacon_get_null;
static unsigned int diag_beacon_txdesc;
static unsigned int diag_beacon_txdesc_len;
static u32 diag_beacon_txdw1;
static u8 diag_beacon_macid;
#if CONFIG_RTL8XXXU_TX_CENSUS
static unsigned int diag_h2c_calls;
#endif

static struct rtl8xxxu_priv *
diag_priv(void *wiphy)
{
	struct ieee80211_local *local = (struct ieee80211_local *)wiphy_priv(wiphy);
	struct ieee80211_hw *hw = &local->hw;

	return (struct rtl8xxxu_priv *)hw->priv;
}

void
rtl8xxxu_diag_ap_state(void *wiphy)
{
	struct rtl8xxxu_priv *priv;
	struct ieee80211_vif *vif;
	u8 bcn_ctrl, tsf_rst;
	u32 txq_ctrl, tbtt, tdectrl;
	u16 bcn_int;

	if (wiphy == NULL)
		return;
	priv = diag_priv(wiphy);
	if (priv == NULL)
		return;
	vif = priv->vifs[0];

	bcn_ctrl = rtl8xxxu_read8(priv, REG_BEACON_CTRL);
	txq_ctrl = rtl8xxxu_read32(priv, REG_FWHW_TXQ_CTRL);
	tbtt = rtl8xxxu_read32(priv, REG_TBTT_PROHIBIT);
	bcn_int = rtl8xxxu_read16(priv, REG_BCN_INTERVAL);
	tdectrl = rtl8xxxu_read32(priv, REG_TDECTRL);
	tsf_rst = rtl8xxxu_read8(priv, REG_DUAL_TSF_RST);
	lkpi_printf(ULOG_INFO_LEVEL,
	    "rtl8xxxu_diag: ap vif=%d type=%d enable_beacon=%d bcn_int=%d "
	    "bcn_ctrl=0x%02x txq_ctrl=0x%08x tsf_rst=0x%02x\n",
	    vif != NULL, vif != NULL ? (int)vif->type : -1,
	    vif != NULL ? (int)vif->bss_conf.enable_beacon : -1,
	    vif != NULL ? (int)vif->bss_conf.beacon_int : -1,
	    (unsigned)bcn_ctrl, (unsigned)txq_ctrl, (unsigned)tsf_rst);
	lkpi_printf(ULOG_INFO_LEVEL,
	    "rtl8xxxu_diag: reg tbtt=0x%08x bcn_int_reg=%u tdectrl=0x%08x "
	    "bcn_valid=%d en_bcnq_dl=%d bcn_func_en=%d\n",
	    (unsigned)tbtt, (unsigned)bcn_int, (unsigned)tdectrl,
	    (tdectrl & BIT_BCN_VALID) ? 1 : 0,
	    (txq_ctrl & EN_BCNQ_DL) ? 1 : 0,
	    (bcn_ctrl & BEACON_FUNCTION_ENABLE) ? 1 : 0);
}

void
rtl8xxxu_diag_beacon_counters(void)
{
	lkpi_printf(ULOG_INFO_LEVEL,
	    "rtl8xxxu_diag: beacon get=%u null=%u txdesc=%u len=%u "
	    "txdw1=0x%08x macid=%u\n",
	    diag_beacon_get, diag_beacon_get_null, diag_beacon_txdesc,
	    diag_beacon_txdesc_len, (unsigned)diag_beacon_txdw1,
	    (unsigned)diag_beacon_macid);
}

/* --- the diagnostic arm --------------------------------------------------- */

/*
 * Clear REG_BCN_CTRL bit4 (DIS_TSF_UDT0 on the 8188E), the state the vendor
 * driver leaves behind after its beacon download.  Called only by the
 * CONFIG_RTL8XXXU_AP_ARM_BCN4 arm, right after the start-up beacon; the
 * delivery path never writes here.
 */
void
rtl8xxxu_diag_ap_arm_bcn4(void *wiphy)
{
	struct rtl8xxxu_priv *priv;
	u8 before, after;

	if (wiphy == NULL)
		return;
	priv = diag_priv(wiphy);
	if (priv == NULL)
		return;
	before = rtl8xxxu_read8(priv, REG_BEACON_CTRL);
	after = (u8)((before | BEACON_FUNCTION_ENABLE) & ~BIT(4));
	rtl8xxxu_write8(priv, REG_BEACON_CTRL, after);
	lkpi_printf(ULOG_INFO_LEVEL,
	    "rtl8xxxu_diag: arm bcn4 before=0x%02x after=0x%02x\n",
	    (unsigned)before, (unsigned)after);
}

/* --- the data-plane census (CONFIG_RTL8XXXU_TX_CENSUS) ---------------------------- */

/*
 * The data-plane data-plane blocker is "the chip sends the broadcast ARP and no
 * unicast frame after it".  The descriptor census proves the host side is
 * identical for the two (only txdw0[0], the BROADMULTICAST bit, differs), so
 * the question is what the chip does differently.  Two observations answer it:
 *
 *   - the H2C media-status report: the driver's own comment says "the firmware
 *     turns on the rate control when it knows it's connected to a network".
 *     report_connect() writes one mailbox and the firmware clears REG_HMTFR's
 *     bit when it reads it, so the read-back separates "written but never
 *     consumed" from "consumed".
 *   - the TX state registers: a paused queue (REG_TXPAUSE), the queue-to-DMA
 *     map (REG_TRXDMA_CTRL), the page split (REG_RQPN/RQPN_NPQ) and the TX
 *     report arming are the chip-side state that differs between the two
 *     benches (2 bulk-OUT endpoints here, 4 on the RK3568 arm).
 *
 * Both are read-only and both print one line; the case calls chip_state at
 * three points of the data-plane boot.  Never in the transmit path: these are
 * synchronous USB control transfers and the TX path is not the place for them.
 */
#if CONFIG_RTL8XXXU_TX_CENSUS

void
rtl8xxxu_diag_chip_state(void *wiphy, const char *tag)
{
	struct rtl8xxxu_priv *priv;
	u8 txpause, txrpt, hmtfr;
	u16 cr, trxdma;
	u32 fwhw, rqpn, rqnpq, hisr, hisre;

	if (wiphy == NULL)
		return;
	priv = diag_priv(wiphy);
	if (priv == NULL)
		return;

	hmtfr = rtl8xxxu_read8(priv, REG_HMTFR);
	txpause = rtl8xxxu_read8(priv, REG_TXPAUSE);
	cr = rtl8xxxu_read16(priv, REG_CR);
	fwhw = rtl8xxxu_read32(priv, REG_FWHW_TXQ_CTRL);
	trxdma = rtl8xxxu_read16(priv, REG_TRXDMA_CTRL);
	rqpn = rtl8xxxu_read32(priv, REG_RQPN);
	rqnpq = rtl8xxxu_read32(priv, REG_RQPN_NPQ);
	txrpt = rtl8xxxu_read8(priv, REG_TX_REPORT_CTRL);
	hisr = rtl8xxxu_read32(priv, REG_HISR);
	hisre = rtl8xxxu_read32(priv, REG_HISRE);

	lkpi_printf(ULOG_INFO_LEVEL,
	    "rtl8xxxu_diag: chip tag=%s out_eps=%d ep_tx=%u hmtfr=0x%02x "
	    "txpause=0x%02x cr=0x%04x fwhw_txq=0x%08x trxdma=0x%04x "
	    "rqpn=0x%08x rqpn_npq=0x%08x txrpt=0x%02x hisr=0x%08x "
	    "hisre=0x%08x\n",
	    tag != NULL ? tag : "-", priv->nr_out_eps,
	    (unsigned)priv->ep_tx_count, (unsigned)hmtfr, (unsigned)txpause,
	    (unsigned)cr,
	    (unsigned)fwhw, (unsigned)trxdma, (unsigned)rqpn,
	    (unsigned)rqnpq, (unsigned)txrpt, (unsigned)hisr,
	    (unsigned)hisre);
}

void __real_rtl8xxxu_gen2_report_connect(struct rtl8xxxu_priv *priv,
    u8 macid, u8 role, bool connect);

void
__wrap_rtl8xxxu_gen2_report_connect(struct rtl8xxxu_priv *priv, u8 macid,
    u8 role, bool connect)
{
	u8 before = 0U, after = 0U;
	u32 mbox[4] = { 0U, 0U, 0U, 0U };
	unsigned int i;

	if (priv == NULL) {
		__real_rtl8xxxu_gen2_report_connect(priv, macid, role, connect);
		return;
	}
	before = rtl8xxxu_read8(priv, REG_HMTFR);
	__real_rtl8xxxu_gen2_report_connect(priv, macid, role, connect);
	after = rtl8xxxu_read8(priv, REG_HMTFR);
	for (i = 0U; i < 4U; i++)
		mbox[i] = rtl8xxxu_read32(priv, (u16)(REG_HMBOX_0 + i * 4U));
	lkpi_printf(ULOG_INFO_LEVEL,
	    "rtl8xxxu_diag: h2c n=%u macid=%u role=%u conn=%u "
	    "hmtfr_before=0x%02x hmtfr_after=0x%02x next=%d "
	    "mbox0=0x%08x mbox1=0x%08x mbox2=0x%08x mbox3=0x%08x\n",
	    diag_h2c_calls++, (unsigned)macid, (unsigned)role,
	    connect ? 1U : 0U, (unsigned)before, (unsigned)after,
	    priv->next_mbox, (unsigned)mbox[0], (unsigned)mbox[1],
	    (unsigned)mbox[2], (unsigned)mbox[3]);
}

#endif /* CONFIG_RTL8XXXU_TX_CENSUS */

/* --- the two shims -------------------------------------------------------- */

struct sk_buff *__real_ieee80211_beacon_get_tim(struct ieee80211_hw *hw,
    struct ieee80211_vif *vif, u16 *tim_offset, u16 *tim_length,
    unsigned int link_id);

struct sk_buff *
__wrap_ieee80211_beacon_get_tim(struct ieee80211_hw *hw,
    struct ieee80211_vif *vif, u16 *tim_offset, u16 *tim_length,
    unsigned int link_id)
{
	struct sk_buff *skb = __real_ieee80211_beacon_get_tim(hw, vif,
	    tim_offset, tim_length, link_id);

	diag_beacon_get++;
	if (skb == NULL)
		diag_beacon_get_null++;
	return skb;
}

void __real_rtl8xxxu_fill_txdesc_v3(struct ieee80211_hw *hw,
    struct ieee80211_hdr *hdr, struct ieee80211_tx_info *tx_info,
    struct rtl8xxxu_txdesc32 *tx_desc, bool sgi, bool short_preamble,
    bool ampdu_enable, u32 rts_rate, u8 macid);

void
__wrap_rtl8xxxu_fill_txdesc_v3(struct ieee80211_hw *hw,
    struct ieee80211_hdr *hdr, struct ieee80211_tx_info *tx_info,
    struct rtl8xxxu_txdesc32 *tx_desc, bool sgi, bool short_preamble,
    bool ampdu_enable, u32 rts_rate, u8 macid)
{
	diag_fill_total++;
	__real_rtl8xxxu_fill_txdesc_v3(hw, hdr, tx_info, tx_desc, sgi,
	    short_preamble, ampdu_enable, rts_rate, macid);
	if (1) {
		struct rtl8xxxu_priv *priv = hw->priv;

		if (ieee80211_is_data(hdr->frame_control))
			diag_data_txdesc++;
		/* The first frames of the data-plane data path: the descriptor the chip
		 * accepts for the one frame that reaches the air and for the ones
		 * that do not.  Bounded to sixteen, one line each, so the broadcast
		 * ARP and the unicast frames that follow it are both in the record. */
		if (diag_data_dumped < 16U) {
			diag_data_dumped++;
			dev_info(&priv->udev->dev,
				 "rtl8xxxu_txdump: #%u/%u fc=%04x da=%02x:%02x:%02x:%02x:%02x:%02x len=%u off=%u txdw0=%08x txdw1=%08x txdw2=%08x txdw3=%08x txdw4=%08x txdw5=%08x q=%u macid=%u rate=%u\n",
				 diag_fill_total, diag_data_txdesc,
				 (unsigned)le16_to_cpu(hdr->frame_control),
				 hdr->addr1[0], hdr->addr1[1], hdr->addr1[2],
				 hdr->addr1[3], hdr->addr1[4], hdr->addr1[5],
				 (unsigned)le16_to_cpu(tx_desc->pkt_size),
				 (unsigned)tx_desc->pkt_offset,
				 le32_to_cpu(tx_desc->txdw0),
				 le32_to_cpu(tx_desc->txdw1),
				 le32_to_cpu(tx_desc->txdw2),
				 le32_to_cpu(tx_desc->txdw3),
				 le32_to_cpu(tx_desc->txdw4),
				 le32_to_cpu(tx_desc->txdw5),
				 (unsigned)((le32_to_cpu(tx_desc->txdw1) >> 8) & 0x1fU),
				 (unsigned)(le32_to_cpu(tx_desc->txdw1) & 0x3fU),
				 (unsigned)(le32_to_cpu(tx_desc->txdw5) & 0x3fU));
		}
	}
	if (ieee80211_is_beacon(hdr->frame_control)) {
		if (diag_beacon_txdesc == 0U) {
			diag_beacon_txdw1 = le32_to_cpu(tx_desc->txdw1);
			diag_beacon_macid = macid;
			diag_beacon_txdesc_len = le16_to_cpu(tx_desc->pkt_size);
		}
		diag_beacon_txdesc++;
	}
}

/*
 * The fill-descriptor census.  A zero data count beside the driver's own
 * per-frame `TX rate` lines is impossible; a data count of one beside them
 * means the frames after the first are filled by a path the wrap does not
 * see.  The F429 data-plane case prints this after its ping window.
 */
void
rtl8xxxu_diag_tx_counts(unsigned int *total, unsigned int *data)
{
	if (total != NULL)
		*total = diag_fill_total;
	if (data != NULL)
		*data = diag_data_txdesc;
}
