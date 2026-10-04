/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * net/mac80211.h - the mac80211 driver API the verbatim Linux wireless
 * drivers compile against.
 *
 * The type/constant shape is only as complete as the drivers actually
 * touch (rtw88 first; grow it per driver, not speculatively), but the
 * struct ieee80211_ops signatures are the real Linux 6.12 ones: the
 * bridge (linuxkpi_80211.c) dispatches through them, so a signature
 * drift against the driver's op implementation would be a silent ABI
 * mismatch, not a compile error (the vendored world compiles -w).
 *
 * First-party, shaped on FreeBSD's linuxkpi approach: the mac80211 entry
 * points are implemented in linuxkpi_80211.c on top of the vendored
 * net80211 protocol core.
 */

#ifndef _LINUXKPI_NET_MAC80211_H_
#define _LINUXKPI_NET_MAC80211_H_

#include "../../linuxkpi_core.h"
#include <linux/if_ether.h>
#include <linux/etherdevice.h>

/* kernel vocabulary rtw88 reaches for; minilibc's prototype stays the
 * link-time provider for the rest of the image */
#ifndef	abs
#define	abs(x)		((x) < 0 ? -(x) : (x))
#endif

struct napi_struct;

/* TX rate descriptor (control.rates / status.rates) and its flags. */
struct ieee80211_tx_rate {
	s8			idx;
	u16			flags;
	u8			count;
};

#define	IEEE80211_TX_RC_MCS			BIT(0)
#define	IEEE80211_TX_RC_GREEN_FIELD		BIT(1)
#define	IEEE80211_TX_RC_40_MHZ			BIT(2)
#define	IEEE80211_TX_RC_DUP_DATA		BIT(3)
#define	IEEE80211_TX_RC_SHORT_GI		BIT(4)
#define	IEEE80211_TX_RC_VHT_MCS			BIT(7)
#define	IEEE80211_TX_RC_80_MHZ			BIT(8)
#define	IEEE80211_TX_RC_160_MHZ			BIT(9)
#define	IEEE80211_TX_RC_USE_RTS			BIT(11)
#define	IEEE80211_TX_RC_USE_CTS_PROTECT		BIT(12)

/* ------------------------------------------------------------------ */
/* constants                                                           */
/* ------------------------------------------------------------------ */

#define	IEEE80211_NUM_ACS	4
#define	IEEE80211_NUM_TIDS	16
#define	IEEE80211_NUM_BANDS	3
#define	NUM_NL80211_BANDS	IEEE80211_NUM_BANDS
#define	IEEE80211_MAX_SSID_LEN	32
#define	IEEE80211_MAX_DATA_LEN	2304
#define	IEEE80211_MAX_CHAINS	4
#define	IEEE80211_MAX_MPDU_LEN_VHT_11454	11454
#define	FCS_LEN			4
#define	IEEE80211_MAX_AMPDU_BUF_HT	256
#define	IEEE80211_DEAUTH_FRAME_LEN	(24 + 2)
#define	IEEE80211_CCMP_HDR_LEN		8
#define	IEEE80211_CCMP_PN_LEN		6
#define	IEEE80211_CCMP_MIC_LEN		8
#define	IEEE80211_CCMP_256_HDR_LEN	8
#define	IEEE80211_CCMP_256_MIC_LEN	16
#define	IEEE80211_GCMP_HDR_LEN		4
#define	IEEE80211_GCMP_MIC_LEN		16
#define	IEEE80211_IV_LEN		(IEEE80211_CCMP_HDR_LEN - IEEE80211_CCMP_PN_LEN)
#define	IEEE80211_TKIP_IV_LEN		8
#define	IEEE80211_TKIP_ICV_LEN		4
#define	IEEE80211_WEP_IV_LEN		4
#define	IEEE80211_WEP_ICV_LEN		4
#define	IEEE80211_MAX_FRAME_LEN		2352

enum nl80211_band {
	NL80211_BAND_2GHZ = 0,
	NL80211_BAND_5GHZ = 1,
	NL80211_BAND_60GHZ = 2,
};

enum nl80211_sar_type {
	NL80211_SAR_TYPE_POWER,
	NL80211_SAR_TYPE_COUNT
};

enum nl80211_iftype {
	NL80211_IFTYPE_UNSPECIFIED = 0,
	NL80211_IFTYPE_ADHOC = 1,
	NL80211_IFTYPE_STATION = 2,
	NL80211_IFTYPE_AP = 3,
	NL80211_IFTYPE_AP_VLAN = 4,
	NL80211_IFTYPE_WDS = 5,
	NL80211_IFTYPE_MONITOR = 6,
	NL80211_IFTYPE_MESH_POINT = 7,
	NL80211_IFTYPE_P2P_CLIENT = 8,
	NL80211_IFTYPE_P2P_GO = 9,
	NL80211_IFTYPE_P2P_DEVICE = 10,
};

enum nl80211_chan_width {
	NL80211_CHAN_WIDTH_20_NOHT = 0,
	NL80211_CHAN_WIDTH_20 = 1,
	NL80211_CHAN_WIDTH_40 = 2,
	NL80211_CHAN_WIDTH_80 = 3,
	NL80211_CHAN_WIDTH_80P80 = 4,
	NL80211_CHAN_WIDTH_160 = 5,
};

enum ieee80211_ac_numbers {
	IEEE80211_AC_VO = 0,
	IEEE80211_AC_VI = 1,
	IEEE80211_AC_BE = 2,
	IEEE80211_AC_BK = 3,
};

enum ieee80211_smps_mode {
	IEEE80211_SMPS_OFF = 0,
	IEEE80211_SMPS_STATIC = 1,
	IEEE80211_SMPS_DYNAMIC = 2,
	IEEE80211_SMPS_AUTOMATIC = 3,
};

enum ieee80211_sta_rx_bandwidth {
	IEEE80211_STA_RX_BW_20 = 0,
	IEEE80211_STA_RX_BW_40 = 1,
	IEEE80211_STA_RX_BW_80 = 2,
	IEEE80211_STA_RX_BW_160 = 3,
};

/* channel flags */
#define	IEEE80211_CHAN_NO_IR		BIT(4)
#define	IEEE80211_CHAN_RADAR		BIT(3)
#define	IEEE80211_CHAN_NO_HT40MINUS	BIT(7)
#define	IEEE80211_CHAN_NO_80MHZ		BIT(11)
#define	IEEE80211_CHAN_DISABLED		0

