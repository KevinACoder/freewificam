/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/ieee80211.h
 * @brief   The 802.11 wire vocabulary (frame control, header layouts, management
 *          frames, information-element constants) as the Linux-family drivers and the
 *          mac80211 face name it.
 *
 * @design declaration-only compatibility face. Two worlds exist in this project: the
 *         imported NetBSD stack declares its own `struct ieee80211_frame` and friends
 *         (components/net80211/net80211/ieee80211_var.h), and the Linux drivers declare
 *         `struct ieee80211_hdr` and friends. They must never share a struct - the seam
 *         between them is the plain-C scalar vtable in ports/net80211/seam.h - so this
 *         header is deliberately a second, independent declaration of the wire format
 *         for the Linux side, not a bridge. Layouts and constants are transcribed from
 *         the interface facts (include/linux/ieee80211.h and its 7.3 splits
 *         ieee80211-ht.h / ieee80211-vht.h), never copied as text; the layout must be
 *         exact because these structures are cast onto bytes that reach the hardware.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 *         Subset: grown by the consumer - a second split file (e.g. the upstream
 *         ieee80211-ht.h) is added when a consumer includes it by that name.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_IEEE80211_H_
#define _LINUXKPI_LINUX_IEEE80211_H_

#include "../../linuxkpi_core.h"
#include <linux/if_ether.h>

/* ------------------------------------------------------------------ */
/* small helpers the layouts below need                                */
/* ------------------------------------------------------------------ */

/* End offset of a member, the interface's way of sizing a fixed prefix. */
#ifndef offsetofend
#define	offsetofend(type, member) \
	(offsetof(type, member) + sizeof_field(type, member))









#endif

/* [LNX] include/linux/ieee80211.h:41 */
#define	FCS_LEN	4

/* [LNX] include/linux/ieee80211.h:751,1789: the frame helpers the compiled set calls.
 * The interface defines them inline - they are one-line predicates over the frame
 * control field and the header-length walk - and they are inline here too, in the
 * block below the helpers they are built from (a call through a symbol would cost
 * more than the test it performs). */

/* [LNX] include/linux/ieee80211-he.h:93 (the enumerators the compiled set names, at their computed positions) */


enum ieee80211_he_mcs_support {
	IEEE80211_HE_MCS_SUPPORT_0_11 = 2,
	IEEE80211_HE_MCS_NOT_SUPPORTED = 3
};
/* [LNX] include/linux/ieee80211-he.h:604 (the enumerators the compiled set names, at their computed positions) */

enum ieee80211_tx_power_category_6ghz {
	IEEE80211_TPE_CAT_6GHZ_DEFAULT = 0
};
/* [LNX] include/linux/ieee80211-p2p.h:25 (the enumerators the compiled set names, at their computed positions) */
enum ieee80211_p2p_attr_id {
	IEEE80211_P2P_ATTR_ABSENCE_NOTICE = 12
};
/* [LNX] include/linux/ieee80211-vht.h:55 (the enumerators the compiled set names, at their computed positions) */
enum ieee80211_vht_max_ampdu_length_exp {
	IEEE80211_VHT_MAX_AMPDU_1024K = 7
};
/* [LNX] include/linux/ieee80211-vht.h:112 (the enumerators the compiled set names, at their computed positions) */
enum ieee80211_vht_mcs_support {
	IEEE80211_VHT_MCS_SUPPORT_0_7 = 0,
	IEEE80211_VHT_MCS_SUPPORT_0_8 = 1,
	IEEE80211_VHT_MCS_SUPPORT_0_9 = 2,
	IEEE80211_VHT_MCS_NOT_SUPPORTED = 3
};
/* [LNX] include/linux/ieee80211.h:1843 (the enumerators the compiled set names, at their computed positions) */

enum ieee80211_category {
	WLAN_CATEGORY_SA_QUERY = 8
};
/* [LNX] include/linux/ieee80211.h:1592 (the enumerators the compiled set names, at their computed positions) */




enum ieee80211_eid {
	WLAN_EID_SSID = 0,
	WLAN_EID_SUPP_RATES = 1,
	WLAN_EID_EXT_CAPABILITY = 127,
	WLAN_EID_VENDOR_SPECIFIC = 221
};
/* [LNX] include/linux/ieee80211.h:2188 (the enumerators the compiled set names, at their computed positions) */

enum ieee80211_sa_query_action {
	WLAN_ACTION_SA_QUERY_RESPONSE = 1
};

/*
 * An anonymous struct whose only content is a flexible array. The interface uses
 * the same construct (DECLARE_FLEX_ARRAY) because a bare flexible array member
 * inside a union is not valid C; carrying the payload this way keeps the union
 * member addressable by its own name without relying on a compiler extension.
 */
#define	IEEE80211_FLEX_ARRAY(type, name)	\
	struct {				\
		struct { } __empty;		\
		type name[];			\
	}

/* ------------------------------------------------------------------ */
/* frame control field                                                 */
/* ------------------------------------------------------------------ */

#define	IEEE80211_FCTL_FTYPE		0x000c
#define	IEEE80211_FCTL_TODS		0x0100
#define	IEEE80211_FCTL_FROMDS		0x0200
#define	IEEE80211_FCTL_MOREFRAGS	0x0400
#define	IEEE80211_FCTL_RETRY		0x0800
#define	IEEE80211_FCTL_MOREDATA		0x2000
#define	IEEE80211_FCTL_ORDER		0x8000

#define	IEEE80211_FTYPE_MGMT		0x0000
#define	IEEE80211_FTYPE_CTL		0x0004
#define	IEEE80211_FTYPE_DATA		0x0008
#define	IEEE80211_FTYPE_EXT		0x000c

/* Subtypes this project reaches for; the rest arrive with their consumers. */
#define	IEEE80211_STYPE_ASSOC_REQ	0x0000
#define	IEEE80211_STYPE_PROBE_REQ	0x0040
#define	IEEE80211_STYPE_PROBE_RESP	0x0050
#define	IEEE80211_STYPE_BEACON		0x0080
#define	IEEE80211_STYPE_QOS_DATA	0x0080
#define	IEEE80211_STYPE_AUTH		0x00b0
#define	IEEE80211_STYPE_ACTION		0x00d0

