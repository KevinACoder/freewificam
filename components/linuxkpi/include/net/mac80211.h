/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    net/mac80211.h
 * @brief   The mac80211 surface a Linux SoftMAC driver talks to: the hardware
 *          descriptor, the driver operation table, the per-frame control blocks and
 *          the entry points the driver calls back into.
 *
 * @design declaration-only compatibility face over this project's own mac80211 body.
 *         The Linux driver compiled here is a mac80211 driver: it allocates a
 *         `struct ieee80211_hw`, registers an operation table and hands frames up
 *         through `ieee80211_rx_irqsafe`. In this project the MLME lives in the
 *         imported NetBSD net80211 stack, so this header and its body are a substitute
 *         mac80211 whose operation table is bridged onto the net80211 state machine -
 *         the state machine is *not* re-implemented here. Layouts are transcribed from
 *         the interface (include/net/mac80211.h); they must be exact where the driver
 *         casts them onto frame bytes or skb control blocks, and the sizes are asserted
 *         at compile time below. Subset by consumer: the queue/txq engine, aggregation
 *         and AP-side templates arrive with the bridge that needs them. Reference for
 *         the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    08.10.2026
 */

#ifndef _LINUXKPI_NET_MAC80211_H_
#define _LINUXKPI_NET_MAC80211_H_

#include "../../linuxkpi_core.h"
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/printk.h>
#include <linux/skbuff.h>
#include <net/cfg80211.h>
/* [LNX] include/net/mac80211.h:24: the interface includes the radiotap header itself,
 * which is how a unit that only takes mac80211.h sees the per-field records. */
#include <net/ieee80211_radiotap.h>

struct cfg80211_gtk_rekey_data;
struct ieee80211_low_level_stats;

struct ieee80211_hw;
struct ieee80211_vif;
struct ieee80211_sta;
struct ieee80211_key_conf;
/* [LNX] include/net/mac80211.h (the queue record, drv_priv last) */
struct ieee80211_txq {
	struct ieee80211_vif *vif;
	struct ieee80211_sta *sta;
	u8 tid;
	u8 ac;

	/* must be last */
	u8 drv_priv[] __aligned(sizeof(void *));
};

struct ieee80211_txq;

/* ------------------------------------------------------------------ */
/* queues and access categories                                        */
/* ------------------------------------------------------------------ */

#define	IEEE80211_MAX_QUEUES		16
#define	IEEE80211_INVAL_HW_QUEUE	0xff

enum ieee80211_ac_numbers {
	IEEE80211_AC_VO = 0,
	IEEE80211_AC_VI = 1,
	IEEE80211_AC_BE = 2,
	IEEE80211_AC_BK = 3,
};

/* ------------------------------------------------------------------ */
/* hardware capability flags                                           */
/* ------------------------------------------------------------------ */

/*
 * Transcribed in full: the count sizes the flag word array inside the hardware
 * descriptor, so a truncated enumeration would silently let a driver set a flag
 * outside its own bitmap.
 */
enum ieee80211_hw_flags {
	IEEE80211_HW_HAS_RATE_CONTROL,
	IEEE80211_HW_RX_INCLUDES_FCS,
	IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING,
	IEEE80211_HW_SIGNAL_UNSPEC,
	IEEE80211_HW_SIGNAL_DBM,
	IEEE80211_HW_NEED_DTIM_BEFORE_ASSOC,
	IEEE80211_HW_SPECTRUM_MGMT,
	IEEE80211_HW_AMPDU_AGGREGATION,
	IEEE80211_HW_SUPPORTS_PS,
	IEEE80211_HW_PS_NULLFUNC_STACK,
	IEEE80211_HW_SUPPORTS_DYNAMIC_PS,
	IEEE80211_HW_MFP_CAPABLE,
	IEEE80211_HW_WANT_MONITOR_VIF,
	IEEE80211_HW_NO_VIRTUAL_MONITOR,
	IEEE80211_HW_NO_AUTO_VIF,
	IEEE80211_HW_SW_CRYPTO_CONTROL,
	IEEE80211_HW_SUPPORT_FAST_XMIT,
	IEEE80211_HW_REPORTS_TX_ACK_STATUS,
	IEEE80211_HW_CONNECTION_MONITOR,
	IEEE80211_HW_QUEUE_CONTROL,
	IEEE80211_HW_SUPPORTS_PER_STA_GTK,
	IEEE80211_HW_AP_LINK_PS,
	IEEE80211_HW_TX_AMPDU_SETUP_IN_HW,
	IEEE80211_HW_SUPPORTS_RC_TABLE,
	IEEE80211_HW_P2P_DEV_ADDR_FOR_INTF,
	IEEE80211_HW_TIMING_BEACON_ONLY,
	IEEE80211_HW_SUPPORTS_HT_CCK_RATES,
	IEEE80211_HW_CHANCTX_STA_CSA,
	IEEE80211_HW_SUPPORTS_CLONED_SKBS,
	IEEE80211_HW_SINGLE_SCAN_ON_ALL_BANDS,
	IEEE80211_HW_TDLS_WIDER_BW,
	IEEE80211_HW_SUPPORTS_AMSDU_IN_AMPDU,
	IEEE80211_HW_BEACON_TX_STATUS,
	IEEE80211_HW_NEEDS_UNIQUE_STA_ADDR,
	IEEE80211_HW_SUPPORTS_REORDERING_BUFFER,
	IEEE80211_HW_USES_RSS,
	IEEE80211_HW_TX_AMSDU,
	IEEE80211_HW_TX_FRAG_LIST,
	IEEE80211_HW_REPORTS_LOW_ACK,
	IEEE80211_HW_SUPPORTS_TX_FRAG,
	IEEE80211_HW_SUPPORTS_TDLS_BUFFER_STA,
	IEEE80211_HW_DOESNT_SUPPORT_QOS_NDP,
	IEEE80211_HW_BUFF_MMPDU_TXQ,
	IEEE80211_HW_SUPPORTS_VHT_EXT_NSS_BW,
	IEEE80211_HW_STA_MMPDU_TXQ,
	IEEE80211_HW_TX_STATUS_NO_AMPDU_LEN,
	IEEE80211_HW_SUPPORTS_MULTI_BSSID,
	IEEE80211_HW_SUPPORTS_ONLY_HE_MULTI_BSSID,
	IEEE80211_HW_AMPDU_KEYBORDER_SUPPORT,
	IEEE80211_HW_SUPPORTS_TX_ENCAP_OFFLOAD,
	IEEE80211_HW_SUPPORTS_RX_DECAP_OFFLOAD,
	IEEE80211_HW_SUPPORTS_CONC_MON_RX_DECAP,
	IEEE80211_HW_DETECTS_COLOR_COLLISION,
	IEEE80211_HW_MLO_MCAST_MULTI_LINK_TX,
	IEEE80211_HW_DISALLOW_PUNCTURING,
	IEEE80211_HW_HANDLES_QUIET_CSA,
	IEEE80211_HW_STRICT,
	IEEE80211_HW_SUPPORTS_NDP_BLOCKACK,

	NUM_IEEE80211_HW_FLAGS
};

/* ------------------------------------------------------------------ */
/* bss/link state changes                                              */
/* ------------------------------------------------------------------ */

/* The subset the driver reacts to; values are the interface's bit positions. */
enum ieee80211_bss_change {
	BSS_CHANGED_ASSOC		= 1 << 0,
	BSS_CHANGED_ERP_CTS_PROT	= 1 << 1,
	BSS_CHANGED_ERP_PREAMBLE	= 1 << 2,
	BSS_CHANGED_ERP_SLOT		= 1 << 3,
	BSS_CHANGED_HT			= 1 << 4,
	BSS_CHANGED_BASIC_RATES		= 1 << 5,
	BSS_CHANGED_BEACON_INT		= 1 << 6,
	BSS_CHANGED_BSSID		= 1 << 7,
	BSS_CHANGED_BEACON		= 1 << 8,
	BSS_CHANGED_BEACON_ENABLED	= 1 << 9,
	BSS_CHANGED_CQM = 1024,
	BSS_CHANGED_ARP_FILTER = 4096,
	BSS_CHANGED_PS = 131072,
	BSS_CHANGED_P2P_PS = 524288,
	BSS_CHANGED_MU_GROUPS = 8388608,
	BSS_CHANGED_HE_BSS_COLOR = 536870912,
	BSS_CHANGED_MLD_VALID_LINKS = 1073741826,
	BSS_CHANGED_TPE = 1073741828
};