/* HT capability bits */
#define	IEEE80211_HT_CAP_LDPC_CODING		BIT(0)
#define	IEEE80211_HT_CAP_SUP_WIDTH_20_40	BIT(1)
#define	IEEE80211_HT_CAP_SGI_20			BIT(5)
#define	IEEE80211_HT_CAP_SGI_40			BIT(6)
#define	IEEE80211_HT_CAP_TX_STBC		BIT(7)
#define	IEEE80211_HT_CAP_RX_STBC		(BIT(8) | BIT(9))
#define	IEEE80211_HT_CAP_RX_STBC_SHIFT		8
#define	IEEE80211_HT_CAP_DSSSCCK40		BIT(10)
#define	IEEE80211_HT_CAP_MAX_AMSDU		BIT(11)
#define	IEEE80211_HT_MCS_TX_DEFINED		BIT(0)
#define	IEEE80211_HT_MAX_AMPDU_64K		3
#define	IEEE80211_HT_MPDU_DENSITY_2		2

/* VHT capability bits */
#define	IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454		BIT(1)
#define	IEEE80211_VHT_CAP_RXLDPC			BIT(4)
#define	IEEE80211_VHT_CAP_SHORT_GI_80			BIT(5)
#define	IEEE80211_VHT_CAP_RXSTBC_MASK			(BIT(8) | BIT(9) | BIT(10))
#define	IEEE80211_VHT_CAP_RXSTBC_1			BIT(8)
#define	IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE		BIT(11)
#define	IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE		BIT(12)
#define	IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT		13
#define	IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK		(BIT(13) | BIT(14) | BIT(15))
#define	IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT	16
#define	IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK	(BIT(16) | BIT(17) | BIT(18))
#define	IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE		BIT(19)
#define	IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE		BIT(20)
#define	IEEE80211_VHT_CAP_TXSTBC			BIT(26)
#define	IEEE80211_VHT_CAP_HTC_VHT			BIT(29)
#define	IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK BIT(31)
#define	IEEE80211_VHT_MCS_SUPPORT_0_7			0
#define	IEEE80211_VHT_MCS_SUPPORT_0_8			1
#define	IEEE80211_VHT_MCS_SUPPORT_0_9			2
#define	IEEE80211_VHT_MCS_NOT_SUPPORTED			3

/* RX status flags and rate encoding */
#define	RX_FLAG_MMIC_ERROR			BIT(0)
#define	RX_FLAG_DECRYPTED			BIT(1)
#define	RX_FLAG_FAILED_FCS_CRC			BIT(3)
#define	RX_FLAG_FAILED_PLCP_CRC			BIT(4)
#define	RX_FLAG_HT				BIT(6)
#define	RX_FLAG_40MHZ				BIT(7)
#define	RX_FLAG_VHT				BIT(11)
#define	RX_FLAG_MACTIME_START			BIT(12)
#define	RX_FLAG_NO_SIGNAL_VAL			BIT(15)
#define	RX_FLAG_NO_PSDU				BIT(29)

#define	RX_ENC_LEGACY				0
#define	RX_ENC_HT				1
#define	RX_ENC_VHT				2

/* ieee80211_hw flags: the drivers name them without the prefix */
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
	IEEE80211_HW_NO_AUTO_VIF,
	IEEE80211_HW_SW_CRYPTO_CONTROL,
	IEEE80211_HW_SUPPORT_FAST_XMIT,
	IEEE80211_HW_REPORTS_TX_ACK_STATUS,
	IEEE80211_HW_CONNECTION_MONITOR,
	IEEE80211_HW_QUEUE_CONTROL,
	IEEE80211_HW_SUPPORTS_PER_STA_GTK,
	IEEE80211_HW_AP_LINK_PS,
	IEEE80211_HW_TX_AMSDU,
	IEEE80211_HW_TX_FRAG_LIST,
	IEEE80211_HW_REPORTS_LOW_ACK,
	IEEE80211_HW_SUPPORTS_TX_FRAG,
	IEEE80211_HW_SUPPORTS_AMSDU_IN_AMPDU,
	IEEE80211_HW_SINGLE_SCAN_ON_ALL_BANDS,
	NUM_IEEE80211_HW_FLAGS,
};

/* TX/RX status flags */
#define	IEEE80211_TX_CTL_REQ_TX_STATUS		BIT(10)
#define	IEEE80211_TX_CTL_AMPDU			BIT(5)
#define	IEEE80211_TX_CTL_NO_PS_BUFFER		BIT(3)
#define	IEEE80211_TX_STAT_ACK			BIT(13)
#define	IEEE80211_TX_STAT_AMPDU			BIT(14)
#define	IEEE80211_TX_STAT_AMPDU_NO_BACK		BIT(15)
#define	IEEE80211_TX_CTL_NO_ACK			BIT(9)
#define	IEEE80211_TX_CTL_USE_RTS_CTS		BIT(1)
#define	IEEE80211_TX_CTL_USE_CTS_PROTECT	BIT(2)
#define	IEEE80211_TX_CTL_FIRST_FRAGMENT		BIT(4)
#define	IEEE80211_TX_CTL_MORE_FRAMES		BIT(6)
#define	IEEE80211_TX_CTL_TX_OFFCHAN		BIT(12)
#define	IEEE80211_TX_CTL_RATE_CTRL_PROBE	BIT(0)
#define	IEEE80211_TX_INTFL_RETRIED		BIT(7)
#define	IEEE80211_TX_STAT_TX_FILTERED		BIT(11)
#define	IEEE80211_TX_STAT_NOACK_TRANSMITTED	BIT(16)

/* vif driver_flags */
#define	IEEE80211_VIF_BEACON_FILTER		BIT(0)
#define	IEEE80211_VIF_SUPPORTS_CQM_RSSI		BIT(1)
#define	IEEE80211_VIF_SUPPORTS_UAPSD		BIT(3)
#define	IEEE80211_VIF_GET_NOA_UPDATE		BIT(4)

/* workqueue flags */
#define	WQ_UNBOUND				0x00000002
#define	WQ_HIGHPRI				0x00000008
#define	WQ_SYSFS				0x00000010

/* conf flags */
#define	IEEE80211_CONF_IDLE			BIT(3)
#define	IEEE80211_CONF_CHANGE_CHANNEL		BIT(1)
#define	IEEE80211_CONF_CHANGE_POWER		BIT(2)
#define	IEEE80211_CONF_CHANGE_PS		BIT(4)
#define	IEEE80211_CONF_CHANGE_IDLE		BIT(5)
#define	IEEE80211_CONF_PS			BIT(0)
#define	IEEE80211_CONF_MONITOR			BIT(6)

#define	IEEE80211_IFACE_ITER_NORMAL		0
#define	IEEE80211_IFACE_ITER_RESUME_ALL		BIT(0)