/* ------------------------------------------------------------------ */
/* QoS control field                                                   */
/* ------------------------------------------------------------------ */

#define	IEEE80211_QOS_CTL_LEN		2
#define	IEEE80211_QOS_CTL_TID_MASK	0x000f
#define	IEEE80211_QOS_CTL_ACK_POLICY_MASK	0x0060
#define	IEEE80211_QOS_CTL_ACK_POLICY_NOACK	0x0020

/* ------------------------------------------------------------------ */
/* sequence control                                                    */
/* ------------------------------------------------------------------ */

#define	IEEE80211_SCTL_SEQ		0xfff0
#define	IEEE80211_SEQ_TO_SN(seq)	(((seq) & IEEE80211_SCTL_SEQ) >> 4)

/* ------------------------------------------------------------------ */
/* sizes                                                               */
/* ------------------------------------------------------------------ */

#define	IEEE80211_MAX_DATA_LEN		2304	/* bytes in a frame body */
/* [LNX] include/linux/ieee80211.h:227: the SSID field's ceiling. It sizes the SSID
 * members of the records the configuration face carries by value, so it is a layout
 * value, not a decoration. */
#define	IEEE80211_MAX_SSID_LEN		32
#define	IEEE80211_MAX_FRAME_LEN		2352	/* max 802.11 frame, no FCS */
#define	IEEE80211_NUM_TIDS		16	/* traffic classes */
#define	IEEE80211_NUM_ACS		4	/* access categories */
#define	IEEE80211_MAX_CHAINS		4	/* RX chains reportable at once */

/* ------------------------------------------------------------------ */
/* unit conversions                                                    */
/* ------------------------------------------------------------------ */

/* [LNX] include/linux/ieee80211.h:2624-2638: the frequency, time-unit and power
 * conversions the interface groups here. The compiled set names MHZ_TO_KHZ (a regulatory
 * rule table is written in kHz), MBM_TO_DBM (a power report is converted back to dBm)
 * and the time-unit pair, whose second macro is spelled over the first; the rest of the
 * interface's blocks are not named by a compiled unit and arrive with a caller, each
 * with the interface's definition. */
#define	TU_TO_JIFFIES(x)	(usecs_to_jiffies((x) * 1024))
#define	TU_TO_EXP_TIME(x)	(jiffies + TU_TO_JIFFIES(x))

#define	MHZ_TO_KHZ(freq)	((freq) * 1000)
#define	MBM_TO_DBM(gain)	((gain) / 100)

/* ------------------------------------------------------------------ */
/* header layouts                                                      */
/* ------------------------------------------------------------------ */

struct ieee80211_hdr {
	__le16	frame_control;
	__le16	duration_id;
	u8	addr1[ETH_ALEN];
	u8	addr2[ETH_ALEN];
	u8	addr3[ETH_ALEN];
	__le16	seq_ctrl;
	u8	addr4[ETH_ALEN];
} __packed __aligned(2);

struct ieee80211_qos_hdr {
	__le16	frame_control;
	__le16	duration_id;
	u8	addr1[ETH_ALEN];
	u8	addr2[ETH_ALEN];
	u8	addr3[ETH_ALEN];
	__le16	seq_ctrl;
	__le16	qos_ctrl;
} __packed __aligned(2);

struct ieee80211_qos_hdr_4addr {
	__le16	frame_control;
	__le16	duration_id;
	u8	addr1[ETH_ALEN];
	u8	addr2[ETH_ALEN];
	u8	addr3[ETH_ALEN];
	__le16	seq_ctrl;
	u8	addr4[ETH_ALEN];
	__le16	qos_ctrl;
} __packed __aligned(2);

struct ieee80211_mgmt {
	__le16	frame_control;
	__le16	duration;
	u8	da[ETH_ALEN];
	u8	sa[ETH_ALEN];
	u8	bssid[ETH_ALEN];
	__le16	seq_ctrl;
	union {
		struct {
			__le16	auth_alg;
			__le16	auth_transaction;
			__le16	status_code;
			u8	variable[];
		} __packed auth;
		struct {
			__le16	reason_code;
		} __packed deauth;
		struct {
			__le16	reason_code;
		} __packed disassoc;
		struct {
			__le16	capab_info;
			__le16	listen_interval;
			u8	variable[];
		} __packed assoc_req;
		struct {
			__le16	capab_info;
			__le16	status_code;
			__le16	aid;
			u8	variable[];
		} __packed assoc_resp, reassoc_resp;
		struct {
			__le16	capab_info;
			__le16	listen_interval;
			u8	current_ap[ETH_ALEN];
			u8	variable[];
		} __packed reassoc_req;
		struct {
			__le64	timestamp;
			__le16	beacon_int;
			__le16	capab_info;
			u8	variable[];
		} __packed beacon, probe_resp;
		IEEE80211_FLEX_ARRAY(u8, probe_req) __packed;
		struct {
			u8	category;
			u8	action_code;
			union {
				struct {
					u8	dialog_token;
					__le16	capab;
					__le16	timeout;
					__le16	start_seq_num;
					u8	variable[];
				} __packed addba_req;
				struct {
					u8	dialog_token;
					__le16	status;
					__le16	capab;
					__le16	timeout;
					u8	variable[];
				} __packed addba_resp;
				struct {
					__le16	params;
					__le16	reason_code;
				} __packed delba;
			};
		} __packed action;
	} u;
} __packed __aligned(2);

/*
 * The fixed prefix of an action frame: everything up to and including the action
 * code. Callers bound-check the frame against this before reading the variant
 * body, which is what the interface's macro of the same name is for.
 */
#define	IEEE80211_MIN_ACTION_SIZE(type) \
	offsetofend(struct ieee80211_mgmt, u.action.type)

