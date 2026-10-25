/* SPDX-License-Identifier: BSD-3-Clause */
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

/* [LNX] include/net/ieee80211_radiotap.h:405-412: the vendor-namespace radiotap
 * content record the receive path measures. */
struct ieee80211_radiotap_vendor_content {
	u8	oui[3];
	u8	oui_subtype;
	__le16	vendor_type;
	__le16	reserved;
	u8	data[];
} __packed;

/* [LNX] include/net/ieee80211_radiotap.h:102-110: the IEEE80211_RADIOTAP_FLAGS field's
 * bits. */
enum ieee80211_radiotap_flags {
	IEEE80211_RADIOTAP_F_CFP = 0x01,
	IEEE80211_RADIOTAP_F_SHORTPRE = 0x02,
	IEEE80211_RADIOTAP_F_WEP = 0x04,
	IEEE80211_RADIOTAP_F_FRAG = 0x08,
	IEEE80211_RADIOTAP_F_FCS = 0x10,
	IEEE80211_RADIOTAP_F_DATAPAD = 0x20,
	IEEE80211_RADIOTAP_F_BADFCS = 0x40,
};

/* [LNX] include/net/ieee80211_radiotap.h:113-121: the IEEE80211_RADIOTAP_CHANNEL
 * field's band/modulation bits. */
enum ieee80211_radiotap_channel_flags {
	IEEE80211_CHAN_CCK = 0x0020,
	IEEE80211_CHAN_OFDM = 0x0040,
	IEEE80211_CHAN_2GHZ = 0x0080,
	IEEE80211_CHAN_5GHZ = 0x0100,
	IEEE80211_CHAN_DYN = 0x0400,
	IEEE80211_CHAN_HALF = 0x4000,
	IEEE80211_CHAN_QUARTER = 0x8000,
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
	/* [LNX] net/ieee80211_radiotap.h:359-362: the signal-A bandwidth values. */
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_20MHZ	= 0x0000,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_40MHZ	= 0x0001,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_80MHZ	= 0x0002,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_160MHZ	= 0x0003,
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

/* [LNX] include/net/ieee80211_radiotap.h:800-805: the header-length accessor the
 * monitor-mode receive path calls to step over a captured radiotap header. */
static inline u16 ieee80211_get_radiotap_len(const char *data)
{
	const struct ieee80211_radiotap_header *hdr = (const void *)data;

	return get_unaligned_le16(&hdr->it_len);
}

/* ------------------------------------------------------------------ */
/* the remaining radiotap field constants the compiled set names, each
 * enumerated in the interface's own headers (the reference on each line
 * names the file and line it was transcribed from). */
/* ------------------------------------------------------------------ */