#define	IEEE80211_KEY_FLAG_PAIRWISE		BIT(0)
#define	IEEE80211_KEY_FLAG_GENERATE_IV		BIT(1)
#define	IEEE80211_KEY_FLAG_GENERATE_MMIC	BIT(2)
#define	IEEE80211_KEY_FLAG_PUT_IV_SPACE		BIT(3)
#define	IEEE80211_KEY_FLAG_RX_MGMT		BIT(4)
#define	IEEE80211_KEY_FLAG_GENERATE_IV_MGMT	BIT(5)
#define	IEEE80211_KEY_FLAG_SW_MGMT_TX		BIT(8)

enum set_key_cmd {
	SET_KEY,
	DISABLE_KEY,
};

enum ieee80211_ampdu_mlme_action {
	IEEE80211_AMPDU_RX_START,
	IEEE80211_AMPDU_RX_STOP,
	IEEE80211_AMPDU_TX_START,
	IEEE80211_AMPDU_TX_STOP_CONT,
	IEEE80211_AMPDU_TX_STOP_FLUSH,
	IEEE80211_AMPDU_TX_STOP_FLUSH_CONT,
	IEEE80211_AMPDU_TX_OPERATIONAL,
};

/* ampdu_action TX_START return: 0 = start immediately, 1 = defer */
#define	IEEE80211_AMPDU_TX_START_IMMEDIATE	0
#define	IEEE80211_AMPDU_TX_START_DEFER		1

enum ieee80211_reconfig_type {
	IEEE80211_RECONFIG_TYPE_CONFIG,
	IEEE80211_RECONFIG_TYPE_RESTART,
};

/* MLO-era link sta: rtw88 only reaches through to the sta */
struct ieee80211_link_sta {
	struct ieee80211_sta	*sta;
};

/* sta_rc_update change bits */
#define	IEEE80211_RC_BW_CHANGED			BIT(0)
#define	IEEE80211_RC_SUPP_RATES_CHANGED		BIT(1)
#define	IEEE80211_RC_NSS_CHANGED		BIT(2)

/* configure_filter flags */
#define	FIF_ALLMULTI		BIT(1)
#define	FIF_FCSFAIL		BIT(2)
#define	FIF_PLCPFAIL		BIT(3)
#define	FIF_BCN_PRBRESP_PROMISC	BIT(4)
#define	FIF_CONTROL		BIT(5)
#define	FIF_OTHER_BSS		BIT(6)
#define	FIF_PSPOLL		BIT(7)
#define	FIF_PROBE_REQ		BIT(8)

#define	WLAN_CIPHER_SUITE_WEP40			0x000fac01
#define	WLAN_CIPHER_SUITE_TKIP			0x000fac02
#define	WLAN_CIPHER_SUITE_CCMP			0x000fac04
#define	WLAN_CIPHER_SUITE_CCMP_256		0x000fac08
#define	WLAN_CIPHER_SUITE_GCMP			0x000fac09
#define	WLAN_CIPHER_SUITE_GCMP_256		0x000fac0a
#define	WLAN_CIPHER_SUITE_WEP104		0x000fac05
#define	WLAN_CIPHER_SUITE_AES_CMAC		0x000fac06
#define	WLAN_CIPHER_SUITE_BIP_GMAC_128		0x000fac0b
#define	WLAN_CIPHER_SUITE_BIP_GMAC_256		0x000fac0c
#define	WLAN_CIPHER_SUITE_BIP_CMAC_256		0x000fac0d
#define	WLAN_CIPHER_SUITE_NO_GROUP_ADDR		0x000fac07

#define	WIPHY_FLAG_SUPPORTS_TDLS		BIT(0)
#define	WIPHY_FLAG_TDLS_EXTERNAL_SETUP		BIT(1)
#define	WIPHY_FLAG_IBSS_RSN			BIT(2)

#define	NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR	BIT(5)
#define	NL80211_EXT_FEATURE_CAN_REPLACE_PTK0	1
#define	NL80211_EXT_FEATURE_SCAN_RANDOM_SN	2
#define	NL80211_EXT_FEATURE_SET_SCAN_DWELL	3
#define	NL80211_SCAN_FLAG_RANDOM_ADDR		BIT(0)
#define	NL80211_SCAN_FLAG_RANDOM_SN		BIT(1)
#define	NL80211_SCAN_FLAG_LOW_PRIORITY		BIT(2)
#define	NL80211_SCAN_FLAG_AP			BIT(3)

#define	REGULATORY_STRICT_REG			BIT(0)
#define	REGULATORY_COUNTRY_IE_IGNORE		BIT(1)

#define	NL80211_REGDOM_SET_BY_DRIVER		0
#define	NL80211_REGDOM_SET_BY_USER		1
#define	NL80211_REGDOM_SET_BY_CORE		2

#define	NL80211_DFS_UNSET			0
#define	NL80211_DFS_FCC				1
#define	NL80211_DFS_ETSI			2
#define	NL80211_DFS_JP				3

#define	NL80211_SAR_TYPE_POWER			0

enum nl80211_cqm_rssi_threshold_event {
	NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW	= 0,
	NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH	= 1,
};

#define	RATE_INFO_FLAGS_MCS			BIT(0)
#define	RATE_INFO_FLAGS_SHORT_GI		BIT(1)
#define	RATE_INFO_FLAGS_VHT_MCS			BIT(2)
#define	RATE_INFO_BW_20				0
#define	RATE_INFO_BW_40				1
#define	RATE_INFO_BW_80				2

#define	BSS_CHANGED_ASSOC			BIT(0)
#define	BSS_CHANGED_BASIC_RATES			BIT(1)
#define	BSS_CHANGED_BEACON_INT			BIT(2)
#define	BSS_CHANGED_BSSID			BIT(3)
#define	BSS_CHANGED_BEACON			BIT(4)
#define	BSS_CHANGED_ERP_SLOT			BIT(6)
#define	BSS_CHANGED_ERP_PREAMBLE		BIT(7)
#define	BSS_CHANGED_HT				BIT(9)
#define	BSS_CHANGED_BEACON_ENABLED		BIT(10)
#define	BSS_CHANGED_CQM				BIT(11)
#define	BSS_CHANGED_QOS				BIT(14)
#define	BSS_CHANGED_TXPOWER			BIT(15)
#define	BSS_CHANGED_ARP_FILTER			BIT(18)
#define	BSS_CHANGED_PS				BIT(21)
#define	BSS_CHANGED_DTIM_PERIOD			BIT(24)
#define	BSS_CHANGED_P2P_PS			BIT(26)
#define	BSS_CHANGED_MU_GROUPS			BIT(17)