/* ------------------------------------------------------------------ */
/* high-throughput capability vocabulary                               */
/* ------------------------------------------------------------------ */

#define	IEEE80211_HT_MCS_MASK_LEN	10

#define	IEEE80211_HT_CAP_LDPC_CODING		0x0001
#define	IEEE80211_HT_CAP_SUP_WIDTH_20_40	0x0002
#define	IEEE80211_HT_CAP_SM_PS			0x000C
#define	IEEE80211_HT_CAP_GRN_FLD		0x0010
#define	IEEE80211_HT_CAP_SGI_20			0x0020
#define	IEEE80211_HT_CAP_SGI_40			0x0040
#define	IEEE80211_HT_CAP_TX_STBC		0x0080
#define	IEEE80211_HT_CAP_RX_STBC		0x0300
#define	IEEE80211_HT_CAP_DELAY_BA		0x0400
#define	IEEE80211_HT_CAP_MAX_AMSDU		0x0800
#define	IEEE80211_HT_CAP_DSSSCCK40		0x1000
#define	IEEE80211_HT_CAP_40MHZ_INTOLERANT	0x4000
#define	IEEE80211_HT_CAP_LSIG_TXOP_PROT		0x8000

#define	IEEE80211_HT_MCS_TX_DEFINED		0x01
#define	IEEE80211_HT_MCS_TX_RX_DIFF		0x02
#define	IEEE80211_HT_MCS_TX_MAX_STREAMS_MASK	0x0c
#define	IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT	2
#define	IEEE80211_HT_MCS_TX_MAX_STREAMS		4
#define	IEEE80211_HT_MCS_TX_UNEQUAL_MODULATION	0x10

#define	IEEE80211_HT_MCS_RX_HIGHEST_MASK	0x3ff

struct ieee80211_mcs_info {
	u8	rx_mask[IEEE80211_HT_MCS_MASK_LEN];
	__le16	rx_highest;
	u8	tx_params;
	u8	reserved[3];
} __packed;

struct ieee80211_vht_mcs_info {
	__le16	rx_mcs_map;
	__le16	rx_highest;
	__le16	tx_mcs_map;
	__le16	tx_highest;
} __packed;

/*
 * The A-MPDU parameters are fields of the HT capability element: the maximum
 * length factor and the minimum MPDU start spacing, both as the element encodes
 * them (the values a driver writes into its own descriptor tables).
 */
enum ieee80211_ht_max_ampdu {
	IEEE80211_HT_MAX_AMPDU_8K = 0,
	IEEE80211_HT_MAX_AMPDU_16K = 1,
	IEEE80211_HT_MAX_AMPDU_32K = 2,
	IEEE80211_HT_MAX_AMPDU_64K = 3,
};

enum ieee80211_ht_mpdu_density {
	IEEE80211_HT_MPDU_DENSITY_NONE = 0,
	IEEE80211_HT_MPDU_DENSITY_0_25 = 1,
	IEEE80211_HT_MPDU_DENSITY_0_5 = 2,
	IEEE80211_HT_MPDU_DENSITY_1 = 3,
	IEEE80211_HT_MPDU_DENSITY_2 = 4,
	IEEE80211_HT_MPDU_DENSITY_4 = 5,
	IEEE80211_HT_MPDU_DENSITY_8 = 6,
	IEEE80211_HT_MPDU_DENSITY_16 = 7,
};

/* ------------------------------------------------------------------ */
/* management-frame actions                                            */
/* ------------------------------------------------------------------ */

/* Block-ack (back) action codes, category WLAN_CATEGORY_BACK. */
#define	WLAN_ACTION_ADDBA_REQ		0
#define	WLAN_ACTION_ADDBA_RESP		1
#define	WLAN_ACTION_DELBA		2

/* The block-ack action's parameters are two 16-bit fields inside a fixed
 * prefix, as the interface encodes them. */
#define	IEEE80211_ADDBA_PARAM_TID_MASK		0x003C
#define	IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK	0xffc0

/* ------------------------------------------------------------------ */
/* cipher suites                                                       */
/* ------------------------------------------------------------------ */

/* OUI 00-0f-ac, the IEEE 802.11 selector space. */
#define	SUITE(oui, id)		(((oui) << 8) | (id))

#define	WLAN_CIPHER_SUITE_USE_GROUP	SUITE(0x000fac, 0)
#define	WLAN_CIPHER_SUITE_WEP40		SUITE(0x000fac, 1)
#define	WLAN_CIPHER_SUITE_TKIP		SUITE(0x000fac, 2)
#define	WLAN_CIPHER_SUITE_CCMP		SUITE(0x000fac, 4)
#define	WLAN_CIPHER_SUITE_WEP104	SUITE(0x000fac, 5)
#define	WLAN_CIPHER_SUITE_AES_CMAC	SUITE(0x000fac, 6)
#define	WLAN_CIPHER_SUITE_BIP_GMAC_128	SUITE(0x000fac, 11)
#define	WLAN_CIPHER_SUITE_BIP_GMAC_256	SUITE(0x000fac, 12)
#define	WLAN_CIPHER_SUITE_BIP_CMAC_256	SUITE(0x000fac, 13)

/* ------------------------------------------------------------------ */
/* frame predicates                                                    */
/* ------------------------------------------------------------------ */

static inline bool ieee80211_has_tods(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_TODS)) != 0;
}

static inline bool ieee80211_has_fromds(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FROMDS)) != 0;
}

static inline bool ieee80211_has_a4(__le16 fc)
{
	__le16 tmp = cpu_to_le16(IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS);

	return (fc & tmp) == tmp;
}

static inline bool ieee80211_is_mgmt(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT);
}

static inline bool ieee80211_is_ctl(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL);
}

static inline bool ieee80211_is_data(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_DATA);
}

static inline bool ieee80211_is_data_qos(__le16 fc)
{
	/* Masking with the QoS-data subtype bit alone is enough: only a QoS
	 * data frame has it set inside the data type. */
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_STYPE_QOS_DATA)) ==
	       cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_DATA);
}