/* ------------------------------------------------------------------ */
/* per-frame transmit control                                          */
/* ------------------------------------------------------------------ */

enum mac80211_tx_info_flags {
	IEEE80211_TX_CTL_REQ_TX_STATUS		= BIT(0),
	IEEE80211_TX_CTL_ASSIGN_SEQ		= BIT(1),
	IEEE80211_TX_CTL_NO_ACK			= BIT(2),
	IEEE80211_TX_CTL_CLEAR_PS_FILT		= BIT(3),
	IEEE80211_TX_CTL_FIRST_FRAGMENT		= BIT(4),
	IEEE80211_TX_CTL_SEND_AFTER_DTIM	= BIT(5),
	IEEE80211_TX_CTL_AMPDU			= BIT(6),
	IEEE80211_TX_CTL_INJECTED		= BIT(7),
	IEEE80211_TX_STAT_TX_FILTERED		= BIT(8),
	IEEE80211_TX_STAT_ACK			= BIT(9),
	IEEE80211_TX_STAT_AMPDU			= BIT(10),
	IEEE80211_TX_STAT_AMPDU_NO_BACK		= BIT(11),
	IEEE80211_TX_CTL_RATE_CTRL_PROBE	= BIT(12),
	IEEE80211_TX_INTFL_OFFCHAN_TX_OK	= BIT(13),
	IEEE80211_TX_CTL_HW_80211_ENCAP		= BIT(14),
	IEEE80211_TX_INTFL_RETRIED		= BIT(15),
	IEEE80211_TX_INTFL_DONT_ENCRYPT		= BIT(16),
	IEEE80211_TX_CTL_NO_PS_BUFFER		= BIT(17),
	IEEE80211_TX_CTL_MORE_FRAMES		= BIT(18),
	IEEE80211_TX_INTFL_RETRANSMISSION	= BIT(19),
	IEEE80211_TX_INTFL_MLME_CONN_TX		= BIT(20),
	IEEE80211_TX_INTFL_NL80211_FRAME_TX	= BIT(21),
	IEEE80211_TX_CTL_LDPC			= BIT(22),
	IEEE80211_TX_CTL_STBC			= BIT(23) | BIT(24),
	IEEE80211_TX_CTL_TX_OFFCHAN = 33554432,
	IEEE80211_TX_CTL_NO_CCK_RATE = 134217728,
	IEEE80211_TX_STAT_NOACK_TRANSMITTED = 2147483648
};

#define	IEEE80211_TX_MAX_RATES		4

/* Rate flags inside a per-rate entry. */
#define	IEEE80211_TX_RC_USE_RTS_CTS		BIT(0)
#define	IEEE80211_TX_RC_USE_CTS_PROTECT		BIT(1)
#define	IEEE80211_TX_RC_USE_SHORT_PREAMBLE	BIT(2)
#define	IEEE80211_TX_RC_MCS			BIT(3)
#define	IEEE80211_TX_RC_GREEN_FIELD		BIT(4)
#define	IEEE80211_TX_RC_40_MHZ_WIDTH		BIT(5)
#define	IEEE80211_TX_RC_DUP_DATA		BIT(6)
#define	IEEE80211_TX_RC_SHORT_GI		BIT(7)

#define	IEEE80211_MAX_TX_RETRY			31

struct ieee80211_tx_rate {
	s8	idx;
	u16	count:5,
		flags:11;
} __packed;

/* [LNX] include/linux/ieee80211-he.h:169-451 (record carried whole) */
struct ieee80211_he_mu_edca_param_ac_rec {
	u8 aifsn;
	u8 ecw_min_max;
	u8 mu_edca_timer;
} __packed;

/**
 * struct ieee80211_mu_edca_param_set - MU EDCA Parameter Set element
 * @mu_qos_info: QoS Info
 * @ac_be: MU AC_BE Parameter Record
 * @ac_bk: MU AC_BK Parameter Record
 * @ac_vi: MU AC_VI Parameter Record
 * @ac_vo: MU AC_VO Parameter Record
 *
 * This structure represents the payload of the "MU EDCA Parameter Set
 * element" as described in IEEE Std 802.11ax-2021 section 9.4.2.251.
 */
struct ieee80211_mu_edca_param_set {
	u8 mu_qos_info;
	struct ieee80211_he_mu_edca_param_ac_rec ac_be;
	struct ieee80211_he_mu_edca_param_ac_rec ac_bk;
	struct ieee80211_he_mu_edca_param_ac_rec ac_vi;
	struct ieee80211_he_mu_edca_param_ac_rec ac_vo;
} __packed;

/* 802.11ax HE MAC capabilities */
#define IEEE80211_HE_MAC_CAP0_HTC_HE				0x01
#define IEEE80211_HE_MAC_CAP0_TWT_REQ				0x02
#define IEEE80211_HE_MAC_CAP0_TWT_RES				0x04
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_NOT_SUPP		0x00
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_LEVEL_1		0x08
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_LEVEL_2		0x10
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_LEVEL_3		0x18
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_MASK			0x18
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_1		0x00
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_2		0x20
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_4		0x40
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_8		0x60
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_16		0x80
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_32		0xa0
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_64		0xc0
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_UNLIMITED	0xe0
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_MASK		0xe0

#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_UNLIMITED		0x00
#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_128			0x01
#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_256			0x02
#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_512			0x03
#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_MASK		0x03
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_0US		0x00
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_8US		0x04
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US		0x08
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_MASK		0x0c
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_1		0x00
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_2		0x10
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_3		0x20
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_4		0x30
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_5		0x40
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_6		0x50
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_7		0x60
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8		0x70
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_MASK		0x70

/* Link adaptation is split between byte HE_MAC_CAP1 and
 * HE_MAC_CAP2. It should be set only if IEEE80211_HE_MAC_CAP0_HTC_HE
 * in which case the following values apply:
 * 0 = No feedback.
 * 1 = reserved.
 * 2 = Unsolicited feedback.
 * 3 = both
 */
#define IEEE80211_HE_MAC_CAP1_LINK_ADAPTATION			0x80

#define IEEE80211_HE_MAC_CAP2_LINK_ADAPTATION			0x01
#define IEEE80211_HE_MAC_CAP2_ALL_ACK				0x02
#define IEEE80211_HE_MAC_CAP2_TRS				0x04
#define IEEE80211_HE_MAC_CAP2_BSR				0x08
#define IEEE80211_HE_MAC_CAP2_BCAST_TWT				0x10
#define IEEE80211_HE_MAC_CAP2_32BIT_BA_BITMAP			0x20
#define IEEE80211_HE_MAC_CAP2_MU_CASCADING			0x40
#define IEEE80211_HE_MAC_CAP2_ACK_EN				0x80

#define IEEE80211_HE_MAC_CAP3_OMI_CONTROL			0x02
#define IEEE80211_HE_MAC_CAP3_OFDMA_RA				0x04

/* The maximum length of an A-MDPU is defined by the combination of the Maximum
 * A-MDPU Length Exponent field in the HT capabilities, VHT capabilities and the
 * same field in the HE capabilities.
 */
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_0		0x00
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_1		0x08
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_2		0x10
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3		0x18
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_MASK		0x18
#define IEEE80211_HE_MAC_CAP3_AMSDU_FRAG			0x20
#define IEEE80211_HE_MAC_CAP3_FLEX_TWT_SCHED			0x40
#define IEEE80211_HE_MAC_CAP3_RX_CTRL_FRAME_TO_MULTIBSS		0x80

#define IEEE80211_HE_MAC_CAP4_BSRP_BQRP_A_MPDU_AGG		0x01
#define IEEE80211_HE_MAC_CAP4_QTP				0x02
#define IEEE80211_HE_MAC_CAP4_BQR				0x04
#define IEEE80211_HE_MAC_CAP4_PSR_RESP				0x08
#define IEEE80211_HE_MAC_CAP4_NDP_FB_REP			0x10
#define IEEE80211_HE_MAC_CAP4_OPS				0x20
#define IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU			0x40
/* Multi TID agg TX is split between byte #4 and #5
 * The value is a combination of B39,B40,B41
 */
#define IEEE80211_HE_MAC_CAP4_MULTI_TID_AGG_TX_QOS_B39		0x80

