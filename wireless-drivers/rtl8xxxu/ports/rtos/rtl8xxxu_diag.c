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
		/* The first frames of the W6 data path: the descriptor the chip
		 * accepts for the one frame that reaches the air and for the ones
		 * that do not.  Bounded to six, one line each. */
		if (diag_data_dumped < 6U) {
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
 * see.  The F429 W6 case prints this after its ping window.
 */
void
rtl8xxxu_diag_tx_counts(unsigned int *total, unsigned int *data)
{
	if (total != NULL)
		*total = diag_fill_total;
	if (data != NULL)
		*data = diag_data_txdesc;
}