static inline bool ieee80211_is_beacon(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | 0x00f0)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACON);
}

static inline bool ieee80211_is_action(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | 0x00f0)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ACTION);
}

/* ------------------------------------------------------------------ */
/* frame helpers                                                       */
/* ------------------------------------------------------------------ */

static inline u8 *ieee80211_get_DA(struct ieee80211_hdr *hdr)
{
	return hdr->addr1;
}

/*
 * The QoS control field sits right after the addresses: after addr3 in a
 * three-address frame, after addr4 in a four-address frame. Which one applies is
 * decided by the frame control, not by the caller.
 */
static inline u8 *ieee80211_get_qos_ctl(struct ieee80211_hdr *hdr)
{
	union {
		struct ieee80211_qos_hdr		addr3;
		struct ieee80211_qos_hdr_4addr	addr4;
	} *qos;

	qos = (void *)hdr;
	if (ieee80211_has_a4(qos->addr3.frame_control))
		return (u8 *)&qos->addr4.qos_ctrl;
	else
		return (u8 *)&qos->addr3.qos_ctrl;
}

/* [LNX] include/linux/ieee80211.h:309 (record carried whole) */
struct ieee80211_hdr_3addr {
	__le16 frame_control;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctrl;
} __packed __aligned(2);

/* [LNX] include/linux/ieee80211.h:339 (record carried whole) */
struct ieee80211_trigger {
	__le16 frame_control;
	__le16 duration;
	u8 ra[ETH_ALEN];
	u8 ta[ETH_ALEN];
	__le64 common_info;
	u8 variable[];
} __packed __aligned(2);

/* [LNX] include/linux/ieee80211.h:1898-1911 */
enum ieee80211_key_len {
	WLAN_KEY_LEN_WEP40 = 5,
	WLAN_KEY_LEN_WEP104 = 13,
	WLAN_KEY_LEN_CCMP = 16,
	WLAN_KEY_LEN_CCMP_256 = 32,
	WLAN_KEY_LEN_TKIP = 32,
	WLAN_KEY_LEN_AES_CMAC = 16,
	WLAN_KEY_LEN_SMS4 = 32,
	WLAN_KEY_LEN_GCMP = 16,
	WLAN_KEY_LEN_GCMP_256 = 32,
	WLAN_KEY_LEN_BIP_CMAC_256 = 32,
	WLAN_KEY_LEN_BIP_GMAC_128 = 16,
	WLAN_KEY_LEN_BIP_GMAC_256 = 32,
};

/* [LNX] include/linux/ieee80211-he.h:138 (record carried whole) */
struct ieee80211_he_operation {
	__le32 he_oper_params;
	__le16 he_mcs_nss_set;
	u8 optional[];
} __packed;

/* [LNX] include/linux/ieee80211-p2p.h:55 (record carried whole) */
struct ieee80211_p2p_noa_desc {
	u8 count;
	__le32 duration;
	__le32 interval;
	__le32 start_time;
} __packed;

/* [LNX] include/linux/ieee80211-p2p.h:62 (record carried whole) */
/* [LNX] include/linux/ieee80211-p2p.h:53 */
#define	IEEE80211_P2P_NOA_DESC_MAX	4

struct ieee80211_p2p_noa_attr {
	u8 index;
	u8 oppps_ctwindow;
	struct ieee80211_p2p_noa_desc desc[IEEE80211_P2P_NOA_DESC_MAX];
} __packed;

/* [LNX] include/linux/ieee80211.h:2275 */
/* [LNX] include/linux/ieee80211.h:2264 */
#define WLAN_AKM_SUITE_WFA_DPP			SUITE(WLAN_OUI_WFA, 2)

#define	WLAN_OUI_WFA			0x506f9a

/* [LNX] include/linux/ieee80211.h:935-936 */
#define	WLAN_MEMBERSHIP_LEN	8
#define	WLAN_USER_POSITION_LEN	16

/* [LNX] include/linux/ieee80211-vht.h:195-196 */
#define IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT	23
/* [LNX] include/linux/ieee80211-vht.h:196-197 */
#define IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK	\
		(7 << IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT)

/* [LNX] include/linux/ieee80211-eht.h:47 */
#define IEEE80211_EHT_MCS_NSS_RX 0x0f

/* [LNX] include/linux/ieee80211-eht.h:48 */
#define IEEE80211_EHT_MCS_NSS_TX 0xf0

/* [LNX] include/linux/ieee80211-eht.h:192 */
#define IEEE80211_EHT_MAC_CAP0_MAX_MPDU_LEN_MASK		0xc0

/* [LNX] include/linux/ieee80211-eht.h:205 */
#define IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ			0x02

/* [LNX] include/linux/ieee80211-eht.h:207 */
#define IEEE80211_EHT_PHY_CAP0_NDP_4_EHT_LFT_32_GI		0x08

/* [LNX] include/linux/ieee80211-eht.h:210 */
#define IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMEE			0x40

/* [LNX] include/linux/ieee80211-eht.h:213 */
#define IEEE80211_EHT_PHY_CAP0_BEAMFORMEE_SS_80MHZ_MASK		0x80

/* [LNX] include/linux/ieee80211-eht.h:214 */
#define IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_80MHZ_MASK		0x03

/* [LNX] include/linux/ieee80211-eht.h:216 */
#define IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_160MHZ_MASK	0x1c

/* [LNX] include/linux/ieee80211-eht.h:217 */
#define IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_320MHZ_MASK	0xe0

/* [LNX] include/linux/ieee80211-eht.h:227 */
#define IEEE80211_EHT_PHY_CAP3_CODEBOOK_4_2_SU_FDBK		0x08

/* [LNX] include/linux/ieee80211-eht.h:228 */
#define IEEE80211_EHT_PHY_CAP3_CODEBOOK_7_5_MU_FDBK		0x10

/* [LNX] include/linux/ieee80211-eht.h:229 */
#define IEEE80211_EHT_PHY_CAP3_TRIG_SU_BF_FDBK			0x20