/* station_info.filled bits (the sta_statistics op reports) */
#define	NL80211_STA_INFO_RX_BITRATE		BIT(4)
#define	NL80211_STA_INFO_TX_BITRATE		BIT(5)
#define	NL80211_STA_INFO_SIGNAL			BIT(6)
#define	NL80211_STA_INFO_TX_FAILED		BIT(13)

/* 802.11 frame control */
#define	IEEE80211_FCTL_FTYPE		0x000c
#define	IEEE80211_FCTL_STYPE		0x00f0
#define	IEEE80211_FCTL_TODS		0x0100
#define	IEEE80211_FCTL_FROMDS		0x0200
#define	IEEE80211_FCTL_MOREFRAGS	0x0400
#define	IEEE80211_FCTL_PM		0x1000
#define	IEEE80211_FCTL_PROTECTED	0x4000
#define	IEEE80211_FTYPE_MGMT		0x0000
#define	IEEE80211_FTYPE_CTL		0x0004
#define	IEEE80211_FTYPE_DATA		0x0008
#define	IEEE80211_STYPE_ASSOC_REQ	0x0000
#define	IEEE80211_STYPE_ASSOC_RESP	0x0010
#define	IEEE80211_STYPE_REASSOC_REQ	0x0020
#define	IEEE80211_STYPE_REASSOC_RESP	0x0030
#define	IEEE80211_STYPE_PROBE_REQ	0x0040
#define	IEEE80211_STYPE_PROBE_RESP	0x0050
#define	IEEE80211_STYPE_BEACON		0x0080
#define	IEEE80211_STYPE_ATIM		0x0090
#define	IEEE80211_STYPE_DISASSOC	0x00a0
#define	IEEE80211_STYPE_AUTH		0x00b0
#define	IEEE80211_STYPE_DEAUTH		0x00c0
#define	IEEE80211_STYPE_ACTION		0x00d0
#define	IEEE80211_STYPE_BACK_REQ	0x0080
#define	IEEE80211_STYPE_BACK		0x0090
#define	IEEE80211_STYPE_PSPOLL		0x00a0
#define	IEEE80211_STYPE_RTS		0x00b0
#define	IEEE80211_STYPE_CTS		0x00c0
#define	IEEE80211_STYPE_ACK		0x00d0
#define	IEEE80211_STYPE_CFEND		0x00e0
#define	IEEE80211_STYPE_CFENDACK	0x00f0
#define	IEEE80211_STYPE_DATA		0x0000
#define	IEEE80211_STYPE_NULLFUNC	0x0040
#define	IEEE80211_STYPE_QOS_DATA	0x0080
#define	IEEE80211_STYPE_QOS_NULLFUNC	0x00c0
#define	IEEE80211_SCTL_SEQ		0xfff0
#define	IEEE80211_SCTL_FRAG		0x000f

#define	IEEE80211_HDRLEN		24
#define	IEEE80211_QOS_CTL_LEN		2
#define	IEEE80211_MAX_LEN		(IEEE80211_MAX_FRAME_LEN + FCS_LEN + 2)

#define	IEEE80211_SKB_CB(skb)	((struct ieee80211_tx_info *)((skb)->cb))
#define	IEEE80211_TX_INFO_MAX_RATES	4

/* ------------------------------------------------------------------ */
/* 802.11 headers                                                      */
/* ------------------------------------------------------------------ */

struct ieee80211_hdr {
	__le16		frame_control;
	__le16		duration_id;
	u8		addr1[6];
	u8		addr2[6];
	u8		addr3[6];
	__le16		seq_ctrl;
	u8		addr4[6];
} __packed;

struct ieee80211_hdr_3addr {
	__le16		frame_control;
	__le16		duration_id;
	u8		addr1[6];
	u8		addr2[6];
	u8		addr3[6];
	__le16		seq_ctrl;
} __packed;

struct ieee80211_qos_hdr {
	__le16		frame_control;
	__le16		duration_id;
	u8		addr1[6];
	u8		addr2[6];
	u8		addr3[6];
	__le16		seq_ctrl;
	__le16		qos_ctrl;
} __packed;

struct ieee80211_mgmt {
	__le16		frame_control;
	__le16		duration;
	u8		da[6];
	u8		sa[6];
	u8		bssid[6];
	__le16		seq_ctrl;
	union {
		struct {
			__le64	timestamp;
			__le16	beacon_int;
			__le16	capab_info;
			u8	variable[1];
		} __packed beacon;
		struct {
			__le64	timestamp;
			__le16	beacon_int;
			__le16	capab_info;
			u8	variable[1];
		} __packed probe_resp;
		struct {
			__le16	capab_info;
			__le16	listen_int;
			u8	variable[1];
		} __packed assoc_req;
		struct {
			__le16	capab_info;
			__le16	status_code;
			__le16	aid;
			u8	variable[1];
		} __packed assoc_resp;
		struct {
			__le16	auth_alg;
			__le16	auth_transaction;
			__le16	status_code;
			u8	variable[1];
		} __packed auth;
		struct {
			__le16	reason_code;
			u8	variable[1];
		} __packed deauth;
		struct {
			__le16	reason_code;
			u8	variable[1];
		} __packed disassoc;
		struct {
			u8	variable[1];
		} __packed action;
	} u;
} __packed;