#define IEEE80211_HE_MAC_CAP5_MULTI_TID_AGG_TX_QOS_B40		0x01
#define IEEE80211_HE_MAC_CAP5_MULTI_TID_AGG_TX_QOS_B41		0x02
#define IEEE80211_HE_MAC_CAP5_SUBCHAN_SELECTIVE_TRANSMISSION	0x04
#define IEEE80211_HE_MAC_CAP5_UL_2x996_TONE_RU			0x08
#define IEEE80211_HE_MAC_CAP5_OM_CTRL_UL_MU_DATA_DIS_RX		0x10
#define IEEE80211_HE_MAC_CAP5_HE_DYNAMIC_SM_PS			0x20
#define IEEE80211_HE_MAC_CAP5_PUNCTURED_SOUNDING		0x40
#define IEEE80211_HE_MAC_CAP5_HT_VHT_TRIG_FRAME_RX		0x80

#define IEEE80211_HE_VHT_MAX_AMPDU_FACTOR	20
#define IEEE80211_HE_HT_MAX_AMPDU_FACTOR	16
#define IEEE80211_HE_6GHZ_MAX_AMPDU_FACTOR	13

/* 802.11ax HE PHY capabilities */
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G		0x02
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G	0x04
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G		0x08
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G	0x10
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_MASK_ALL		0x1e

#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_2G	0x20
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_5G	0x40
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_MASK			0xfe

#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_80MHZ_ONLY_SECOND_20MHZ	0x01
#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_80MHZ_ONLY_SECOND_40MHZ	0x02
#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_160MHZ_ONLY_SECOND_20MHZ	0x04
#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_160MHZ_ONLY_SECOND_40MHZ	0x08
#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK			0x0f
#define IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A				0x10
#define IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD			0x20
#define IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US		0x40
/* Midamble RX/TX Max NSTS is split between byte #2 and byte #3 */
#define IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS			0x80

#define IEEE80211_HE_PHY_CAP2_MIDAMBLE_RX_TX_MAX_NSTS			0x01
#define IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US			0x02
#define IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ			0x04
#define IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ			0x08
#define IEEE80211_HE_PHY_CAP2_DOPPLER_TX				0x10
#define IEEE80211_HE_PHY_CAP2_DOPPLER_RX				0x20

/* Note that the meaning of UL MU below is different between an AP and a non-AP
 * sta, where in the AP case it indicates support for Rx and in the non-AP sta
 * case it indicates support for Tx.
 */
#define IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO			0x40
#define IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO			0x80

#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_NO_DCM			0x00
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_BPSK			0x01
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_QPSK			0x02
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_16_QAM			0x03
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_MASK			0x03
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_1				0x00
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_2				0x04
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_NO_DCM			0x00
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_BPSK			0x08
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_QPSK			0x10
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM			0x18
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_MASK			0x18
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1				0x00
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_2				0x20
#define IEEE80211_HE_PHY_CAP3_RX_PARTIAL_BW_SU_IN_20MHZ_MU		0x40
#define IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER				0x80

#define IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE				0x01
#define IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER				0x02

/* Minimal allowed value of Max STS under 80MHz is 3 */
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4		0x0c
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_5		0x10
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_6		0x14
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_7		0x18
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_8		0x1c
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_MASK	0x1c

/* Minimal allowed value of Max STS above 80MHz is 3 */
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_4		0x60
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_5		0x80
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_6		0xa0
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_7		0xc0
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_8		0xe0
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_MASK	0xe0

#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_1	0x00
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_2	0x01
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_3	0x02
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_4	0x03
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_5	0x04
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_6	0x05
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_7	0x06
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_8	0x07
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK	0x07

#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_1	0x00
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_2	0x08
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_3	0x10
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_4	0x18
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_5	0x20
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_6	0x28
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_7	0x30
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_8	0x38
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_MASK	0x38

#define IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK				0x40
#define IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK				0x80

#define IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU			0x01
#define IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU			0x02
#define IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB			0x04
#define IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB		0x08
#define IEEE80211_HE_PHY_CAP6_TRIG_CQI_FB				0x10
#define IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE			0x20
#define IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO		0x40
#define IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT			0x80

#define IEEE80211_HE_PHY_CAP7_PSR_BASED_SR				0x01
#define IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_SUPP			0x02
#define IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI		0x04
#define IEEE80211_HE_PHY_CAP7_MAX_NC_1					0x08
#define IEEE80211_HE_PHY_CAP7_MAX_NC_2					0x10
#define IEEE80211_HE_PHY_CAP7_MAX_NC_3					0x18
#define IEEE80211_HE_PHY_CAP7_MAX_NC_4					0x20
#define IEEE80211_HE_PHY_CAP7_MAX_NC_5					0x28
#define IEEE80211_HE_PHY_CAP7_MAX_NC_6					0x30
#define IEEE80211_HE_PHY_CAP7_MAX_NC_7					0x38
#define IEEE80211_HE_PHY_CAP7_MAX_NC_MASK				0x38
#define IEEE80211_HE_PHY_CAP7_STBC_TX_ABOVE_80MHZ			0x40
#define IEEE80211_HE_PHY_CAP7_STBC_RX_ABOVE_80MHZ			0x80

#define IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI		0x01
#define IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G		0x02
#define IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU			0x04
#define IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU			0x08
#define IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI		0x10
#define IEEE80211_HE_PHY_CAP8_MIDAMBLE_RX_TX_2X_AND_1XLTF		0x20
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_242				0x00
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484				0x40
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_996				0x80
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_2x996				0xc0
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_MASK				0xc0

#define IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM		0x01
#define IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK		0x02
#define IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU		0x04
#define IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU		0x08
#define IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB	0x10
#define IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB	0x20
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_0US			0x0
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_8US			0x1
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_16US			0x2
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_RESERVED		0x3
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_POS			6
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_MASK			0xc0

#define IEEE80211_HE_PHY_CAP10_HE_MU_M1RU_MAX_LTF			0x01

/* 802.11ax HE TX/RX MCS NSS Support  */
#define IEEE80211_TX_RX_MCS_NSS_SUPP_HIGHEST_MCS_POS			(3)
#define IEEE80211_TX_RX_MCS_NSS_SUPP_TX_BITMAP_POS			(6)
#define IEEE80211_TX_RX_MCS_NSS_SUPP_RX_BITMAP_POS			(11)
#define IEEE80211_TX_RX_MCS_NSS_SUPP_TX_BITMAP_MASK			0x07c0
#define IEEE80211_TX_RX_MCS_NSS_SUPP_RX_BITMAP_MASK			0xf800

/* TX/RX HE MCS Support field Highest MCS subfield encoding */
enum ieee80211_he_highest_mcs_supported_subfield_enc {
	HIGHEST_MCS_SUPPORTED_MCS7 = 0,
	HIGHEST_MCS_SUPPORTED_MCS8,
	HIGHEST_MCS_SUPPORTED_MCS9,
	HIGHEST_MCS_SUPPORTED_MCS10,
	HIGHEST_MCS_SUPPORTED_MCS11,
};

struct ieee80211_tx_queue_params {
	struct ieee80211_he_mu_edca_param_ac_rec mu_edca_param_rec;
	bool mu_edca;
	u16	txop;
	u16	cw_min;
	u16	cw_max;
	u8	aifs;
	bool	acm;
	bool	uapsd;
};

/*
 * The control block mac80211 hands down with every frame. Layout matters: the
 * driver writes the transmit rate/status fields through it and hijacks one of the
 * driver-data words as its completion context, and the receive status structure
 * shares the same control block, so every union member must sit at the offsets the
 * interface puts it at (asserted below).
 */
#define	IEEE80211_TX_INFO_DRIVER_DATA_SIZE	40
#define	IEEE80211_TX_INFO_RATE_DRIVER_DATA_SIZE	24

struct ieee80211_tx_info {
	u32	flags;
	u32	band:3,
		hw_queue:4,
		tx_time_est:10;
	union {
		struct {
			union {
				struct {
					struct ieee80211_tx_rate rates[IEEE80211_TX_MAX_RATES];
					s8 rts_cts_rate_idx;
					u8 use_rts:1;
					u8 use_cts_prot:1;
					u8 short_preamble:1;
					u8 skip_table:1;
					u8 antennas:2;
				};
				/*
				 * The interface overlays this slot with the current time;
				 * the name cannot be used bare here because this project's
				 * contract header defines it as a macro (the kernel API it
				 * stands for), so the macro is suspended for the field.
				 */
#pragma push_macro("jiffies")
#undef jiffies
				unsigned long jiffies;
			};
#pragma pop_macro("jiffies")
			struct ieee80211_vif *vif;
			struct ieee80211_key_conf *hw_key;
			u32 flags;
		} control;
		struct {
			u64 cookie;
		} ack;
		struct {
			struct ieee80211_tx_rate rates[IEEE80211_TX_MAX_RATES];
			s32 ack_signal;
			u8 ampdu_ack_len;
			u8 ampdu_len;
			u8 antenna;
			u8 pad;
			u8 flags;
			void *status_driver_data[16 / sizeof(void *)];
		} status;
		struct {
			struct ieee80211_tx_rate driver_rates[IEEE80211_TX_MAX_RATES];
			u8 pad[4];
			void *rate_driver_data[
				IEEE80211_TX_INFO_RATE_DRIVER_DATA_SIZE / sizeof(void *)];
		};
		void *driver_data[
			IEEE80211_TX_INFO_DRIVER_DATA_SIZE / sizeof(void *)];
	};
};

