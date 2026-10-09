/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    net/ieee80211_radiotap.h
 * @brief   The radiotap capture header a monitor-mode receive path prepends: the base
 *          header record and the presence-bitmap field indices.
 *
 * @design declaration-only compatibility face, transcribed field for field from the
 *         interface's header (include/net/ieee80211_radiotap.h:26-99). The interface
 *         builds the base record with its __struct_group() alias so a fixed-size view of
 *         the four leading fields can be named as well; that alias machinery is not
 *         carried, because nothing in the compiled set names it - the record below has
 *         the interface's exact members, in its order, with its types and packing, and
 *         the flexible `it_optional` array that makes the header as long as its
 *         per-field presence bitmaps require. It is part of the vendored driver's fixed
 *         include set, so the header must exist under this name even where the compiled
 *         unit only includes it; the rest of the interface's radiotap surface (the
 *         per-field value records and their bit definitions) arrives with the consumer
 *         that names them. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_NET_IEEE80211_RADIOTAP_H_
#define _LINUXKPI_NET_IEEE80211_RADIOTAP_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/net/ieee80211_radiotap.h:26-53 */
struct ieee80211_radiotap_header {
	u8		it_version;	/* radiotap version, always 0 */
	u8		it_pad;		/* padding (or alignment) */
	__le16		it_len;		/* overall radiotap header length */
	__le32		it_present;	/* (first) present word */

	__le32		it_optional[];	/* all remaining presence bitmaps */
} __packed;

/* [LNX] include/net/ieee80211_radiotap.h:57 */
#define	PKTHDR_RADIOTAP_VERSION	0

/* [LNX] include/net/ieee80211_radiotap.h:62-99, one enumerator for one present-word bit
 * position: the values are the wire field indices and are contract with the capture
 * format, so they are stated exactly. */
enum ieee80211_radiotap_presence {
	IEEE80211_RADIOTAP_TSFT = 0,
	IEEE80211_RADIOTAP_FLAGS = 1,
	IEEE80211_RADIOTAP_RATE = 2,
	IEEE80211_RADIOTAP_CHANNEL = 3,
	IEEE80211_RADIOTAP_FHSS = 4,
	IEEE80211_RADIOTAP_DBM_ANTSIGNAL = 5,
	IEEE80211_RADIOTAP_DBM_ANTNOISE = 6,
	IEEE80211_RADIOTAP_LOCK_QUALITY = 7,
	IEEE80211_RADIOTAP_TX_ATTENUATION = 8,
	IEEE80211_RADIOTAP_DB_TX_ATTENUATION = 9,
	IEEE80211_RADIOTAP_DBM_TX_POWER = 10,
	IEEE80211_RADIOTAP_ANTENNA = 11,
	IEEE80211_RADIOTAP_DB_ANTSIGNAL = 12,
	IEEE80211_RADIOTAP_DB_ANTNOISE = 13,
	IEEE80211_RADIOTAP_RX_FLAGS = 14,
	IEEE80211_RADIOTAP_TX_FLAGS = 15,
	IEEE80211_RADIOTAP_RTS_RETRIES = 16,
	IEEE80211_RADIOTAP_DATA_RETRIES = 17,
	/* 18 is XChannel, but it's not defined yet */
	IEEE80211_RADIOTAP_MCS = 19,
	IEEE80211_RADIOTAP_AMPDU_STATUS = 20,
	IEEE80211_RADIOTAP_VHT = 21,
	IEEE80211_RADIOTAP_TIMESTAMP = 22,
	IEEE80211_RADIOTAP_HE = 23,
	IEEE80211_RADIOTAP_HE_MU = 24,
	IEEE80211_RADIOTAP_ZERO_LEN_PSDU = 26,
	IEEE80211_RADIOTAP_LSIG = 27,
	IEEE80211_RADIOTAP_TLV = 28,

	/* valid in every it_present bitmap, even vendor namespaces */
	IEEE80211_RADIOTAP_RADIOTAP_NAMESPACE = 29,
	IEEE80211_RADIOTAP_VENDOR_NAMESPACE = 30,
	IEEE80211_RADIOTAP_EXT = 31,
	IEEE80211_RADIOTAP_EHT_USIG = 33,
	IEEE80211_RADIOTAP_EHT = 34,
	IEEE80211_RADIOTAP_UHR_ELR = 37,
	IEEE80211_RADIOTAP_UHR = 38,
};