static __always_inline __unused int
ieee80211_is_mgmt(__le16 fc)
{
	return (le16toh(fc) & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT;
}

static __always_inline __unused int
ieee80211_is_data(__le16 fc)
{
	return (le16toh(fc) & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA;
}

static __always_inline __unused int
ieee80211_is_ctl(__le16 fc)
{
	return (le16toh(fc) & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_CTL;
}

static __always_inline __unused int
ieee80211_is_beacon(__le16 fc)
{
	return (le16toh(fc) & (IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	    (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACON);
}

static __always_inline __unused int
ieee80211_is_probe_req(__le16 fc)
{
	return (le16toh(fc) & (IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	    (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_REQ);
}

static __always_inline __unused int
ieee80211_is_probe_resp(__le16 fc)
{
	return (le16toh(fc) & (IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	    (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_RESP);
}

static __always_inline __unused int
ieee80211_is_assoc_req(__le16 fc)
{
	return (le16toh(fc) & (IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	    (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ASSOC_REQ);
}

static __always_inline __unused int
ieee80211_is_auth(__le16 fc)
{
	return (le16toh(fc) & (IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	    (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_AUTH);
}

static __always_inline __unused int
ieee80211_is_deauth(__le16 fc)
{
	return (le16toh(fc) & (IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	    (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_DEAUTH);
}

static __always_inline __unused int
ieee80211_is_disassoc(__le16 fc)
{
	return (le16toh(fc) & (IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	    (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_DISASSOC);
}

static __always_inline __unused int
ieee80211_is_action(__le16 fc)
{
	return (le16toh(fc) & (IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	    (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ACTION);
}

static __always_inline __unused int
ieee80211_is_data_qos(__le16 fc)
{
	return (le16toh(fc) & (IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_DATA)) ==
	    (IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_DATA);
}

static __always_inline __unused int
ieee80211_is_nullfunc(__le16 fc)
{
	return (le16toh(fc) & (IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC)) ==
	    (IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC);
}

static __always_inline __unused int
ieee80211_is_any_nullfunc(__le16 fc)
{
	u16 v = le16toh(fc) & (IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE);

	return v == (IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC) ||
	    v == (IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_NULLFUNC);
}

static __always_inline __unused int
ieee80211_has_tods(__le16 fc)
{
	return (le16toh(fc) & IEEE80211_FCTL_TODS) != 0;
}

static __always_inline __unused int
ieee80211_has_fromds(__le16 fc)
{
	return (le16toh(fc) & IEEE80211_FCTL_FROMDS) != 0;
}

static __always_inline __unused int
ieee80211_has_a4(__le16 fc)
{
	return (le16toh(fc) & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) ==
	    (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS);
}

static __always_inline __unused int
ieee80211_is_protected(__le16 fc)
{
	return (le16toh(fc) & IEEE80211_FCTL_PROTECTED) != 0;
}

static __always_inline __unused int
ieee80211_is_first_frag(__le16 seq)
{
	return (le16toh(seq) & IEEE80211_SCTL_FRAG) == 0;
}

static __always_inline __unused u16
ieee80211_get_tid(struct ieee80211_hdr *hdr)
{
	u8 *qc = (u8 *)hdr + IEEE80211_HDRLEN;

	return qc[0] & 0x0f;
}

static __always_inline __unused u8 *
ieee80211_get_qos_ctl(struct ieee80211_hdr *hdr)
{

	return (u8 *)hdr + IEEE80211_HDRLEN;
}

/* ------------------------------------------------------------------ */
/* cfg80211 types                                                      */
/* ------------------------------------------------------------------ */

struct ieee80211_channel {
	u16			center_freq;
	u16			hw_value;
	u32			flags;
	u8			band;
	u8			max_antenna_gain;
	s8			max_power;
	s32			max_reg_power;
	bool			beacon_found;
};

struct ieee80211_rate {
	u32			flags;
	u16			bitrate;
	u16			hw_value;
	u16			hw_value_short;
};

enum nl80211_bss_scan_width {
	NL80211_BSS_CHAN_WIDTH_20 = 0,
};

struct ieee80211_sta_ht_cap {
	u16			cap;
	bool			ht_supported;
	u8			ampdu_factor;
	u8			ampdu_density;
	struct {
		u8		rx_mask[10];
		u16		rx_highest;
		u8		tx_params;
	} mcs;
};

struct ieee80211_sta_vht_cap {
	bool			vht_supported;
	u32			cap;
	struct {
		__le16		rx_mcs_map;
		__le16		tx_mcs_map;
		__le16		rx_highest;
		__le16		tx_highest;
	} vht_mcs;
};

struct ieee80211_supported_band {
	const struct ieee80211_channel *channels;
	const struct ieee80211_rate *bitrates;
	enum nl80211_band	band;
	int			n_channels;
	int			n_bitrates;
	struct ieee80211_sta_ht_cap ht_cap;
	struct ieee80211_sta_vht_cap vht_cap;
};

struct cfg80211_chan_def {
	struct ieee80211_channel *chan;
	enum nl80211_chan_width	width;
	u32			center_freq1;
	u32			center_freq2;
};

struct cfg80211_ssid {
	u8			ssid[IEEE80211_MAX_SSID_LEN];
	u8			ssid_len;
};

struct cfg80211_bitrate_mask {
	struct {
		u32		legacy;
		u8		ht_mcs[10];
		u16		vht_mcs[8];
	} control[IEEE80211_NUM_BANDS];
};

struct cfg80211_sar_freq_ranges {
	u32			start_freq;
	u32			end_freq;
};

struct cfg80211_sar_capa {
	enum nl80211_sar_type	type;
	u32			num_freq_ranges;
	const struct cfg80211_sar_freq_ranges *freq_ranges;
};

struct cfg80211_sar_sub_specs {
	u32			freq_range_index;
	s32			power;
};

struct cfg80211_sar_specs {
	enum nl80211_sar_type	type;
	u32			num_sub_specs;
	const struct cfg80211_sar_sub_specs *sub_specs;
};

struct cfg80211_scan_info {
	u64			scan_start_tsf;
	u8			tsf_bssid[6];
	bool			aborted;
};

struct cfg80211_sched_scan_plan {
	u32			interval;
	u32			iterations;
};

struct cfg80211_match_set {
	struct cfg80211_ssid	ssid;
	u8			bssid[6];
	s32			rssi_thold;
	s32			per_band_rssi_thold[IEEE80211_NUM_BANDS];
};

struct cfg80211_scan_request {
	struct cfg80211_ssid	*ssids;
	struct ieee80211_channel **channels;
	u8			n_ssids;
	u32			n_channels;
	u32			flags;
	u32			duration;
	bool			duration_mandatory;
	bool			no_cck;
	size_t			ie_len;
	u8			mac_addr[6];
	u8			mac_addr_mask[6];
};

struct cfg80211_wowlan {
	bool			any;
	bool			disconnect;
	bool			magic_pkt;
	bool			gtk_rekey_failure;
	bool			eap_identity_req;
	bool			four_way_handshake;
	bool			rfkill_release;
	struct cfg80211_sched_scan_plan *patterns;
	bool			tcp;
	bool			net_detect;
};

struct wiphy_wowlan_support {
	u32			flags;
	int			n_patterns;
	int			pattern_max_len;
	int			pattern_min_len;
	int			max_pkt_offset;
};

struct ieee80211_iface_limit {
	u16			max;
	u16			types;
};

struct ieee80211_iface_combination {
	const struct ieee80211_iface_limit *limits;
	u32			num_different_channels;
	u16			max_interfaces;
	u8			n_limits;
	bool			beacon_int_infra_match;
	u8			radar_detect_widths;
	u8			radar_detect_regions;
};

struct ieee80211_tx_queue_params {
	u16			aifs;
	u16			cw_min;
	u16			cw_max;
	u16			txop;
	bool			acm;
	bool			uapsd;
};

struct ieee80211_scan_ies {
	const u8		*ies[IEEE80211_NUM_BANDS];
	size_t			len[IEEE80211_NUM_BANDS];
	const u8		*common_ies;
	size_t			common_ie_len;
};

struct ieee80211_scan_request {
	struct cfg80211_scan_request req;
	struct list_head	scan_list;
	struct ieee80211_scan_ies ies;
};

enum nl80211_dfs_regions {
	NL80211_DFS_REGION_UNSET = NL80211_DFS_UNSET,
	NL80211_DFS_REGION_FCC = NL80211_DFS_FCC,
	NL80211_DFS_REGION_ETSI = NL80211_DFS_ETSI,
	NL80211_DFS_REGION_JP = NL80211_DFS_JP,
};

struct regulatory_request {
	char			alpha2[2];
	enum nl80211_dfs_regions dfs_region;
	u32			initiator;
};

/*
 * The wiphy is large in Linux; only the members the drivers touch are
 * present.  ieee80211_hw embeds one so that hw->wiphy is always valid.
 */
struct wiphy {
	struct ieee80211_supported_band *bands[IEEE80211_NUM_BANDS];
	u32			flags;
	u32			features;
	u32			regulatory_flags;
	u16			interface_modes;
	u32			available_antennas_tx;
	u32			available_antennas_rx;
	int			max_scan_ssids;
	int			max_sched_scan_ssids;
	u16			max_scan_ie_len;
	u32			rts_threshold;
	u8			fw_version[32];
	void			(*reg_notifier)(struct wiphy *,
				    struct regulatory_request *);
	const struct ieee80211_iface_combination *iface_combinations;
	int			n_iface_combinations;
	const struct cfg80211_sar_capa *sar_capa;
	const struct wiphy_wowlan_support *wowlan;
	void			*debugfsdir;
	u8			perm_addr[ETH_ALEN];
};

/* ------------------------------------------------------------------ */
/* mac80211 types                                                      */
/* ------------------------------------------------------------------ */

struct ieee80211_conf {
	u32			flags;
	int			power_level;
	int			max_power_level;
	struct cfg80211_chan_def chandef;
};

/* enough of mac80211's rate_info for the RA report */
struct rate_info {
	u8			flags;
	u8			legacy;
	u8			mcs;
	u8			nss;
	u8			bw;
	u8			he_gi;
	u16			he_ru_alloc;
};

/* enough of cfg80211's station_info for the sta_statistics op */
struct station_info {
	struct rate_info	rxrate;
	struct rate_info	txrate;
	u32			filled;
	s8			signal;
	u16			rx_bytes;
	u32			tx_failed;
};

struct ieee80211_bss_conf {
	u8			bssid[ETH_ALEN];
	bool			assoc;
	u32			basic_rates;
	u16			aid;
	u16			beacon_int;
	bool			enable_beacon;
	bool			use_short_slot;
	bool			use_short_preamble;
	bool			use_cts_prot;
	s32			cqm_rssi_thold;
	u8			cqm_rssi_hyst;
	u8			dtim_period;
	bool			he_support;
	struct {
		u8		membership[8];
		u8		position[16];
	} mu_group;
};

struct ieee80211_tx_control {
	struct ieee80211_sta	*sta;
	struct ieee80211_txq	*txq;
};

struct ieee80211_key_conf {
	u32			cipher;
	u8			icv_len;
	u8			iv_len;
	u8			hw_key_idx;
	u8			keyidx;
	u32			flags;
	u8			keylen;
	u8			key[32];
};

struct ieee80211_chanctx_conf {
	struct cfg80211_chan_def def;
	u8			drv_priv[64] __aligned(sizeof(void *));
};

struct ieee80211_chanctx {
	struct ieee80211_chanctx_conf conf;
	u8			drv_priv[64] __aligned(sizeof(void *));
};

struct ieee80211_ampdu_params {
	struct ieee80211_sta	*sta;
	enum ieee80211_ampdu_mlme_action action;
	u16			tid;
	u16			ssn;
	u8			buf_size;
	bool			amsdu;
	u16			timeout;
};

struct ieee80211_hw {
	struct ieee80211_conf	conf;
	struct wiphy		wiphy_storage;
	struct wiphy		*wiphy;
	const struct ieee80211_ops *ops;
	void			*priv;
	unsigned long		flags;
	u32			extra_tx_headroom;
	u32			queues;
	int			txq_data_size;
	int			sta_data_size;
	int			vif_data_size;
};

struct ieee80211_vif {
	enum nl80211_iftype	type;
	u8			addr[ETH_ALEN];
	struct ieee80211_bss_conf bss_conf;
	u32			driver_flags;
	bool			p2p;
	struct {
		bool		assoc;
		bool		ps;
		u16		aid;
	} cfg;
	struct ieee80211_txq	*txq;
	u8			drv_priv[128] __aligned(sizeof(void *));
};

struct ieee80211_sta {
	u8			addr[ETH_ALEN];
	u16			aid;
	bool			tdls;
	u8			bandwidth;
	u8			max_rc_amsdu_len;
	struct ieee80211_sta_ht_cap ht_cap;
	struct ieee80211_sta_vht_cap vht_cap;
	struct ieee80211_txq	*txq[IEEE80211_NUM_TIDS];
	u8			supp_rates[IEEE80211_NUM_BANDS];
	struct {
		u8		bandwidth;
		struct {
			u16	max_rc_amsdu_len;
		} agg;
		struct ieee80211_sta_ht_cap ht_cap;
		struct ieee80211_sta_vht_cap vht_cap;
		u8		supp_rates[IEEE80211_NUM_BANDS];
	} deflink;
	u8			drv_priv[128] __aligned(sizeof(void *));
};

struct ieee80211_txq {
	u8			ac;
	u8			tid;
	struct ieee80211_sta	*sta;
	struct ieee80211_vif	*vif;
	u8			drv_priv[64] __aligned(sizeof(void *));
};

struct ieee80211_tx_info {
	u32			flags;
	u8			band;
	u8			hw_queue;
	struct {
		struct ieee80211_key_conf *hw_key;
		struct ieee80211_vif *vif;
		struct ieee80211_sta *sta;
		bool		use_rts;
		bool		use_cts_prot;
		u8		flags;
		struct ieee80211_tx_rate rates[IEEE80211_TX_INFO_MAX_RATES];
	} control;
	struct {
		struct ieee80211_tx_rate rates[IEEE80211_TX_INFO_MAX_RATES];
		u32		ampdu_ack_len;
		u32		ampdu_len;
		u8		ack_signal;
		u8		status_driver_data[16];
	} status;
	void			*driver_data[8];
};

struct ieee80211_rx_status {
	u64			mactime;
	u64			boottime_ns;
	u32			device_timestamp;
	u16			freq;
	u16			flag;
	u8			band;
	u8			encoding;
	u8			bw;
	u8			nss;
	u8			chains;
	s8			signal;
	s8			chain_signal[IEEE80211_MAX_CHAINS];
	u8			rate_idx;
	s8			vht_flag;
	u8			zero_length_psdu_type;
};

/*
 * The real ops table: the signatures are Linux 6.12's, verbatim - the
 * bridge dispatches through them, so they must not drift from the
 * driver's op implementations.
 */
struct ieee80211_vif_chanctx_switch {
	struct ieee80211_vif		*vif;
	struct ieee80211_chanctx_conf	*old_ctx;
	struct cfg80211_chan_def	old_chandef;
	struct ieee80211_chanctx_conf	*new_ctx;
	struct cfg80211_chan_def	new_chandef;
};

struct ieee80211_ops {
	void	(*tx)(struct ieee80211_hw *hw,
		    struct ieee80211_tx_control *control,
		    struct sk_buff *skb);
	int	(*start)(struct ieee80211_hw *hw);
	void	(*stop)(struct ieee80211_hw *hw, bool suspend);
	int	(*config)(struct ieee80211_hw *hw, u32 changed);
	int	(*add_interface)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif);
	void	(*remove_interface)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif);
	int	(*change_interface)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif,
		    enum nl80211_iftype new_type, bool p2p);
	void	(*configure_filter)(struct ieee80211_hw *hw,
		    unsigned int changed_flags, unsigned int *total_flags,
		    u64 multicast);
	void	(*bss_info_changed)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif,
		    struct ieee80211_bss_conf *info, u64 changed);
	int	(*start_ap)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif,
		    struct ieee80211_bss_conf *link_conf);
	void	(*stop_ap)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif,
		    struct ieee80211_bss_conf *link_conf);
	int	(*conf_tx)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif, u16 ac,
		    const struct ieee80211_tx_queue_params *params);
	int	(*sta_add)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif, struct ieee80211_sta *sta);
	int	(*sta_remove)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif, struct ieee80211_sta *sta);
	int	(*set_tim)(struct ieee80211_hw *hw,
		    struct ieee80211_sta *sta, bool set);
	int	(*set_key)(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		    struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		    struct ieee80211_key_conf *key);
	int	(*ampdu_action)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif,
		    struct ieee80211_ampdu_params *params);
	bool	(*can_aggregate_in_amsdu)(struct ieee80211_hw *hw,
		    struct sk_buff *head, struct sk_buff *skb);
	void	(*sw_scan_start)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif, const u8 *mac_addr);
	void	(*sw_scan_complete)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif);
	void	(*mgd_prepare_tx)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif, u16 duration);
	int	(*set_rts_threshold)(struct ieee80211_hw *hw, u32 value);
	int	(*sta_statistics)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		    struct station_info *sinfo);
	void	(*flush)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    u32 queues, bool drop);
	int	(*set_bitrate_mask)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif,
		    const struct cfg80211_bitrate_mask *mask);
	int	(*set_antenna)(struct ieee80211_hw *hw, u32 tx_ant,
		    u32 rx_ant);
	int	(*get_antenna)(struct ieee80211_hw *hw, u32 *tx_ant,
		    u32 *rx_ant);
	void	(*reconfig_complete)(struct ieee80211_hw *hw,
		    enum ieee80211_reconfig_type type);
	int	(*hw_scan)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif,
		    struct ieee80211_scan_request *hw_req);
	void	(*cancel_hw_scan)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif);
	void	(*link_sta_rc_update)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif,
		    struct ieee80211_link_sta *link_sta, u32 changed);
	void	(*sta_rc_update)(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		    u32 changed);
	int	(*set_sar_specs)(struct ieee80211_hw *hw,
		    const struct cfg80211_sar_specs *sar);
	int	(*wake_tx_queue)(struct ieee80211_hw *hw,
		    struct ieee80211_txq *txq);
	int	(*add_chanctx)(struct ieee80211_hw *hw,
		    struct ieee80211_chanctx *ctx);
	void	(*remove_chanctx)(struct ieee80211_hw *hw,
		    struct ieee80211_chanctx *ctx);
	int	(*change_chanctx)(struct ieee80211_hw *hw,
		    struct ieee80211_chanctx *ctx, u32 changed);
	int	(*switch_vif_chanctx)(struct ieee80211_hw *hw,
		    struct ieee80211_vif_chanctx_switch *vifs, int n_vifs);
};