	/* [LNX] net/ieee80211_radiotap.h:402 */
	#define	IEEE80211_RADIOTAP_EHT_DATA0_EHT_LTF	14336
	/* [LNX] net/ieee80211_radiotap.h:403 */
	#define	IEEE80211_RADIOTAP_EHT_DATA0_LDPC_EXTRA_SYM_OM	16384
	/* [LNX] net/ieee80211_radiotap.h:401 */
	#define	IEEE80211_RADIOTAP_EHT_DATA0_LTF	1536
	/* [LNX] net/ieee80211_radiotap.h:408 */
	#define	IEEE80211_RADIOTAP_EHT_DATA0_PE_DISAMBIGUITY_OM	131072
	/* [LNX] net/ieee80211_radiotap.h:405 */
	#define	IEEE80211_RADIOTAP_EHT_DATA0_PRE_PADD_FACOR_OM	98304
	/* [LNX] net/ieee80211_radiotap.h:398 */
	#define	IEEE80211_RADIOTAP_EHT_DATA0_SPATIAL_REUSE	120
	/* [LNX] net/ieee80211_radiotap.h:426 */
	#define	IEEE80211_RADIOTAP_EHT_DATA1_PRIMARY_80	3221225472
	/* [LNX] net/ieee80211_radiotap.h:479 */
	#define	IEEE80211_RADIOTAP_EHT_DATA7_BEAMFORMED_S	65536
	/* [LNX] net/ieee80211_radiotap.h:478 */
	#define	IEEE80211_RADIOTAP_EHT_DATA7_NSS_S	61440
	/* [LNX] net/ieee80211_radiotap.h:480 */
	#define	IEEE80211_RADIOTAP_EHT_DATA7_NUM_OF_NON_OFDMA_USERS	917504
	/* [LNX] net/ieee80211_radiotap.h:486 */
	#define	IEEE80211_RADIOTAP_EHT_DATA8_RU_ALLOC_TB_FMT_B0	2
	/* [LNX] net/ieee80211_radiotap.h:487 */
	#define	IEEE80211_RADIOTAP_EHT_DATA8_RU_ALLOC_TB_FMT_B7_B1	508
	/* [LNX] net/ieee80211_radiotap.h:485 */
	#define	IEEE80211_RADIOTAP_EHT_DATA8_RU_ALLOC_TB_FMT_PS_160	1
	/* [LNX] net/ieee80211_radiotap.h:376 */
	#define	IEEE80211_RADIOTAP_EHT_KNOWN_BEAMFORMED_S	262144
	/* [LNX] net/ieee80211_radiotap.h:361 */
	#define	IEEE80211_RADIOTAP_EHT_KNOWN_EHT_LTF	16
	/* [LNX] net/ieee80211_radiotap.h:362 */
	#define	IEEE80211_RADIOTAP_EHT_KNOWN_LDPC_EXTRA_SYM_OM	32
	/* [LNX] net/ieee80211_radiotap.h:377 */
	#define	IEEE80211_RADIOTAP_EHT_KNOWN_NR_NON_OFDMA_USERS_M	524288
	/* [LNX] net/ieee80211_radiotap.h:373 */
	#define	IEEE80211_RADIOTAP_EHT_KNOWN_NSS_S	131072
	/* [LNX] net/ieee80211_radiotap.h:364 */
	#define	IEEE80211_RADIOTAP_EHT_KNOWN_PE_DISAMBIGUITY_OM	128
	/* [LNX] net/ieee80211_radiotap.h:363 */
	#define	IEEE80211_RADIOTAP_EHT_KNOWN_PRE_PADD_FACOR_OM	64
	/* [LNX] net/ieee80211_radiotap.h:393 */
	#define	IEEE80211_RADIOTAP_EHT_KNOWN_PRIMARY_80	33554432
	/* [LNX] net/ieee80211_radiotap.h:388 */
	#define	IEEE80211_RADIOTAP_EHT_KNOWN_RU_ALLOC_TB_FMT	16777216
	/* [LNX] net/ieee80211_radiotap.h:360 */
	#define	IEEE80211_RADIOTAP_EHT_KNOWN_SPATIAL_REUSE	2
	/* [LNX] net/ieee80211_radiotap.h:493 */
	#define	IEEE80211_RADIOTAP_EHT_USER_INFO_BEAMFORMING_KNOWN_O	32
	/* [LNX] net/ieee80211_radiotap.h:501 */
	#define	IEEE80211_RADIOTAP_EHT_USER_INFO_BEAMFORMING_O	536870912
	/* [LNX] net/ieee80211_radiotap.h:495 */
	#define	IEEE80211_RADIOTAP_EHT_USER_INFO_DATA_FOR_USER	128
	/* [LNX] net/ieee80211_radiotap.h:496 */
	#define	IEEE80211_RADIOTAP_EHT_USER_INFO_STA_ID	524032
	/* [LNX] net/ieee80211_radiotap.h:489 */
	#define	IEEE80211_RADIOTAP_EHT_USER_INFO_STA_ID_KNOWN	1
	/* [LNX] net/ieee80211_radiotap.h:524 */
	#define	IEEE80211_RADIOTAP_EHT_USIG1_MU_B20_B24_DISREGARD	31
	/* [LNX] net/ieee80211_radiotap.h:526 */
	#define	IEEE80211_RADIOTAP_EHT_USIG1_MU_B25_VALIDATE	32
	/* [LNX] net/ieee80211_radiotap.h:539 */
	#define	IEEE80211_RADIOTAP_EHT_USIG1_TB_B20_B25_DISREGARD	31
	/* [LNX] net/ieee80211_radiotap.h:527 */
	#define	IEEE80211_RADIOTAP_EHT_USIG2_MU_B0_B1_PPDU_TYPE	192
	/* [LNX] net/ieee80211_radiotap.h:533 */
	#define	IEEE80211_RADIOTAP_EHT_USIG2_MU_B11_B15_EHT_SIG_SYMBOLS	4063232
	/* [LNX] net/ieee80211_radiotap.h:534 */
	#define	IEEE80211_RADIOTAP_EHT_USIG2_MU_B16_B19_CRC	62914560
	/* [LNX] net/ieee80211_radiotap.h:529 */
	#define	IEEE80211_RADIOTAP_EHT_USIG2_MU_B2_VALIDATE	256
	/* [LNX] net/ieee80211_radiotap.h:530 */
	#define	IEEE80211_RADIOTAP_EHT_USIG2_MU_B3_B7_PUNCTURED_INFO	15872
	/* [LNX] net/ieee80211_radiotap.h:531 */
	#define	IEEE80211_RADIOTAP_EHT_USIG2_MU_B8_VALIDATE	16384
	/* [LNX] net/ieee80211_radiotap.h:532 */
	#define	IEEE80211_RADIOTAP_EHT_USIG2_MU_B9_B10_SIG_MCS	98304
	/* [LNX] net/ieee80211_radiotap.h:540 */
	#define	IEEE80211_RADIOTAP_EHT_USIG2_TB_B0_B1_PPDU_TYPE	192
	/* [LNX] net/ieee80211_radiotap.h:545 */
	#define	IEEE80211_RADIOTAP_EHT_USIG2_TB_B11_B15_DISREGARD	4063232
	/* [LNX] net/ieee80211_radiotap.h:545 */
	#define	IEEE80211_RADIOTAP_EHT_USIG2_TB_B16_B19_CRC	62914560
	/* [LNX] net/ieee80211_radiotap.h:541 */
	#define	IEEE80211_RADIOTAP_EHT_USIG2_TB_B2_VALIDATE	256
	/* [LNX] net/ieee80211_radiotap.h:542 */
	#define	IEEE80211_RADIOTAP_EHT_USIG2_TB_B3_B6_SPATIAL_REUSE_1	7680
	/* [LNX] net/ieee80211_radiotap.h:544 */
	#define	IEEE80211_RADIOTAP_EHT_USIG2_TB_B7_B10_SPATIAL_REUSE_2	122880
	/* [LNX] net/ieee80211_radiotap.h:510 */
	#define	IEEE80211_RADIOTAP_EHT_USIG_COMMON_BAD_USIG_CRC	32
	/* [LNX] net/ieee80211_radiotap.h:522 */
	#define	IEEE80211_RADIOTAP_EHT_USIG_COMMON_BSS_COLOR	33030144
	/* [LNX] net/ieee80211_radiotap.h:508 */
	#define	IEEE80211_RADIOTAP_EHT_USIG_COMMON_BSS_COLOR_KNOWN	8
	/* [LNX] net/ieee80211_radiotap.h:514 */
	#define	IEEE80211_RADIOTAP_EHT_USIG_COMMON_PHY_VER	28672
	/* [LNX] net/ieee80211_radiotap.h:505 */
	#define	IEEE80211_RADIOTAP_EHT_USIG_COMMON_PHY_VER_KNOWN	1
	/* [LNX] net/ieee80211_radiotap.h:523 */
	#define	IEEE80211_RADIOTAP_EHT_USIG_COMMON_TXOP	4261412864
	/* [LNX] net/ieee80211_radiotap.h:509 */
	#define	IEEE80211_RADIOTAP_EHT_USIG_COMMON_TXOP_KNOWN	16
	/* [LNX] net/ieee80211_radiotap.h:521 */
	#define	IEEE80211_RADIOTAP_EHT_USIG_COMMON_UL_DL	262144
	/* [LNX] net/ieee80211_radiotap.h:507 */
	#define	IEEE80211_RADIOTAP_EHT_USIG_COMMON_UL_DL_KNOWN	4
	/* [LNX] net/ieee80211_radiotap.h:512 */
	#define	IEEE80211_RADIOTAP_EHT_USIG_COMMON_VALIDATE_BITS_CHECKED	64
	/* [LNX] net/ieee80211_radiotap.h:513 */
	#define	IEEE80211_RADIOTAP_EHT_USIG_COMMON_VALIDATE_BITS_OK	128
	/* [LNX] net/ieee80211_radiotap.h:208 */
	#define	IEEE80211_RADIOTAP_HE_DATA1_DATA_DCM_KNOWN	64
	/* [LNX] net/ieee80211_radiotap.h:232 */
	#define	IEEE80211_RADIOTAP_HE_DATA2_NUM_LTF_SYMS_KNOWN	4
	/* [LNX] net/ieee80211_radiotap.h:229 */
	#define	IEEE80211_RADIOTAP_HE_DATA2_PRISEC_80_KNOWN	1
	/* [LNX] net/ieee80211_radiotap.h:242 */
	#define	IEEE80211_RADIOTAP_HE_DATA2_PRISEC_80_SEC	32768
	/* [LNX] net/ieee80211_radiotap.h:240 */
	#define	IEEE80211_RADIOTAP_HE_DATA2_RU_OFFSET	16128
	/* [LNX] net/ieee80211_radiotap.h:241 */
	#define	IEEE80211_RADIOTAP_HE_DATA2_RU_OFFSET_KNOWN	16384
	/* [LNX] net/ieee80211_radiotap.h:279 */
	#define	IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE	192
	/* [LNX] net/ieee80211_radiotap.h:281 */
	#define	IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_1X	1
	/* [LNX] net/ieee80211_radiotap.h:283 */
	#define	IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_2X	2
	/* [LNX] net/ieee80211_radiotap.h:284 */
	#define	IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_4X	3
	/* [LNX] net/ieee80211_radiotap.h:280 */
	#define	IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_UNKNOWN	0
	/* [LNX] net/ieee80211_radiotap.h:285 */
	#define	IEEE80211_RADIOTAP_HE_DATA5_NUM_LTF_SYMS	1792
	/* [LNX] net/ieee80211_radiotap.h:293 */
	#define	IEEE80211_RADIOTAP_HE_DATA6_TB_PPDU_BW	192
	/* [LNX] net/ieee80211_radiotap.h:292 */
	#define	IEEE80211_RADIOTAP_HE_DATA6_TB_PPDU_BW_KNOWN	32
	/* [LNX] net/ieee80211_radiotap.h:334 */
	#define	IEEE80211_RADIOTAP_LSIG_DATA1_LENGTH_KNOWN	2
	/* [LNX] net/ieee80211_radiotap.h:339 */
	#define	IEEE80211_RADIOTAP_LSIG_DATA2_LENGTH	65520
	/* [LNX] net/ieee80211_radiotap.h:182 */
	#define	IEEE80211_RADIOTAP_TIMESTAMP_SPOS_PLCP_SIG_ACQ	16
	/* [LNX] net/ieee80211_radiotap.h:175 */
	#define	IEEE80211_RADIOTAP_TIMESTAMP_UNIT_US	1
	/* [LNX] net/ieee80211_radiotap.h:347 */
	#define	IEEE80211_RADIOTAP_ZERO_LEN_PSDU_NOT_CAPTURED	1
	/* [LNX] net/ieee80211_radiotap.h:347 */
	#define	IEEE80211_RADIOTAP_ZERO_LEN_PSDU_SOUNDING	0
	/* [LNX] net/ieee80211_radiotap.h:348 */
	#define	IEEE80211_RADIOTAP_ZERO_LEN_PSDU_VENDOR	255

/* ------------------------------------------------------------------ */
/* The remaining radiotap field constants the compiled set names,
 * each line names the interface header and line it was transcribed from. */
/* ------------------------------------------------------------------ */