/* [LNX] include/linux/ieee80211-eht.h:230 */
#define IEEE80211_EHT_PHY_CAP3_TRIG_MU_BF_PART_BW_FDBK		0x40

/* [LNX] include/linux/ieee80211-eht.h:235 */
#define IEEE80211_EHT_PHY_CAP4_POWER_BOOST_FACT_SUPP		0x04

/* [LNX] include/linux/ieee80211-eht.h:237 */
#define IEEE80211_EHT_PHY_CAP4_MAX_NC_MASK			0xf0

/* [LNX] include/linux/ieee80211-eht.h:242 */
#define IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT		0x08

/* [LNX] include/linux/ieee80211-eht.h:243 */
#define IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_MASK	0x30

/* [LNX] include/linux/ieee80211-eht.h:247 */
#define   IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_20US	3

/* [LNX] include/linux/ieee80211-eht.h:316 */
#define IEEE80211_EHT_PPE_THRES_RU_INDEX_BITMASK_MASK	0x1f0

/* [LNX] include/linux/ieee80211-eht.h:317 */
#define IEEE80211_EHT_PPE_THRES_INFO_PPET_SIZE		3

/* [LNX] include/linux/ieee80211-eht.h:318 */
#define IEEE80211_EHT_PPE_THRES_INFO_HEADER_SIZE	9

/* [LNX] include/linux/ieee80211-eht.h:505 */
#define  IEEE80211_EML_CAP_EML_PADDING_DELAY_256US		4

/* [LNX] include/linux/ieee80211-eht.h:513 */
#define  IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY_256US		5

/* [LNX] include/linux/ieee80211-he.h:195 */
#define IEEE80211_HE_MAC_CAP0_HTC_HE				0x01

/* [LNX] include/linux/ieee80211-he.h:220 */
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US		0x08

/* [LNX] include/linux/ieee80211-he.h:243 */
#define IEEE80211_HE_MAC_CAP2_ALL_ACK				0x02

/* [LNX] include/linux/ieee80211-he.h:245 */
#define IEEE80211_HE_MAC_CAP2_BSR				0x08

/* [LNX] include/linux/ieee80211-he.h:251 */
#define IEEE80211_HE_MAC_CAP3_OMI_CONTROL			0x02

/* [LNX] include/linux/ieee80211-he.h:260 */
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_2		0x10

/* [LNX] include/linux/ieee80211-he.h:272 */
#define IEEE80211_HE_MAC_CAP4_OPS				0x20

/* [LNX] include/linux/ieee80211-he.h:273 */
#define IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU			0x40

/* [LNX] include/linux/ieee80211-he.h:286 */
#define IEEE80211_HE_MAC_CAP5_HT_VHT_TRIG_FRAME_RX		0x80

/* [LNX] include/linux/ieee80211-he.h:293 */
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G		0x02

/* [LNX] include/linux/ieee80211-he.h:294 */
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G	0x04

/* [LNX] include/linux/ieee80211-he.h:295 */
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G		0x08

/* [LNX] include/linux/ieee80211-he.h:296 */
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G	0x10

/* [LNX] include/linux/ieee80211-he.h:297 */
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_MASK_ALL		0x1e

/* [LNX] include/linux/ieee80211-he.h:308 */
#define IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A				0x10

/* [LNX] include/linux/ieee80211-he.h:309 */
#define IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD			0x20

/* [LNX] include/linux/ieee80211-he.h:310 */
#define IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US		0x40

/* [LNX] include/linux/ieee80211-he.h:315 */
#define IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US			0x02

/* [LNX] include/linux/ieee80211-he.h:316 */
#define IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ			0x04

/* [LNX] include/linux/ieee80211-he.h:317 */
#define IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ			0x08

/* [LNX] include/linux/ieee80211-he.h:318 */
#define IEEE80211_HE_PHY_CAP2_DOPPLER_TX				0x10

/* [LNX] include/linux/ieee80211-he.h:331 */
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_16_QAM			0x03

/* [LNX] include/linux/ieee80211-he.h:334 */
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_2				0x04

/* [LNX] include/linux/ieee80211-he.h:338 */
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM			0x18

/* [LNX] include/linux/ieee80211-he.h:342 */
#define IEEE80211_HE_PHY_CAP3_RX_PARTIAL_BW_SU_IN_20MHZ_MU		0x40

/* [LNX] include/linux/ieee80211-he.h:343 */
#define IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER				0x80

/* [LNX] include/linux/ieee80211-he.h:345 */
#define IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE				0x01

/* [LNX] include/linux/ieee80211-he.h:346 */
#define IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER				0x02

/* [LNX] include/linux/ieee80211-he.h:349 */
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4		0x0c

/* [LNX] include/linux/ieee80211-he.h:357 */
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_4		0x60

/* [LNX] include/linux/ieee80211-he.h:372 */
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK	0x07

/* [LNX] include/linux/ieee80211-he.h:384 */
#define IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK				0x40

/* [LNX] include/linux/ieee80211-he.h:385 */
#define IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK				0x80

/* [LNX] include/linux/ieee80211-he.h:387 */
#define IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU			0x01

/* [LNX] include/linux/ieee80211-he.h:388 */
#define IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU			0x02

/* [LNX] include/linux/ieee80211-he.h:389 */
#define IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB			0x04

/* [LNX] include/linux/ieee80211-he.h:392 */
#define IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE			0x20

/* [LNX] include/linux/ieee80211-he.h:394 */
#define IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT			0x80

/* [LNX] include/linux/ieee80211-he.h:397 */
#define IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_SUPP			0x02

/* [LNX] include/linux/ieee80211-he.h:398 */
#define IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI		0x04

/* [LNX] include/linux/ieee80211-he.h:399 */
#define IEEE80211_HE_PHY_CAP7_MAX_NC_1					0x08

/* [LNX] include/linux/ieee80211-he.h:410 */
#define IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI		0x01