/* ------------------------------------------------------------------ */
/* per-frame receive status                                            */
/* ------------------------------------------------------------------ */

enum mac80211_rx_encoding_flags {
	RX_ENC_FLAG_SHORTPRE	= BIT(0),
	RX_ENC_FLAG_SHORT_GI	= BIT(2),
	RX_ENC_FLAG_HT_GF	= BIT(3),
	RX_ENC_FLAG_STBC_MASK	= BIT(4) | BIT(5),
	RX_ENC_FLAG_LDPC	= BIT(6),
	RX_ENC_FLAG_BF		= BIT(7),
};

enum mac80211_rx_encoding {
	RX_ENC_LEGACY = 0,
	RX_ENC_HT,
	RX_ENC_VHT,
	RX_ENC_HE,
	RX_ENC_EHT,
	RX_ENC_SHR,
	RX_ENC_S1G,
};

#define	RX_ENC_FLAG_STBC_SHIFT	4

/* Receiver flags reported alongside the frame. */
#define	RX_FLAG_MMIC_ERROR		BIT(0)
#define	RX_FLAG_DECRYPTED		BIT(1)
#define	RX_FLAG_FAILED_FCS_CRC		BIT(2)
#define	RX_FLAG_FAILED_PLCP_CRC		BIT(3)
#define	RX_FLAG_MACTIME_START		BIT(4)
#define	RX_FLAG_SHORTPRE		BIT(5)
#define	RX_FLAG_HT			BIT(6)
#define	RX_FLAG_40MHZ			BIT(7)
#define	RX_FLAG_SHORT_GI		BIT(8)
#define	RX_FLAG_NO_SIGNAL_VAL		BIT(9)
#define	RX_FLAG_IV_STRIPPED		BIT(10)
#define	RX_FLAG_MMIC_STRIPPED		BIT(11)
#define	RX_FLAG_AMPDU_DETAILS		BIT(12)
#define	RX_FLAG_PN_VALIDATED		BIT(13)
#define	RX_FLAG_DUP_VALIDATED		BIT(14)
#define	RX_FLAG_AMPDU_LAST_KNOWN	BIT(15)
#define	RX_FLAG_AMPDU_IS_LAST		BIT(16)
#define	RX_FLAG_AMPDU_DELIM_CRC_ERROR	BIT(17)
#define	RX_FLAG_AMPDU_DELIM_CRC_KNOWN	BIT(18)
/* [LNX] include/net/mac80211.h:1683 (the one further flag the compiled set names) */
#define	RX_FLAG_NO_PSDU			BIT(29)

struct ieee80211_rx_status {
	u64	mactime;
	u32	device_timestamp;
	u32	flag;
	u16	freq:13, freq_offset:1;
	u8	enc_flags;
	u8	encoding:3, bw:4;
	/* [LNX] include/net/mac80211.h: the per-format rate descriptor union (HE/EHT) */
	union {
		struct {
			u8 he_ru:3;
			u8 he_gi:2;
			u8 he_dcm:1;
		} __packed;
		struct {
			u8 ru:4;
			u8 gi:2;
		} __packed eht;
	} __packed;
	u8	rate_idx;
	u8	nss;
	u8	rx_flags;
	u8	band;
	u8	antenna;
	s8	signal;
	u8	chains;
	s8	chain_signal[IEEE80211_MAX_CHAINS];
	u64	boottime_ns;
	u32	ampdu_reference;
	u8	link_valid:1, link_id:4;
};

/* ------------------------------------------------------------------ */
/* global hardware configuration                                       */
/* ------------------------------------------------------------------ */

enum ieee80211_conf_flags {
	IEEE80211_CONF_MONITOR		= BIT(0),
	IEEE80211_CONF_PS		= BIT(1),
	IEEE80211_CONF_IDLE		= BIT(3),
	IEEE80211_CONF_OFFCHANNEL	= BIT(4),
};

enum ieee80211_conf_changed {
	IEEE80211_CONF_CHANGE_SMPS		= BIT(1),
	IEEE80211_CONF_CHANGE_LISTEN_INTERVAL	= BIT(2),
	IEEE80211_CONF_CHANGE_MONITOR		= BIT(3),
	IEEE80211_CONF_CHANGE_PS		= BIT(4),
	IEEE80211_CONF_CHANGE_POWER		= BIT(5),
	IEEE80211_CONF_CHANGE_CHANNEL		= BIT(6),
	IEEE80211_CONF_CHANGE_RETRY_LIMITS	= BIT(7),
	IEEE80211_CONF_CHANGE_IDLE		= BIT(8),
};

enum ieee80211_smps_mode {
	IEEE80211_SMPS_OFF,
	IEEE80211_SMPS_STATIC,
	IEEE80211_SMPS_DYNAMIC,
	IEEE80211_SMPS_AUTOMATIC,
};

struct ieee80211_conf {
	u32			flags;
	int			power_level, dynamic_ps_timeout;
	u16			listen_interval;
	u8			ps_dtim_period;
	u8			long_frame_max_tx_count,
				short_frame_max_tx_count;
	struct cfg80211_chan_def chandef;
	bool			radar_enabled;
	enum ieee80211_smps_mode smps_mode;
};

struct ieee80211_chanctx_conf {
	struct cfg80211_chan_def def;
	struct cfg80211_chan_def min_def;
	bool			radar_enabled;
	u8			drv_priv[] __aligned(sizeof(void *));
};

struct ieee80211_vif_chanctx_switch {
	struct ieee80211_bss_conf *link_conf;
	struct ieee80211_vif		*vif;
	struct ieee80211_chanctx_conf	*old_ctx;
	struct ieee80211_chanctx_conf	*new_ctx;
};

enum ieee80211_chanctx_switch_mode {
	CHANCTX_SWMODE_REASSIGN_VIF,
	CHANCTX_SWMODE_SWAP_CONTEXTS,
};

/* ------------------------------------------------------------------ */
/* virtual interface and the bss state it carries                      */
/* ------------------------------------------------------------------ */

/* [LNX] include/net/mac80211.h:425 */
#define	IEEE80211_BSS_ARP_ADDR_LIST_LEN	4

struct ieee80211_vif_cfg {
	__be32			arp_addr_list[IEEE80211_BSS_ARP_ADDR_LIST_LEN];
	bool	assoc, ibss_joined;
	bool	ibss_creator;
	bool	ps;
	u16	aid;
	u8	ssid[32];
	size_t	ssid_len;
	bool	s1g;
	bool	idle;
	u8	ap_addr[ETH_ALEN] __aligned(2);
};
/* [LNX] include/net/mac80211.h:568,575 */
#define	IEEE80211_TPE_EIRP_ENTRIES_320MHZ	5
#define	IEEE80211_TPE_PSD_ENTRIES_320MHZ	16


/* [LNX] include/net/mac80211.h:596 (record carried whole) */
struct ieee80211_parsed_tpe_eirp {
	bool valid;
	s8 power[IEEE80211_TPE_EIRP_ENTRIES_320MHZ];
	u8 count;
};

/* [LNX] include/net/mac80211.h:576 (record carried whole) */
struct ieee80211_parsed_tpe_psd {
	bool valid;
	s8 power[IEEE80211_TPE_PSD_ENTRIES_320MHZ];
	u8 count, n;
};

struct ieee80211_parsed_tpe {
	struct ieee80211_parsed_tpe_eirp max_local[2], max_reg_client[2];
	struct ieee80211_parsed_tpe_psd psd_local[2], psd_reg_client[2];
};

/* [LNX] include/net/mac80211.h:244 (record carried whole) */
struct ieee80211_chan_req {
	struct cfg80211_chan_def oper;
	struct cfg80211_chan_def ap;
	bool require_npca;
};

