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

#endif /* _LINUXKPI_LINUX_IEEE80211_H_ */