/* [LNX] include/linux/ieee80211-he.h:412 */
#define IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU			0x04

/* [LNX] include/linux/ieee80211-he.h:413 */
#define IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU			0x08

/* [LNX] include/linux/ieee80211-he.h:414 */
#define IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI		0x10

/* [LNX] include/linux/ieee80211-he.h:418 */
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_996				0x80

/* [LNX] include/linux/ieee80211-he.h:422 */
#define IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM		0x01

/* [LNX] include/linux/ieee80211-he.h:424 */
#define IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU		0x04

/* [LNX] include/linux/ieee80211-he.h:425 */
#define IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU		0x08

/* [LNX] include/linux/ieee80211-he.h:426 */
#define IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB	0x10

/* [LNX] include/linux/ieee80211-he.h:427 */
#define IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB	0x20

/* [LNX] include/linux/ieee80211-he.h:430 */
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_16US			0x2

/* [LNX] include/linux/ieee80211-he.h:433 */
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_MASK			0xc0

/* [LNX] include/linux/ieee80211-he.h:473 */
#define IEEE80211_PPE_THRES_NSS_MASK				(7)

/* [LNX] include/linux/ieee80211-he.h:476 */
#define IEEE80211_PPE_THRES_RU_INDEX_BITMASK_MASK		0x78

/* [LNX] include/linux/ieee80211-he.h:478 */
#define IEEE80211_PPE_THRES_INFO_PPET_SIZE			(3)

/* [LNX] include/linux/ieee80211-he.h:539 */
#define IEEE80211_HE_OPERATION_ER_SU_DISABLE			0x00010000

/* [LNX] include/linux/ieee80211-he.h:814 */
#define IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START	0x0007

/* [LNX] include/linux/ieee80211-he.h:816 */
#define IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP	0x0038

/* [LNX] include/linux/ieee80211-he.h:818 */
#define IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN	0x00c0

/* [LNX] include/linux/ieee80211-ht.h:28 */
#define IEEE80211_HT_CTL_LEN		4

/* [LNX] include/linux/ieee80211-ht.h:142 */
#define		IEEE80211_HT_CAP_RX_STBC_SHIFT	8

/* [LNX] include/linux/ieee80211-p2p.h:68 */
#define IEEE80211_P2P_OPPPS_ENABLE_BIT		BIT(7)

/* [LNX] include/linux/ieee80211-p2p.h:69 */
#define IEEE80211_P2P_OPPPS_CTWINDOW_MASK	0x7F

/* [LNX] include/linux/ieee80211-vht.h:96 */
#define IEEE80211_VHT_EXT_NSS_BW_CAPABLE	(1 << 13)

/* [LNX] include/linux/ieee80211-vht.h:167 */
#define IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454			0x00000002

/* [LNX] include/linux/ieee80211-vht.h:21-23 (the lengths the compiled set names) */
#define IEEE80211_MAX_MPDU_LEN_VHT_3895				3895
#define IEEE80211_MAX_MPDU_LEN_VHT_7991				7991
#define IEEE80211_MAX_MPDU_LEN_VHT_11454			11454

/* [LNX] include/linux/ieee80211-vht.h:169 */
#define IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ		0x00000004

/* [LNX] include/linux/ieee80211-vht.h:173 */
#define IEEE80211_VHT_CAP_RXLDPC				0x00000010

/* [LNX] include/linux/ieee80211-vht.h:174 */
#define IEEE80211_VHT_CAP_SHORT_GI_80				0x00000020

/* [LNX] include/linux/ieee80211-vht.h:175 */
#define IEEE80211_VHT_CAP_SHORT_GI_160				0x00000040

/* [LNX] include/linux/ieee80211-vht.h:176 */
#define IEEE80211_VHT_CAP_TXSTBC				0x00000080

/* [LNX] include/linux/ieee80211-vht.h:177 */
#define IEEE80211_VHT_CAP_RXSTBC_1				0x00000100

/* [LNX] include/linux/ieee80211-vht.h:181 */
#define IEEE80211_VHT_CAP_RXSTBC_MASK				0x00000700

/* [LNX] include/linux/ieee80211-vht.h:183 */
#define IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE			0x00000800

/* [LNX] include/linux/ieee80211-vht.h:184 */
#define IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE			0x00001000

/* [LNX] include/linux/ieee80211-vht.h:185 */
#define IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT                  13

/* [LNX] include/linux/ieee80211-vht.h:189 */
#define IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT	16
#define IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK		\
		(7 << IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT)

/* [LNX] include/linux/ieee80211-vht.h:191 */
#define IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE			0x00080000

/* [LNX] include/linux/ieee80211-vht.h:192 */
#define IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE			0x00100000

/* [LNX] include/linux/ieee80211-vht.h:194 */
#define IEEE80211_VHT_CAP_HTC_VHT				0x00400000

/* [LNX] include/linux/ieee80211.h:51 */
#define IEEE80211_FCTL_PM		0x1000

/* [LNX] include/linux/ieee80211.h:53 */
#define IEEE80211_FCTL_PROTECTED	0x4000

/* [LNX] include/linux/ieee80211.h:92 */
#define IEEE80211_STYPE_DATA			0x0000
/* [LNX] include/linux/ieee80211.h:95,103: the two null-function subtypes (the
 * data-frame subtypes the interface names beside IEEE80211_STYPE_DATA) */
#define IEEE80211_STYPE_NULLFUNC		0x0040
#define IEEE80211_STYPE_QOS_NULLFUNC		0x00c0

/* [LNX] include/linux/ieee80211.h:159 */
#define IEEE80211_SN_MASK		((IEEE80211_SCTL_SEQ) >> 4)

/* [LNX] include/linux/ieee80211.h:239 */
#define IEEE80211_QOS_CTL_TAG1D_MASK		0x0007

/* [LNX] include/linux/ieee80211.h:243 */
#define IEEE80211_QOS_CTL_EOSP			0x0010

/* [LNX] include/linux/ieee80211.h:272 */
#define IEEE80211_WMM_IE_STA_QOSINFO_SP_ALL	0x00