/* [LNX] include/net/mac80211.h:536 (record carried whole) */
struct ieee80211_mu_group_data {
	u8 membership[WLAN_MEMBERSHIP_LEN];
	u8 position[WLAN_USER_POSITION_LEN];
};

struct ieee80211_bss_conf {
	struct ieee80211_vif	*vif;
	const u8		*bssid;
	u8			addr[ETH_ALEN] __aligned(2);
	bool			use_cts_prot;
	bool			use_short_preamble;
	bool			use_short_slot;
	bool			enable_beacon;
	u8			dtim_period;
	u16			beacon_int;
	u16			assoc_capability;
	u32			basic_rates;
	int			mcast_rate[NUM_NL80211_BANDS];
	u16			ht_operation_mode;
	bool			ht_ldpc;
	bool			vht_ldpc;
	bool			csa_active;
	struct ieee80211_chanctx_conf __rcu *chanctx_conf;
	unsigned int		link_id;
	bool			he_support;
	u8			transmitter_bssid[ETH_ALEN] __aligned(2);
	u8			bssid_index;
	bool			nontransmitted;
	struct {
		u32 params;
		u16 nss_set;
	} he_oper;
	struct cfg80211_he_bss_color he_bss_color;
	struct ieee80211_mu_group_data mu_group;
	s32			cqm_rssi_thold;
	u32			cqm_rssi_hyst;
	struct ieee80211_chan_req chanreq;
	struct ieee80211_p2p_noa_attr p2p_noa_attr;
	struct ieee80211_parsed_tpe tpe;
	enum ieee80211_ap_reg_power power_type;
	bool			eht_support;
};

struct ieee80211_vif {
	enum nl80211_iftype	type;
	struct ieee80211_vif_cfg cfg;
	struct ieee80211_bss_conf bss_conf;
	struct ieee80211_bss_conf *link_conf[IEEE80211_MLD_MAX_NUM_LINKS];
	u16			valid_links, active_links, dormant_links;
	u8			addr[ETH_ALEN] __aligned(2);
	bool			addr_valid;
	bool			p2p;
	u8			cab_queue;
	u8			hw_queue[IEEE80211_NUM_ACS];
	struct ieee80211_txq	*txq;
	struct ieee80211_txq	*txq_mgmt;
	netdev_features_t	netdev_features;
	u32			driver_flags;
	bool			probe_req_reg;
	bool			rx_mcast_action_reg;
	u8			drv_priv[] __aligned(sizeof(void *));
};

/* [LNX] include/net/mac80211.h:2607 (record carried whole) */
struct ieee80211_sta_aggregates {
	u16 max_amsdu_len;

	u16 max_rc_amsdu_len;
	u16 max_tid_amsdu_len[IEEE80211_NUM_TIDS];
};

/* [LNX] include/net/mac80211.h:2540 (the enumerators the compiled set names, at their computed positions) */
enum ieee80211_sta_rx_bandwidth {
	IEEE80211_STA_RX_BW_20 = 0,
	IEEE80211_STA_RX_BW_40 = 1,
	IEEE80211_STA_RX_BW_80 = 2,
	IEEE80211_STA_RX_BW_160 = 3,
	IEEE80211_STA_RX_BW_320 = 4
};

struct ieee80211_link_sta {
	struct ieee80211_sta		*sta;
	u8				addr[ETH_ALEN];
	u8				link_id;
	enum ieee80211_smps_mode	smps_mode;
	u32				supp_rates[NUM_NL80211_BANDS];
	struct ieee80211_sta_ht_cap	ht_cap;
	struct ieee80211_sta_vht_cap	vht_cap;
	/* [LNX] include/net/mac80211.h: the capability records and the aggregate limits
	 * the compiled set reads off a link, in the interface's order */
	struct ieee80211_sta_he_cap	he_cap;
	struct ieee80211_sta_eht_cap	eht_cap;
	struct ieee80211_sta_aggregates	agg;
	u8				rx_nss;
	enum ieee80211_sta_rx_bandwidth	bandwidth;
};

struct ieee80211_sta {
	u8				addr[ETH_ALEN] __aligned(2);
	u16				aid;
	u16				max_rx_aggregation_subframes;
	bool				wme;
	u8				uapsd_queues;
	u8				max_sp;
	struct ieee80211_sta_rates __rcu *rates;
	bool				tdls;
	bool				mfp;
	bool				mlo;
	u8				max_amsdu_subframes;
	/* [LNX] include/net/mac80211.h:2749,2755: the per-TID queues and the per-link
	 * records the compiled set indexes */
	struct ieee80211_txq *txq[IEEE80211_NUM_TIDS + 1];
	struct ieee80211_link_sta	deflink;
	struct ieee80211_link_sta __rcu *link[IEEE80211_MLD_MAX_NUM_LINKS];
	u8				drv_priv[] __aligned(sizeof(void *));
};

/* ------------------------------------------------------------------ */
/* keys                                                                */
/* ------------------------------------------------------------------ */

enum ieee80211_key_flags {
	IEEE80211_KEY_FLAG_GENERATE_IV_MGMT	= BIT(0),
	IEEE80211_KEY_FLAG_GENERATE_IV		= BIT(1),
	IEEE80211_KEY_FLAG_GENERATE_MMIC	= BIT(2),
	IEEE80211_KEY_FLAG_PAIRWISE		= BIT(3),
	IEEE80211_KEY_FLAG_SW_MGMT_TX		= BIT(4),
	IEEE80211_KEY_FLAG_PUT_IV_SPACE		= BIT(5),
	IEEE80211_KEY_FLAG_RX_MGMT		= BIT(6),
	IEEE80211_KEY_FLAG_RESERVE_TAILROOM	= BIT(7),
	IEEE80211_KEY_FLAG_PUT_MIC_SPACE	= BIT(8),
	IEEE80211_KEY_FLAG_NO_AUTO_TX		= BIT(9),
};

struct ieee80211_key_conf {
	/* [LNX] include/net/mac80211.h: the transmit packet number is the atomic the
	 * driver increments per frame (the type is contract with that call) */
	atomic64_t	tx_pn;
	u32	cipher;
	u8	icv_len;
	u8	iv_len;
	u8	hw_key_idx;
	s8	keyidx;
	u16	flags;
	s8	link_id;
	u8	keylen;
	u8	key[];
};

enum set_key_cmd {
	SET_KEY,
	DISABLE_KEY,
};

/* ------------------------------------------------------------------ */
/* aggregation                                                         */
/* ------------------------------------------------------------------ */

enum ieee80211_ampdu_mlme_action {
	IEEE80211_AMPDU_RX_START,
	IEEE80211_AMPDU_RX_STOP,
	IEEE80211_AMPDU_TX_START,
	IEEE80211_AMPDU_TX_STOP_CONT,
	IEEE80211_AMPDU_TX_STOP_FLUSH,
	IEEE80211_AMPDU_TX_STOP_FLUSH_CONT,
	IEEE80211_AMPDU_TX_OPERATIONAL,
};

#define	IEEE80211_AMPDU_TX_START_IMMEDIATE	1
#define	IEEE80211_AMPDU_TX_START_DELAY_ADDBA	2

struct ieee80211_ampdu_params {
	enum ieee80211_ampdu_mlme_action action;
	struct ieee80211_sta	*sta;
	u16			tid;
	u16			ssn;
	u16			buf_size;
	bool			amsdu;
	u16			timeout;
};

/* ------------------------------------------------------------------ */
/* receive filters                                                     */
/* ------------------------------------------------------------------ */

enum ieee80211_filter_flags {
	FIF_ALLMULTI		= 1 << 1,
	FIF_FCSFAIL		= 1 << 2,
	FIF_PLCPFAIL		= 1 << 3,
	FIF_BCN_PRBRESP_PROMISC	= 1 << 4,
	FIF_CONTROL		= 1 << 5,
	FIF_OTHER_BSS		= 1 << 6,
	FIF_PSPOLL		= 1 << 7,
	FIF_PROBE_REQ		= 1 << 8,
	FIF_MCAST_ACTION	= 1 << 9,
};

enum ieee80211_interface_iteration_flags {
	IEEE80211_IFACE_ITER_NORMAL		= 0,
	IEEE80211_IFACE_ITER_RESUME_ALL		= BIT(0),
	IEEE80211_IFACE_ITER_ACTIVE		= BIT(1),
	IEEE80211_IFACE_SKIP_SDATA_NOT_IN_DRIVER	= BIT(2),
};

/* ------------------------------------------------------------------ */
/* the driver operation table                                          */
/* ------------------------------------------------------------------ */