static __always_inline __unused int
ieee80211_emulate_add_chanctx(struct ieee80211_hw *hw,
    struct ieee80211_chanctx *ctx)
{
	(void)hw;
	(void)ctx;
	return 0;
}

static __always_inline __unused void
ieee80211_emulate_remove_chanctx(struct ieee80211_hw *hw,
    struct ieee80211_chanctx *ctx)
{
	(void)hw;
	(void)ctx;
}

static __always_inline __unused int
ieee80211_emulate_change_chanctx(struct ieee80211_hw *hw,
    struct ieee80211_chanctx *ctx, u32 changed)
{
	(void)hw;
	(void)ctx;
	(void)changed;
	return 0;
}

static __always_inline __unused int
ieee80211_emulate_switch_vif_chanctx(struct ieee80211_hw *hw,
    struct ieee80211_vif_chanctx_switch *vifs, int n_vifs)
{
	(void)hw;
	(void)vifs;
	(void)n_vifs;
	return 0;
}

/* ------------------------------------------------------------------ */
/* mac80211 entry points - implemented in linuxkpi_80211.c              */
/* ------------------------------------------------------------------ */

struct ieee80211_hw *ieee80211_alloc_hw(size_t priv_data_len,
    const struct ieee80211_ops *ops);
void ieee80211_free_hw(struct ieee80211_hw *hw);
struct led_trigger *ieee80211_create_tpt_led_trigger(struct ieee80211_hw *hw,
    unsigned int flags, const struct ieee80211_tpt_blink *blink_table,
    unsigned int blink_table_len);