/* [LNX] include/linux/ieee80211.h:280 */
#define IEEE80211_TRIGGER_TYPE_MASK		0xf

/* [LNX] include/linux/ieee80211.h:281 */
#define IEEE80211_TRIGGER_TYPE_BASIC		0x0

/* [LNX] include/linux/ieee80211.h:283 */
#define IEEE80211_TRIGGER_TYPE_MU_BAR		0x2

/* [LNX] include/linux/ieee80211.h:291 */
#define IEEE80211_TRIGGER_ULBW_MASK		0xc0000

/* [LNX] include/linux/ieee80211.h:295 */
#define IEEE80211_TRIGGER_ULBW_160_80P80MHZ	0x3

/* [LNX] include/linux/ieee80211.h:2014 */
#define WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING	BIT(2)

/* [LNX] include/linux/ieee80211.h:2019 */
#define WLAN_EXT_CAPA3_MULTI_BSSID_SUPPORT	BIT(6)

/* [LNX] include/linux/ieee80211.h:2045 */
#define WLAN_EXT_CAPA8_OPMODE_NOTIF	BIT(6)

/* [LNX] include/linux/ieee80211.h:2066 */
#define WLAN_EXT_CAPA10_OBSS_NARROW_BW_RU_TOLERANCE_SUPPORT BIT(7)

/* [LNX] include/linux/ieee80211.h:2233 */
#define WLAN_CIPHER_SUITE_GCMP		SUITE(0x000FAC, 8)

/* [LNX] include/linux/ieee80211.h:2234 */
#define WLAN_CIPHER_SUITE_GCMP_256	SUITE(0x000FAC, 9)

/* [LNX] include/linux/ieee80211.h:2235 */
#define WLAN_CIPHER_SUITE_CCMP_256	SUITE(0x000FAC, 10)

/* [LNX] include/linux/ieee80211.h:2276 */
#define WLAN_OUI_TYPE_WFA_P2P		9

/* [LNX] include/linux/ieee80211.h:45 */
#define	IEEE80211_FCTL_STYPE		0x00f0

/* [LNX] include/linux/ieee80211.h:161 */
/* [LNX] include/linux/ieee80211.h:160 */
#define IEEE80211_MAX_SN		IEEE80211_SN_MASK

#define	IEEE80211_SN_MODULO		(IEEE80211_MAX_SN + 1)

/* [LNX] include/linux/ieee80211.h */
#define	IEEE80211_STYPE_TRIGGER	0x0020

/* [LNX] include/linux/ieee80211.h:84 */
#define IEEE80211_STYPE_PSPOLL		0x00A0

/* [LNX] include/linux/ieee80211.h:2727-2839 (record carried whole) */
struct element {
	u8 id;
	u8 datalen;
	u8 data[];
} __packed;

/* element iteration helpers */
#define for_each_element(_elem, _data, _datalen)			\
	for (_elem = (const struct element *)(_data);			\
	     (const u8 *)(_data) + (_datalen) - (const u8 *)_elem >=	\
		(int)sizeof(*_elem) &&					\
	     (const u8 *)(_data) + (_datalen) - (const u8 *)_elem >=	\
		(int)sizeof(*_elem) + _elem->datalen;			\
	     _elem = (const struct element *)(_elem->data + _elem->datalen))

#define for_each_element_id(element, _id, data, datalen)		\
	for_each_element(element, data, datalen)			\
		if (element->id == (_id))

#define for_each_element_extid(element, extid, _data, _datalen)		\
	for_each_element(element, _data, _datalen)			\
		if (element->id == WLAN_EID_EXTENSION &&		\
		    element->datalen > 0 &&				\
		    element->data[0] == (extid))

#define for_each_subelement(sub, element)				\
	for_each_element(sub, (element)->data, (element)->datalen)

#define for_each_subelement_id(sub, id, element)			\
	for_each_element_id(sub, id, (element)->data, (element)->datalen)

#define for_each_subelement_extid(sub, extid, element)			\
	for_each_element_extid(sub, extid, (element)->data, (element)->datalen)

/**
 * for_each_element_completed - determine if element parsing consumed all data
 * @element: element pointer after for_each_element() or friends
 * @data: same data pointer as passed to for_each_element() or friends
 * @datalen: same data length as passed to for_each_element() or friends
 * Return: %true if all elements were iterated, %false otherwise; see notes
 *
 * This function returns %true if all the data was parsed or considered
 * while walking the elements. Only use this if your for_each_element()
 * loop cannot be broken out of, otherwise it always returns %false.
 *
 * If some data was malformed, this returns %false since the last parsed
 * element will not fill the whole remaining data.
 */
static inline bool for_each_element_completed(const struct element *element,
					      const void *data, size_t datalen)
{
	return (const u8 *)element == (const u8 *)data + datalen;
}

/*
 * RSNX Capabilities:
 * bits 0-3: Field length (n-1)
 */
#define WLAN_RSNX_CAPA_PROTECTED_TWT BIT(4)
#define WLAN_RSNX_CAPA_SAE_H2E BIT(5)

/* EBPCC = Enhanced BSS Parameter Change Count */
#define IEEE80211_ENH_CRIT_UPD_EBPCC		0x0F
#define IEEE80211_ENH_CRIT_UPD_TYPE		0x70
#define IEEE80211_ENH_CRIT_UPD_TYPE_NO_UHR	0
#define IEEE80211_ENH_CRIT_UPD_TYPE_UHR		1
#define IEEE80211_ENH_CRIT_UPD_ALL		0x80

/**
 * struct ieee80211_enh_crit_upd - enhanced critical update (UHR)
 * @v: value of the enhanced critical update data,
 *	see %IEEE80211_ENH_CRIT_UPD_* to parse the bits
 */
struct ieee80211_enh_crit_upd {
	u8 v;
} __packed;

/*
 * reduced neighbor report, based on Draft P802.11ax_D6.1,
 * section 9.4.2.170 and accepted contributions.
 */