/*
 * Only the operations this project's substitute mac80211 drives are declared; the
 * driver's designated initialisers name exactly these, so a missing one is a
 * compile error rather than a silently NULL hook.
 */
struct ieee80211_tx_control {
	struct ieee80211_sta	*sta;
};

/* [LNX] include/net/mac80211.h:4050 (the enumerators the compiled set names, at their computed positions) */
enum ieee80211_reconfig_type {
	IEEE80211_RECONFIG_TYPE_RESTART = 0
};

/* [LNX] include/net/mac80211.h:4034 (the enumerators the compiled set names, at their computed positions) */
enum ieee80211_roc_type {
	IEEE80211_ROC_TYPE_MGMT_TX = 1
};

/* [LNX] include/net/mac80211.h:2519 (the enumerators the compiled set names, at their computed positions) */
enum ieee80211_sta_state {
	IEEE80211_STA_NOTEXIST = 0,
	IEEE80211_STA_NONE = 1,
	IEEE80211_STA_AUTH = 2,
	IEEE80211_STA_ASSOC = 3
};

/* [LNX] include/net/mac80211.h:4068 (record carried whole) */
struct ieee80211_prep_tx_info {
	u16 duration;
	u16 subtype;
	u8 success:1, was_assoc:1;
	int link_id;
};

struct ieee80211_ops {
	void	(*tx)(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
		       struct sk_buff *skb);
	int	(*start)(struct ieee80211_hw *hw);
	void	(*stop)(struct ieee80211_hw *hw, bool suspend);
	int	(*add_interface)(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif);
	int (*change_interface)(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				enum nl80211_iftype new_type, bool p2p);
	void	(*remove_interface)(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif);
	int	(*config)(struct ieee80211_hw *hw, int radio_idx, u32 changed);
	void	(*bss_info_changed)(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_bss_conf *info,
				    u64 changed);
	void (*vif_cfg_changed)(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				u64 changed);
	void (*link_info_changed)(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_bss_conf *info,
				  u64 changed);
	int	(*start_ap)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf);
	void (*stop_ap)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct ieee80211_bss_conf *link_conf);
	void	(*configure_filter)(struct ieee80211_hw *hw,
				    unsigned int changed_flags,
				    unsigned int *total_flags, u64 multicast);
	int	(*set_tim)(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
			   bool set);
	int	(*set_key)(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			   struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			   struct ieee80211_key_conf *key);
	void (*set_rekey_data)(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct cfg80211_gtk_rekey_data *data);
	int (*hw_scan)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       struct ieee80211_scan_request *req);
	void (*cancel_hw_scan)(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif);
	void	(*sw_scan_start)(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif, const u8 *mac_addr);
	void	(*sw_scan_complete)(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif);
	int (*get_stats)(struct ieee80211_hw *hw,
			 struct ieee80211_low_level_stats *stats);
	int	(*set_rts_threshold)(struct ieee80211_hw *hw, int radio_idx,
				     u32 value);
	int	(*sta_add)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta);
	int	(*sta_remove)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta);
	int (*sta_state)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta,
			 enum ieee80211_sta_state old_state,
			 enum ieee80211_sta_state new_state);
	void (*link_sta_rc_update)(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_link_sta *link_sta,
				   u32 changed);
	void	(*sta_statistics)(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta,
				  struct station_info *sinfo);
	int	(*conf_tx)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   unsigned int link_id, u16 ac,
			   const struct ieee80211_tx_queue_params *params);
	int	(*ampdu_action)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
				struct ieee80211_ampdu_params *params);
	int (*get_survey)(struct ieee80211_hw *hw, int idx,
		struct survey_info *survey);
	void (*rfkill_poll)(struct ieee80211_hw *hw);
	void (*flush)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      u32 queues, bool drop);
	int (*set_antenna)(struct ieee80211_hw *hw, int radio_idx,
			   u32 tx_ant, u32 rx_ant);
	int	(*get_antenna)(struct ieee80211_hw *hw, int radio_idx,
			       u32 *tx_ant, u32 *rx_ant);
	int (*remain_on_channel)(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_channel *chan,
				 int duration,
				 enum ieee80211_roc_type type);
	int (*cancel_remain_on_channel)(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif);
	int (*set_bitrate_mask)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
				const struct cfg80211_bitrate_mask *mask);
	int	(*add_chanctx)(struct ieee80211_hw *hw,
			       struct ieee80211_chanctx_conf *ctx);
	void	(*remove_chanctx)(struct ieee80211_hw *hw,
				  struct ieee80211_chanctx_conf *ctx);
	void	(*change_chanctx)(struct ieee80211_hw *hw,
				  struct ieee80211_chanctx_conf *ctx,
				  u32 changed);
	int (*assign_vif_chanctx)(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_bss_conf *link_conf,
				  struct ieee80211_chanctx_conf *ctx);
	void (*unassign_vif_chanctx)(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *link_conf,
				     struct ieee80211_chanctx_conf *ctx);
	int	(*switch_vif_chanctx)(struct ieee80211_hw *hw,
				      struct ieee80211_vif_chanctx_switch *vifs,
				      int n_vifs,
				      enum ieee80211_chanctx_switch_mode mode);
	void (*reconfig_complete)(struct ieee80211_hw *hw,
				  enum ieee80211_reconfig_type reconfig_type);
	void (*channel_switch_beacon)(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct cfg80211_chan_def *chandef);
	void	(*wake_tx_queue)(struct ieee80211_hw *hw, struct ieee80211_txq *txq);
	int (*set_tid_config)(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta,
			      struct cfg80211_tid_config *tid_conf);
	int (*set_sar_specs)(struct ieee80211_hw *hw,
			     const struct cfg80211_sar_specs *sar);
	bool (*can_activate_links)(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   u16 active_links);
	int (*change_vif_links)(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				u16 old_links, u16 new_links,
				struct ieee80211_bss_conf *old[IEEE80211_MLD_MAX_NUM_LINKS]);
	int (*change_sta_links)(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta,
				u16 old_links, u16 new_links);
	/* [LNX] include/net/mac80211.h:4981,5076 (the two callbacks only the 8821CU set names) */
	void	(*mgd_prepare_tx)(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_prep_tx_info *info);
	bool	(*can_aggregate_in_amsdu)(struct ieee80211_hw *hw,
					  struct sk_buff *head,
					  struct sk_buff *skb);
};

/* ------------------------------------------------------------------ */
/* the hardware descriptor                                             */
/* ------------------------------------------------------------------ */

#define	IEEE80211_MAX_SSID_LEN		32

struct ieee80211_hw {
	struct ieee80211_conf	conf;
	struct wiphy		*wiphy;
	const char		*rate_control_algorithm;
	void			*priv;
	unsigned long		flags[BITS_TO_LONGS(NUM_IEEE80211_HW_FLAGS)];
	unsigned int		extra_tx_headroom;
	unsigned int		extra_beacon_tailroom;
	int			vif_data_size;
	int			sta_data_size;
	int			chanctx_data_size;
	int			txq_data_size;
	u16			queues;
	u16			max_listen_interval;
	s8			max_signal;
	u8			max_rates;
	u8			max_report_rates;
	u8			max_rate_tries;
	u16			max_rx_aggregation_subframes;
	u16			max_tx_aggregation_subframes;
	u8			max_tx_fragments;
	u8			offchannel_tx_hw_queue;
	u8			radiotap_mcs_details;
	u16			radiotap_vht_details;
	netdev_features_t	netdev_features;
	u8			uapsd_queues;
	u8			uapsd_max_sp_len;
	u8			tx_sk_pacing_shift;
	u8			weight_multiplier;
	u32			max_mtu;
};

/*
 * Channel-width predicates over the configured channel: what a driver asks before
 * choosing between its 20 MHz and its 40 MHz register settings. The interface's
 * definitions, including the sideband question the 40 MHz ones answer by comparing the
 * centre frequency against the primary channel.
 */
static inline bool conf_is_ht20(struct ieee80211_conf *conf)
{
	return conf->chandef.width == NL80211_CHAN_WIDTH_20;
}

static inline bool conf_is_ht40_minus(struct ieee80211_conf *conf)
{
	return conf->chandef.width == NL80211_CHAN_WIDTH_40 &&
	       conf->chandef.center_freq1 < conf->chandef.chan->center_freq;
}

static inline bool conf_is_ht40_plus(struct ieee80211_conf *conf)
{
	return conf->chandef.width == NL80211_CHAN_WIDTH_40 &&
	       conf->chandef.center_freq1 > conf->chandef.chan->center_freq;
}