struct led_trigger;
int	ieee80211_register_hw(struct ieee80211_hw *);
void	ieee80211_unregister_hw(struct ieee80211_hw *);
void	ieee80211_restart_hw(struct ieee80211_hw *);
void	ieee80211_queue_work(struct ieee80211_hw *, struct work_struct *);
void	ieee80211_queue_delayed_work(struct ieee80211_hw *,
	    struct delayed_work *, unsigned long);
void	ieee80211_wake_queues(struct ieee80211_hw *);
void	ieee80211_stop_queues(struct ieee80211_hw *);
void	ieee80211_tx_status_irqsafe(struct ieee80211_hw *, struct sk_buff *);
void	ieee80211_free_txskb(struct ieee80211_hw *, struct sk_buff *);
void	ieee80211_tx_info_clear_status(struct ieee80211_tx_info *);
struct sk_buff *ieee80211_tx_dequeue(struct ieee80211_hw *,
	    struct ieee80211_txq *);
void	ieee80211_purge_tx_queue(struct ieee80211_hw *, struct sk_buff_head *);
int	ieee80211_txq_get_depth(struct ieee80211_txq *, unsigned long *,
	    unsigned long *);
void	ieee80211_rx_irqsafe(struct ieee80211_hw *, struct sk_buff *);
void	ieee80211_rx_napi(struct ieee80211_hw *, struct ieee80211_sta *,
	    struct sk_buff *, struct napi_struct *);