	/* [LNX] net/ieee80211_radiotap.h:421 */
	#define	IEEE80211_RADIOTAP_EHT_DATA1_RU_ALLOC_CC_1_1_1	4186112
	/* [LNX] net/ieee80211_radiotap.h:425 */
	#define	IEEE80211_RADIOTAP_EHT_DATA1_RU_ALLOC_CC_1_1_1_KNOWN	4194304
	/* [LNX] net/ieee80211_radiotap.h:434 */
	#define	IEEE80211_RADIOTAP_EHT_DATA2_RU_ALLOC_CC_1_1_2	523264
	/* [LNX] net/ieee80211_radiotap.h:435 */
	#define	IEEE80211_RADIOTAP_EHT_DATA2_RU_ALLOC_CC_1_1_2_KNOWN	524288
	/* [LNX] net/ieee80211_radiotap.h:428 */
	#define	IEEE80211_RADIOTAP_EHT_DATA2_RU_ALLOC_CC_2_1_1	511
	/* [LNX] net/ieee80211_radiotap.h:431 */
	#define	IEEE80211_RADIOTAP_EHT_DATA2_RU_ALLOC_CC_2_1_1_KNOWN	512
	/* [LNX] net/ieee80211_radiotap.h:439 */
	#define	IEEE80211_RADIOTAP_EHT_DATA2_RU_ALLOC_CC_2_1_2	535822336
	/* [LNX] net/ieee80211_radiotap.h:444 */
	#define	IEEE80211_RADIOTAP_EHT_DATA2_RU_ALLOC_CC_2_1_2_KNOWN	536870912
	/* [LNX] net/ieee80211_radiotap.h:445 */
	#define	IEEE80211_RADIOTAP_EHT_DATA3_RU_ALLOC_CC_1_2_1	511
	/* [LNX] net/ieee80211_radiotap.h:447 */
	#define	IEEE80211_RADIOTAP_EHT_DATA3_RU_ALLOC_CC_1_2_1_KNOWN	512
	/* [LNX] net/ieee80211_radiotap.h:451 */
	#define	IEEE80211_RADIOTAP_EHT_DATA3_RU_ALLOC_CC_1_2_2	535822336
	/* [LNX] net/ieee80211_radiotap.h:452 */
	#define	IEEE80211_RADIOTAP_EHT_DATA3_RU_ALLOC_CC_1_2_2_KNOWN	536870912
	/* [LNX] net/ieee80211_radiotap.h:448 */
	#define	IEEE80211_RADIOTAP_EHT_DATA3_RU_ALLOC_CC_2_2_1	523264
	/* [LNX] net/ieee80211_radiotap.h:449 */
	#define	IEEE80211_RADIOTAP_EHT_DATA3_RU_ALLOC_CC_2_2_1_KNOWN	524288
	/* [LNX] net/ieee80211_radiotap.h:453 */
	#define	IEEE80211_RADIOTAP_EHT_DATA4_RU_ALLOC_CC_2_2_2	511
	/* [LNX] net/ieee80211_radiotap.h:454 */
	#define	IEEE80211_RADIOTAP_EHT_DATA4_RU_ALLOC_CC_2_2_2_KNOWN	512
	/* [LNX] net/ieee80211_radiotap.h:297 */
	#define	IEEE80211_RADIOTAP_HE_DATA6_TB_PPDU_BW_160MHZ	3
	/* [LNX] net/ieee80211_radiotap.h:294 */
	#define	IEEE80211_RADIOTAP_HE_DATA6_TB_PPDU_BW_20MHZ	0
	/* [LNX] net/ieee80211_radiotap.h:295 */
	#define	IEEE80211_RADIOTAP_HE_DATA6_TB_PPDU_BW_40MHZ	1
	/* [LNX] net/ieee80211_radiotap.h:296 */
	#define	IEEE80211_RADIOTAP_HE_DATA6_TB_PPDU_BW_80MHZ	2

/* [LNX] include/net/ieee80211_radiotap.h: the L-SIG field record the receive path
 * measures. */
struct ieee80211_radiotap_lsig {
	__le16	data1, data2;
};

#endif /* _LINUXKPI_NET_IEEE80211_RADIOTAP_H_ */