static inline bool conf_is_ht40(struct ieee80211_conf *conf)
{
	return conf->chandef.width == NL80211_CHAN_WIDTH_40;
}

static inline bool conf_is_ht(struct ieee80211_conf *conf)
{
	return (conf->chandef.width != NL80211_CHAN_WIDTH_5) &&
	       (conf->chandef.width != NL80211_CHAN_WIDTH_10) &&
	       (conf->chandef.width != NL80211_CHAN_WIDTH_20_NOHT);
}

static inline void _ieee80211_hw_set(struct ieee80211_hw *hw,
				     enum ieee80211_hw_flags flg)
{
	set_bit(flg, hw->flags);
}

#define	ieee80211_hw_set(hw, flg)	_ieee80211_hw_set(hw, IEEE80211_HW_##flg)
#define	ieee80211_hw_check(hw, flg)	_ieee80211_hw_check(hw, IEEE80211_HW_##flg)

/* [LNX] include/net/mac80211.h:3320-3325: the interface's checker takes the prefixed
 * constant, and the macro below pastes the prefix, as the interface does */
static inline bool _ieee80211_hw_check(struct ieee80211_hw *hw,
				       enum ieee80211_hw_flags flg)
{
	return test_bit(flg, hw->flags);
}

static inline void SET_IEEE80211_DEV(struct ieee80211_hw *hw, struct device *dev)
{
	set_wiphy_dev(hw->wiphy, dev);
}

static inline void SET_IEEE80211_PERM_ADDR(struct ieee80211_hw *hw, const u8 *addr)
{
	ether_addr_copy(hw->wiphy->perm_addr, addr);
	eth_broadcast_addr(hw->wiphy->addr_mask);
}

/* ------------------------------------------------------------------ */
/* the control blocks inside skb->cb                                   */
/* ------------------------------------------------------------------ */

#define	IEEE80211_SKB_CB(__skb)	((struct ieee80211_tx_info *)((__skb)->cb))
#define	IEEE80211_SKB_RXCB(__skb) ((struct ieee80211_rx_status *)((__skb)->cb))

/*
 * The compile-time half of the control-block contract: the transmit info and the
 * receive status are both cast onto skb->cb, so they must fit it, and the union
 * members the driver writes through must sit at the same offset (the interface's
 * own assertion, and the reason its clear-status helper can mask the status as one
 * block).
 */
_Static_assert(sizeof(struct ieee80211_tx_info) <= sizeof(((struct sk_buff *)0)->cb),
	       "ieee80211_tx_info must fit the skb control block");
_Static_assert(sizeof(struct ieee80211_rx_status) <= sizeof(((struct sk_buff *)0)->cb),
	       "ieee80211_rx_status must fit the skb control block");
_Static_assert(offsetof(struct ieee80211_tx_info, status.rates) ==
	       offsetof(struct ieee80211_tx_info, control.rates),
	       "status and control rates must overlap");
_Static_assert(offsetof(struct ieee80211_tx_info, status.rates) ==
	       offsetof(struct ieee80211_tx_info, driver_data),
	       "driver data must start at the rates offset");

/*
 * Clear everything in the transmit status except the rate-control information
 * (which stays because the driver reports it back), so the driver only has to fill
 * in what it learned about the frame.
 */
static inline void ieee80211_tx_info_clear_status(struct ieee80211_tx_info *info)
{
	memset(&info->status.rates[0], 0,
	       sizeof(struct ieee80211_tx_info) -
	       offsetof(struct ieee80211_tx_info, status.rates));
}

/* ------------------------------------------------------------------ */
/* entry points the driver calls                                       */
/* ------------------------------------------------------------------ */

struct ieee80211_hw *ieee80211_alloc_hw(size_t priv_data_len,
					const struct ieee80211_ops *ops);
void ieee80211_free_hw(struct ieee80211_hw *hw);
int ieee80211_register_hw(struct ieee80211_hw *hw);
void ieee80211_unregister_hw(struct ieee80211_hw *hw);

void ieee80211_rx_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb);
void ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb);

struct ieee80211_sta *ieee80211_find_sta(struct ieee80211_vif *vif,
					 const u8 *addr);
struct ieee80211_sta *ieee80211_find_sta_by_ifaddr(struct ieee80211_hw *hw,
						   const u8 *addr,
						   const u8 *localaddr);
void ieee80211_iterate_active_interfaces_atomic(struct ieee80211_hw *hw,
						u32 iter_flags,
						void (*iterator)(void *data, u8 *mac,
								 struct ieee80211_vif *vif),
						void *data);
void ieee80211_iterate_stations_atomic(struct ieee80211_hw *hw,
				       void (*iterator)(void *data,
							struct ieee80211_sta *sta),
				       void *data);

void ieee80211_stop_queues(struct ieee80211_hw *hw);
void ieee80211_wake_queues(struct ieee80211_hw *hw);

int ieee80211_start_tx_ba_session(struct ieee80211_sta *sta, u16 tid, u16 timeout);
void ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_vif *vif, const u8 *ra,
				     u16 tid);

struct sk_buff *ieee80211_beacon_get(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     unsigned int link_id);
bool ieee80211_beacon_cntdwn_is_complete(struct ieee80211_vif *vif,
					 unsigned int link_id);
void ieee80211_csa_finish(struct ieee80211_vif *vif, unsigned int link_id);

/* The channel-context emulation helpers a software part may point its ops at. */
int ieee80211_emulate_add_chanctx(struct ieee80211_hw *hw,
				  struct ieee80211_chanctx_conf *ctx);
void ieee80211_emulate_remove_chanctx(struct ieee80211_hw *hw,
				      struct ieee80211_chanctx_conf *ctx);
void ieee80211_emulate_change_chanctx(struct ieee80211_hw *hw,
				      struct ieee80211_chanctx_conf *ctx,
				      u32 changed);
int ieee80211_emulate_switch_vif_chanctx(struct ieee80211_hw *hw,
					 struct ieee80211_vif_chanctx_switch *vifs,
					 int n_vifs,
					 enum ieee80211_chanctx_switch_mode mode);
void ieee80211_handle_wake_tx_queue(struct ieee80211_hw *hw,
				    struct ieee80211_txq *txq);

/* [LNX] include/net/mac80211.h:1497 (record carried whole) */
struct ieee80211_scan_ies {
	const u8 *ies[NUM_NL80211_BANDS];
	size_t len[NUM_NL80211_BANDS];
	const u8 *common_ies;
	size_t common_ie_len;
};



/* [LNX] include/net/mac80211.h:569 (record carried whole) */




/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:5882 */
struct sk_buff *ieee80211_beacon_get_tim(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 u16 *tim_offset, u16 *tim_length,
					 unsigned int link_id);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:7030 */
void ieee80211_beacon_loss(struct ieee80211_vif *vif);



/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:7046 */
void ieee80211_connection_loss(struct ieee80211_vif *vif);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:7105 */
void ieee80211_cqm_rssi_notify(struct ieee80211_vif *vif,
			       enum nl80211_cqm_rssi_threshold_event rssi_event,
			       s32 rssi_level,
			       gfp_t gfp);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:3448 */
void ieee80211_free_txskb(struct ieee80211_hw *hw, struct sk_buff *skb);

/* [LNX] include/net/mac80211.h names these too, but the interface defines them
 * inline beside the frame-control helpers they are built from; that is where
 * their bodies live in this face as well (include/linux/ieee80211.h):
 * ieee80211_get_sn, ieee80211_get_tid, ieee80211_has_pm,
 * ieee80211_is_any_nullfunc, ieee80211_is_probe_resp, ieee80211_is_pspoll,
 * ieee80211_is_trigger, ieee80211_sn_less, ieee80211_tu_to_usec. */

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:6944 */
void ieee80211_iter_keys_rcu(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     void (*iter)(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif,
					  struct ieee80211_sta *sta,
					  struct ieee80211_key_conf *key,
					  void *data),
			     void *iter_data);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:7815 */
struct ieee80211_txq *ieee80211_next_txq(struct ieee80211_hw *hw, u8 ac);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:6020 */
struct sk_buff *ieee80211_nullfunc_get(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       int link_id, bool qos_ok);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:6037 */
struct sk_buff *ieee80211_probereq_get(struct ieee80211_hw *hw,
				       const u8 *src_addr,
				       const u8 *ssid, size_t ssid_len,
				       size_t tailroom);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:5978 */