/* [LNX] include/net/ieee80211_radiotap.h:140-147 */
enum ieee80211_radiotap_mcs_have {
	IEEE80211_RADIOTAP_MCS_HAVE_FEC = 0x10,
	IEEE80211_RADIOTAP_MCS_HAVE_STBC = 0x20,
};

/* [LNX] include/net/ieee80211_radiotap.h:179-189 */
enum ieee80211_radiotap_vht_known {
	IEEE80211_RADIOTAP_VHT_KNOWN_STBC = 0x0001,
	IEEE80211_RADIOTAP_VHT_KNOWN_BEAMFORMED = 0x0020,
};

/* [LNX] include/net/ieee80211_radiotap.h:215-223 */
struct ieee80211_radiotap_vht {
	__le16 known;
	u8 flags;
	u8 bandwidth;
	u8 mcs_nss[4];
	u8 coding;
	u8 group_id;
	__le16 partial_aid;
};

/* [LNX] include/net/ieee80211_radiotap.h:245-247 */
struct ieee80211_radiotap_he {
	__le16 data1, data2, data3, data4, data5, data6;
};

/* [LNX] include/net/ieee80211_radiotap.h:249-337 */
enum ieee80211_radiotap_he_bits {
	IEEE80211_RADIOTAP_HE_DATA1_FORMAT_MASK		= 3,
	IEEE80211_RADIOTAP_HE_DATA1_FORMAT_SU		= 0,
	IEEE80211_RADIOTAP_HE_DATA1_FORMAT_EXT_SU	= 1,
	IEEE80211_RADIOTAP_HE_DATA1_FORMAT_MU		= 2,
	IEEE80211_RADIOTAP_HE_DATA1_FORMAT_TRIG		= 3,
	IEEE80211_RADIOTAP_HE_DATA1_BSS_COLOR_KNOWN	= 0x0004,
	IEEE80211_RADIOTAP_HE_DATA1_BEAM_CHANGE_KNOWN	= 0x0008,
	IEEE80211_RADIOTAP_HE_DATA1_UL_DL_KNOWN		= 0x0010,
	IEEE80211_RADIOTAP_HE_DATA1_DATA_MCS_KNOWN	= 0x0020,
	IEEE80211_RADIOTAP_HE_DATA1_CODING_KNOWN	= 0x0080,
	IEEE80211_RADIOTAP_HE_DATA1_LDPC_XSYMSEG_KNOWN	= 0x0100,
	IEEE80211_RADIOTAP_HE_DATA1_STBC_KNOWN		= 0x0200,
	IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE_KNOWN	= 0x0400,
	IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE2_KNOWN	= 0x0800,
	IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE3_KNOWN	= 0x1000,
	IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE4_KNOWN	= 0x2000,
	IEEE80211_RADIOTAP_HE_DATA1_BW_RU_ALLOC_KNOWN	= 0x4000,
	IEEE80211_RADIOTAP_HE_DATA1_DOPPLER_KNOWN	= 0x8000,
	IEEE80211_RADIOTAP_HE_DATA2_GI_KNOWN		= 0x0002,
	IEEE80211_RADIOTAP_HE_DATA2_PRE_FEC_PAD_KNOWN	= 0x0008,
	IEEE80211_RADIOTAP_HE_DATA2_TXBF_KNOWN		= 0x0010,
	IEEE80211_RADIOTAP_HE_DATA2_PE_DISAMBIG_KNOWN	= 0x0020,
	IEEE80211_RADIOTAP_HE_DATA2_TXOP_KNOWN		= 0x0040,
	IEEE80211_RADIOTAP_HE_DATA2_MIDAMBLE_KNOWN	= 0x0080,
	IEEE80211_RADIOTAP_HE_DATA3_BSS_COLOR		= 0x003f,
	IEEE80211_RADIOTAP_HE_DATA3_BEAM_CHANGE		= 0x0040,
	IEEE80211_RADIOTAP_HE_DATA3_UL_DL		= 0x0080,
	IEEE80211_RADIOTAP_HE_DATA3_LDPC_XSYMSEG	= 0x4000,
	IEEE80211_RADIOTAP_HE_DATA4_SU_MU_SPTL_REUSE	= 0x000f,
	IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE1	= 0x000f,
	IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE2	= 0x00f0,
	IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE3	= 0x0f00,
	IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE4	= 0xf000,
	IEEE80211_RADIOTAP_HE_DATA5_PRE_FEC_PAD		= 0x3000,
	IEEE80211_RADIOTAP_HE_DATA5_TXBF		= 0x4000,
	IEEE80211_RADIOTAP_HE_DATA5_PE_DISAMBIG		= 0x8000,
	IEEE80211_RADIOTAP_HE_DATA6_DOPPLER		= 0x0010,
	IEEE80211_RADIOTAP_HE_DATA6_TXOP		= 0x7f00,
	IEEE80211_RADIOTAP_HE_DATA6_MIDAMBLE_PDCTY	= 0x8000,
};