struct ieee80211_sta *ieee80211_find_sta(struct ieee80211_vif *,
	    const u8 *);
struct ieee80211_sta *ieee80211_find_sta_by_ifaddr(struct ieee80211_hw *,
	    const u8 *, const u8 *);
void	ieee80211_iterate_active_interfaces_atomic(struct ieee80211_hw *,
	    u32, void (*)(void *, u8 *, struct ieee80211_vif *), void *);
void	ieee80211_iterate_active_interfaces(struct ieee80211_hw *, u32,
	    void (*)(void *, u8 *, struct ieee80211_vif *), void *);
void	ieee80211_iterate_stations_atomic(struct ieee80211_hw *,
	    void (*)(void *, struct ieee80211_sta *), void *);
void	ieee80211_iterate_stations(struct ieee80211_hw *,
	    void (*)(void *, struct ieee80211_sta *), void *);
void	ieee80211_iter_keys(struct ieee80211_hw *, struct ieee80211_vif *,
	    void (*)(struct ieee80211_hw *, struct ieee80211_vif *,
	    struct ieee80211_sta *, struct ieee80211_key_conf *, void *),
	    void *);
void	ieee80211_iter_keys_rcu(struct ieee80211_hw *, struct ieee80211_vif *,
	    void (*)(struct ieee80211_hw *, struct ieee80211_vif *,
	    struct ieee80211_sta *, struct ieee80211_key_conf *, void *),
	    void *);
void	ieee80211_request_smps(struct ieee80211_vif *, int,
	    enum ieee80211_smps_mode);
int	ieee80211_start_tx_ba_session(struct ieee80211_sta *, u16, u16);
void	ieee80211_start_tx_ba_cb_irqsafe(struct ieee80211_vif *,
	    const u8 *, u16);
void	ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_vif *,
	    const u8 *, u16);
void	ieee80211_scan_completed(struct ieee80211_hw *,
	    struct cfg80211_scan_info *);
void	ieee80211_connection_loss(struct ieee80211_vif *);
void	ieee80211_cqm_rssi_notify(struct ieee80211_vif *,
	    enum nl80211_cqm_rssi_threshold_event, s32, gfp_t);
void	ieee80211_report_low_ack(struct ieee80211_sta *);
struct sk_buff *ieee80211_nullfunc_get(struct ieee80211_hw *,
	    struct ieee80211_vif *, int, bool);
struct sk_buff *ieee80211_probereq_get(struct ieee80211_hw *, const u8 *,
	    const u8 *, size_t, size_t);
struct sk_buff *ieee80211_proberesp_get(struct ieee80211_hw *,
	    struct ieee80211_vif *);
struct sk_buff *ieee80211_pspoll_get(struct ieee80211_hw *,
	    struct ieee80211_vif *);
struct sk_buff *ieee80211_beacon_get_tim(struct ieee80211_hw *,
	    struct ieee80211_vif *, u16 *, u16 *, int);
struct wiphy *wiphy_to_wiphy(struct ieee80211_hw *);
struct ieee80211_hw *wiphy_to_ieee80211_hw(struct wiphy *);
int	regulatory_hint(struct wiphy *, const char *);
void	get_random_mask_addr(u8 *, const u8 *, const u8 *);

static __always_inline __unused void
ieee80211_hw_set_flag(struct ieee80211_hw *hw, unsigned int flag)
{
	set_bit(flag, &hw->flags);
}

#define	ieee80211_hw_set(hw, _name)	\
	ieee80211_hw_set_flag((hw), IEEE80211_HW_##_name)

#define	ieee80211_hw_check(hw, _name)	\
	test_bit(IEEE80211_HW_##_name, &(hw)->flags)

static __always_inline __unused void
wiphy_ext_feature_set(struct wiphy *wiphy, unsigned int feature)
{
	set_bit(feature, (unsigned long *)&wiphy->features);
}

#define	SET_IEEE80211_DEV(hw, dev) \
	do { (void)(hw); (void)(dev); } while (0)
#define	SET_IEEE80211_PERM_ADDR(hw, addr)				\
	do {								\
		memcpy((hw)->wiphy_storage.perm_addr, (addr), ETH_ALEN); \
	} while (0)

static __always_inline __unused struct wiphy *
hw_to_wiphy(struct ieee80211_hw *hw)
{
	return &hw->wiphy_storage;
}

static __always_inline __unused int
ieee80211_channel_to_frequency(int chan, enum nl80211_band band)
{
	if (band == NL80211_BAND_2GHZ) {
		if (chan == 14)
			return 2484;
		return 2407 + chan * 5;
	}
	return 5000 + chan * 5;
}

static __always_inline __unused int
ieee80211_frequency_to_channel(int freq)
{
	if (freq == 2484)
		return 14;
	if (freq < 2484)
		return (freq - 2407) / 5;
	return (freq - 5000) / 5;
}

static __always_inline __unused int
cfg80211_get_ies_channel_number(const u8 *ies, size_t ielen,
    enum nl80211_band band)
{
	(void)ies;
	(void)ielen;
	(void)band;
	return 1;
}

static __always_inline __unused u32
cfg80211_calculate_bitrate(void *rate)
{
	(void)rate;
	return 0;
}

static __always_inline __unused bool
cfg80211_ssid_eq(struct cfg80211_ssid *a, struct cfg80211_ssid *b)
{
	if (a == NULL || b == NULL || a->ssid_len != b->ssid_len)
		return false;
	return memcmp(a->ssid, b->ssid, a->ssid_len) == 0;
}

static __always_inline __unused enum nl80211_iftype
ieee80211_vif_type_p2p(struct ieee80211_vif *vif)
{
	/* P2P roles collapse onto their infra parents: no P2P in this port */
	return vif->type;
}

#endif /* _LINUXKPI_NET_MAC80211_H_ */