#define IEEE80211_AP_INFO_TBTT_HDR_TYPE				0x03
#define IEEE80211_AP_INFO_TBTT_HDR_FILTERED			0x04
#define IEEE80211_AP_INFO_TBTT_HDR_COLOC			0x08
#define IEEE80211_AP_INFO_TBTT_HDR_COUNT			0xF0
#define IEEE80211_TBTT_INFO_TYPE_TBTT				0
#define IEEE80211_TBTT_INFO_TYPE_MLD				1

#define IEEE80211_RNR_TBTT_PARAMS_OCT_RECOMMENDED		0x01
#define IEEE80211_RNR_TBTT_PARAMS_SAME_SSID			0x02
#define IEEE80211_RNR_TBTT_PARAMS_MULTI_BSSID			0x04
#define IEEE80211_RNR_TBTT_PARAMS_TRANSMITTED_BSSID		0x08
#define IEEE80211_RNR_TBTT_PARAMS_COLOC_ESS			0x10
#define IEEE80211_RNR_TBTT_PARAMS_PROBE_ACTIVE			0x20
#define IEEE80211_RNR_TBTT_PARAMS_COLOC_AP			0x40
#define IEEE80211_RNR_TBTT_PARAMS_SAME_SMD			0x80

#define IEEE80211_RNR_TBTT_PARAMS_PSD_NO_LIMIT			127
#define IEEE80211_RNR_TBTT_PARAMS_PSD_RESERVED			-128

struct ieee80211_neighbor_ap_info {
	u8 tbtt_info_hdr;
	u8 tbtt_info_len;
	u8 op_class;
	u8 channel;
} __packed;

enum ieee80211_range_params_max_total_ltf {
	IEEE80211_RANGE_PARAMS_MAX_TOTAL_LTF_4 = 0,
	IEEE80211_RANGE_PARAMS_MAX_TOTAL_LTF_8,
	IEEE80211_RANGE_PARAMS_MAX_TOTAL_LTF_16,
	IEEE80211_RANGE_PARAMS_MAX_TOTAL_LTF_UNSPECIFIED,
};

/* [LNX] include/linux/ieee80211.h:1593 */
#define	WLAN_EID_SSID	0
/* [LNX] include/linux/ieee80211.h:1594 */
#define	WLAN_EID_SUPP_RATES	1
/* [LNX] include/linux/ieee80211.h:1705 */
#define	WLAN_EID_EXT_CAPABILITY	127
/* [LNX] include/linux/ieee80211.h:1780 */
#define	WLAN_EID_VENDOR_SPECIFIC	221
/* [LNX] include/linux/ieee80211.h:1852 */
#define	WLAN_CATEGORY_SA_QUERY	8
/* [LNX] include/linux/ieee80211.h:2190 */
#define	WLAN_ACTION_SA_QUERY_RESPONSE	1
/*
 * The rest of the frame-control and sequence helpers the interface states as
 * inline functions (include/linux/ieee80211.h).  Bodies are the interface's, one
 * for one; the sequence-number form uses this face's own SCTL_SEQ/SEQ_TO_SN pair
 * (above), which is the same arithmetic under the interface's names.
 */
static inline bool ieee80211_has_order(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_ORDER)) != 0;
}

static inline bool ieee80211_is_ext(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_EXT);
}

static inline bool ieee80211_is_nullfunc(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC);
}

static inline bool ieee80211_is_qos_nullfunc(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_NULLFUNC);
}

static inline bool ieee80211_is_probe_resp(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_RESP);
}

static inline bool ieee80211_is_pspoll(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_PSPOLL);
}

static inline bool ieee80211_is_trigger(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	       cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_TRIGGER);
}

static inline bool ieee80211_is_any_nullfunc(__le16 fc)
{
	return (ieee80211_is_nullfunc(fc) || ieee80211_is_qos_nullfunc(fc));
}

static inline bool ieee80211_has_pm(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_PM)) != 0;
}

static inline u16 ieee80211_get_sn(struct ieee80211_hdr *hdr)
{
	return IEEE80211_SEQ_TO_SN(le16_to_cpu(hdr->seq_ctrl));
}

static inline u8 ieee80211_get_tid(struct ieee80211_hdr *hdr)
{
	u8 *qc = ieee80211_get_qos_ctl(hdr);

	return qc[0] & IEEE80211_QOS_CTL_TID_MASK;
}

static inline bool ieee80211_sn_less(u16 sn1, u16 sn2)
{
	return ((sn1 - sn2) & IEEE80211_SN_MASK) > (IEEE80211_SN_MODULO >> 1);
}

static inline unsigned long ieee80211_tu_to_usec(unsigned long tu)
{
	return 1024 * tu;
}

static inline unsigned int ieee80211_hdrlen(__le16 fc)
{
	unsigned int hdrlen = 24;

	if (ieee80211_is_ext(fc)) {
		hdrlen = 4;
		goto out;
	}

	if (ieee80211_is_data(fc)) {
		if (ieee80211_has_a4(fc))
			hdrlen = 30;
		if (ieee80211_is_data_qos(fc)) {
			hdrlen += IEEE80211_QOS_CTL_LEN;
			if (ieee80211_has_order(fc))
				hdrlen += IEEE80211_HT_CTL_LEN;
		}
		goto out;
	}

	if (ieee80211_is_mgmt(fc)) {
		if (ieee80211_has_order(fc))
			hdrlen += IEEE80211_HT_CTL_LEN;
		goto out;
	}

	if (ieee80211_is_ctl(fc)) {
		/*
		 * ACK and CTS are 10 bytes, all others 16: the subtype bits
		 * that separate them are 0x00e0, and the two short forms share
		 * the value 0x00c0.
		 */
		if ((fc & cpu_to_le16(0x00e0)) == cpu_to_le16(0x00c0))
			hdrlen = 10;
		else
			hdrlen = 16;
	}
out:
	return hdrlen;
}

#endif /* _LINUXKPI_LINUX_IEEE80211_H_ */