/* [LNX] include/net/ieee80211_radiotap.h:339-343 */
struct ieee80211_radiotap_he_mu {
	__le16 flags1, flags2;
	u8 ru_ch1[4];
	u8 ru_ch2[4];
};

/* [LNX] include/net/ieee80211_radiotap.h:345-369 */
enum ieee80211_radiotap_he_mu_bits {
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_MCS		= 0x000f,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_MCS_KNOWN		= 0x0010,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_DCM		= 0x0020,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_DCM_KNOWN		= 0x0040,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH2_CTR_26T_RU_KNOWN	= 0x0080,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH1_RU_KNOWN		= 0x0100,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH2_RU_KNOWN		= 0x0200,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH1_CTR_26T_RU_KNOWN	= 0x1000,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH1_CTR_26T_RU		= 0x2000,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_COMP_KNOWN	= 0x4000,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_SYMS_USERS_KNOWN	= 0x8000,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW	= 0x0003,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_KNOWN	= 0x0004,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_SIG_B_COMP		= 0x0008,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_SIG_B_SYMS_USERS	= 0x00f0,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_PUNC_FROM_SIG_A_BW	= 0x0300,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_PUNC_FROM_SIG_A_BW_KNOWN= 0x0400,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_CH2_CTR_26T_RU		= 0x0800,
};

/* [LNX] include/net/ieee80211_radiotap.h:391-395 */
struct ieee80211_radiotap_tlv {
	__le16 type;
	__le16 len;
	u8 data[];
};

/* [LNX] include/net/ieee80211_radiotap.h:428-432 */
struct ieee80211_radiotap_eht_usig {
	__le32 common;
	__le32 value;
	__le32 mask;
};

/* [LNX] include/net/ieee80211_radiotap.h:437-441 */
struct ieee80211_radiotap_eht {
	__le32 known;
	__le32 data[9];
	__le32 user_info[];
};

/* [LNX] include/net/ieee80211_radiotap.h:447-469 */
enum ieee80211_radiotap_eht_known {
	IEEE80211_RADIOTAP_EHT_KNOWN_GI				= 0x00000004,
};

/* [LNX] include/net/ieee80211_radiotap.h:471-537 */
enum ieee80211_radiotap_eht_data {
	IEEE80211_RADIOTAP_EHT_DATA0_GI				= 0x00000180,
};

/* [LNX] include/net/ieee80211_radiotap.h:539-554 */
enum ieee80211_radiotap_eht_user_info {
	IEEE80211_RADIOTAP_EHT_USER_INFO_MCS_KNOWN		= 0x00000002,
	IEEE80211_RADIOTAP_EHT_USER_INFO_CODING_KNOWN		= 0x00000004,
	IEEE80211_RADIOTAP_EHT_USER_INFO_NSS_KNOWN_O		= 0x00000010,
	IEEE80211_RADIOTAP_EHT_USER_INFO_CODING			= 0x00080000,
	IEEE80211_RADIOTAP_EHT_USER_INFO_MCS			= 0x00f00000,
	IEEE80211_RADIOTAP_EHT_USER_INFO_NSS_O			= 0x0f000000,
};

/* [LNX] include/net/ieee80211_radiotap.h:556-576 */
enum ieee80211_radiotap_eht_usig_common {
	IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW_KNOWN		= 0x00000002,
	IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW			= 0x00038000,
		IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW_20MHZ		= 0,
		IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW_40MHZ		= 1,
		IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW_80MHZ		= 2,
		IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW_160MHZ		= 3,
		IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW_320MHZ_1		= 4,
};

#endif /* _LINUXKPI_NET_IEEE80211_RADIOTAP_H_ */