struct sk_buff *ieee80211_proberesp_get(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:5995 */
struct sk_buff *ieee80211_pspoll_get(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:3460 */
void ieee80211_purge_tx_queue(struct ieee80211_hw *hw,
			      struct sk_buff_head *skbs);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:6633 */
void ieee80211_queue_delayed_work(struct ieee80211_hw *hw,
				  struct delayed_work *dwork,
				  unsigned long delay);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:6621 */
void ieee80211_queue_work(struct ieee80211_hw *hw, struct work_struct *work);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:7168 */
void ieee80211_ready_on_channel(struct ieee80211_hw *hw);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:7174 */
void ieee80211_remain_on_channel_expired(struct ieee80211_hw *hw);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:5375 */
void ieee80211_restart_hw(struct ieee80211_hw *hw);



/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:5424 */
void ieee80211_rx_napi(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
		       struct sk_buff *skb, struct napi_struct *napi);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:6397 */
void ieee80211_scan_completed(struct ieee80211_hw *hw,
			      struct cfg80211_scan_info *info);



/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:8108 */
int ieee80211_set_active_links(struct ieee80211_vif *vif, u16 active_links);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:5505 */
int ieee80211_sta_ps_transition(struct ieee80211_sta *sta, bool start);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:5543 */
void ieee80211_sta_pspoll(struct ieee80211_sta *sta);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:6849 */
void ieee80211_sta_recalc_aggregates(struct ieee80211_sta *pubsta);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:5561 */
void ieee80211_sta_uapsd_trigger(struct ieee80211_sta *sta, u8 tid);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:6690 */
int ieee80211_stop_tx_ba_session(struct ieee80211_sta *sta, u16 tid);

struct sk_buff *ieee80211_tx_dequeue_ni(struct ieee80211_hw *hw,
						      struct ieee80211_txq *txq);



/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:7911 */
void ieee80211_txq_get_depth(struct ieee80211_txq *txq,
			     unsigned long *frame_cnt,
			     unsigned long *byte_cnt);

void ieee80211_txq_schedule_end(struct ieee80211_hw *hw, u8 ac);



/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:7826 */
void ieee80211_txq_schedule_start(struct ieee80211_hw *hw, u8 ac);

/*
 * [LNX] include/net/mac80211.h: `static inline bool ieee80211_vif_is_mld(const
 * struct ieee80211_vif *vif) { return vif->valid_links != 0; }` - a vif with no
 * valid links is not an MLD, which is what every non-MLO interface carries.
 */
static inline bool ieee80211_vif_is_mld(const struct ieee80211_vif *vif)
{
	return vif->valid_links != 0;
}

/*
 * [LNX] include/net/mac80211.h: `static inline enum nl80211_iftype
 * ieee80211_iftype_p2p(enum nl80211_iftype type, bool p2p)` - maps a base iftype to
 * its P2P counterpart, and the vif-shaped wrapper the 8821CU driver calls.
 */
static inline enum nl80211_iftype
ieee80211_iftype_p2p(enum nl80211_iftype type, bool p2p)
{
	if (p2p) {
		switch (type) {
		case NL80211_IFTYPE_STATION:
			return NL80211_IFTYPE_P2P_CLIENT;
		case NL80211_IFTYPE_AP:
			return NL80211_IFTYPE_P2P_GO;
		default:
			break;
		}
	}

	return type;
}

/* [LNX] include/net/mac80211.h:7487 */
static inline enum nl80211_iftype ieee80211_vif_type_p2p(struct ieee80211_vif *vif)
{
	return ieee80211_iftype_p2p(vif->type, vif->p2p);
}

/* [LNX] include/net/mac80211.h:7161 */
void ieee80211_request_smps(struct ieee80211_vif *vif, unsigned int link_id,
			    enum ieee80211_smps_mode smps_mode);

u16 ieee80211_vif_usable_links(const struct ieee80211_vif *vif);



/* [LNX] helper the compiled set names */
/* [LNX] include/net/mac80211.h:3385 */
struct ieee80211_hw *wiphy_to_ieee80211_hw(struct wiphy *wiphy);


/* [LNX] include/net/mac80211.h:224 (the enumerators the compiled set names, at their computed positions) */
enum ieee80211_chanctx_change {
	IEEE80211_CHANCTX_CHANGE_WIDTH = 1,
	IEEE80211_CHANCTX_CHANGE_PUNCTURING = 64,
	IEEE80211_CHANCTX_CHANGE_MIN_DEF = 16,
	IEEE80211_CHANCTX_CHANGE_AP = 32
};

/* [LNX] include/net/mac80211.h:4014 (the enumerators the compiled set names, at their computed positions) */
enum ieee80211_rate_control_changed {
	IEEE80211_RC_BW_CHANGED = 1,
	IEEE80211_RC_SUPP_RATES_CHANGED = 4,
	IEEE80211_RC_NSS_CHANGED = 8
};







/* [LNX] include/net/mac80211.h:2008 (the enumerators the compiled set names, at their computed positions) */
enum ieee80211_vif_flags {
	IEEE80211_VIF_BEACON_FILTER = 1,
	IEEE80211_VIF_SUPPORTS_CQM_RSSI = 2
};

/* [LNX] include/net/mac80211.h:1216 (the enumerators the compiled set names, at their computed positions) */
enum mac80211_rate_control_flags {
	IEEE80211_TX_RC_80_MHZ_WIDTH = 512,
	IEEE80211_TX_RC_160_MHZ_WIDTH = 1024,
	IEEE80211_TX_RC_VHT_MCS		= 256,
	IEEE80211_TX_RC_S1G_MCS		= IEEE80211_TX_RC_VHT_MCS
};

/* [LNX] include/net/mac80211.h:1651 (the enumerators the compiled set names, at their computed positions) */
enum mac80211_rx_flags {
	RX_FLAG_RADIOTAP_TLV_AT_END = 1048576,
	RX_FLAG_RADIOTAP_HE = 67108864,
	RX_FLAG_RADIOTAP_HE_MU = 134217728,
	RX_FLAG_RADIOTAP_VHT = 2147483648
};

/* [LNX] include/net/mac80211.h:1147 (the enumerators the compiled set names, at their computed positions) */
enum mac80211_tx_control_flags {
	IEEE80211_TX_CTRL_PORT_CTRL_PROTO = 1
};

/* [LNX] include/net/mac80211.h:3340-3345 (record carried whole) */
struct ieee80211_scan_request {
	struct ieee80211_scan_ies ies;

	/* Keep last */
	struct cfg80211_scan_request req;
};

/* [LNX] include/net/mac80211.h:6574-6586 */
struct ieee80211_sta *
__ieee80211_iterate_stations(struct ieee80211_hw *hw,
			     struct ieee80211_sta *prev);

#define for_each_station(sta, hw)					\
	for (sta = __ieee80211_iterate_stations(hw, NULL);		\
	     sta;							\
	     sta = __ieee80211_iterate_stations(hw, sta))

/* [LNX] include/net/mac80211.h:7765 */
struct sk_buff *ieee80211_tx_dequeue(struct ieee80211_hw *hw,
				     struct ieee80211_txq *txq);

/* [LNX] include/net/mac80211.h:7833 */
void __ieee80211_schedule_txq(struct ieee80211_hw *hw,
			      struct ieee80211_txq *txq, bool force);

/* [LNX] include/net/mac80211.h:7826,7833,7849,7865,7911, and the next/return pair the
 * engine exposes: the TXQ scheduling entry points the compiled set calls. Bodies belong
 * to the engine the port owns. */
void __ieee80211_schedule_txq(struct ieee80211_hw *hw, struct ieee80211_txq *txq,
			      bool force);
void ieee80211_schedule_txq(struct ieee80211_hw *hw, struct ieee80211_txq *txq);
void ieee80211_return_txq(struct ieee80211_hw *hw, struct ieee80211_txq *txq,
			  bool skip_empty);
struct ieee80211_txq *ieee80211_next_txq(struct ieee80211_hw *hw, u8 ac);
void ieee80211_txq_schedule_start(struct ieee80211_hw *hw, u8 ac);
void ieee80211_txq_schedule_end(struct ieee80211_hw *hw, u8 ac);
void ieee80211_txq_get_depth(struct ieee80211_txq *txq, unsigned long *sch,
			     unsigned long *frm);

/* [LNX] include/net/cfg80211.h: the band-record installer */
void _ieee80211_set_sband_iftype_data(struct ieee80211_supported_band *sband,
				      const struct ieee80211_sband_iftype_data *iftd,
				      u16 n_iftd);

#endif /* _LINUXKPI_NET_MAC80211_H_ */

