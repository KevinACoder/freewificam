/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    net/cfg80211.h
 * @brief   The configuration surface a Linux wireless driver talks to: the wiphy
 *          object, bands/channels/rates, interface combinations and the station rate
 *          reporting types.
 *
 * @design declaration-only compatibility face. Definitions are the interface's
 *         (include/net/cfg80211.h plus the uapi enumerations it includes), transcribed
 *         with their exact values because several of them are written into hardware
 *         descriptors or compared against hardware register fields; the bodies are
 *         first-party and live in the mac80211 face of this project. Subset by
 *         consumer: the regulatory, scan, connection and mesh surface arrives when a
 *         consumer needs it. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    08.10.2026
 */

#ifndef _LINUXKPI_NET_CFG80211_H_
#define _LINUXKPI_NET_CFG80211_H_

#include "../../linuxkpi_core.h"
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>

/* ------------------------------------------------------------------ */
/* band, interface type and channel width                              */
/* ------------------------------------------------------------------ */

enum nl80211_band {
	NL80211_BAND_2GHZ,
	NL80211_BAND_5GHZ,
	NL80211_BAND_60GHZ,
	NL80211_BAND_6GHZ,
	NL80211_BAND_S1GHZ,
	NL80211_BAND_LC,

	NUM_NL80211_BANDS,
};

enum nl80211_iftype {
	NL80211_IFTYPE_UNSPECIFIED,
	NL80211_IFTYPE_ADHOC,
	NL80211_IFTYPE_STATION,
	NL80211_IFTYPE_AP,
	NL80211_IFTYPE_AP_VLAN,
	NL80211_IFTYPE_WDS,
	NL80211_IFTYPE_MONITOR,
	NL80211_IFTYPE_MESH_POINT,
	NL80211_IFTYPE_P2P_CLIENT,
	NL80211_IFTYPE_P2P_GO,
	NL80211_IFTYPE_P2P_DEVICE,
	NL80211_IFTYPE_OCB,
	NL80211_IFTYPE_NAN,

	NUM_NL80211_IFTYPES,
};

enum nl80211_chan_width {
	NL80211_CHAN_WIDTH_20_NOHT,
	NL80211_CHAN_WIDTH_20,
	NL80211_CHAN_WIDTH_40,
	NL80211_CHAN_WIDTH_80,
	NL80211_CHAN_WIDTH_80P80,
	NL80211_CHAN_WIDTH_160,
	NL80211_CHAN_WIDTH_5,
	NL80211_CHAN_WIDTH_10,
	NL80211_CHAN_WIDTH_1,
	NL80211_CHAN_WIDTH_2,
	NL80211_CHAN_WIDTH_4,
	NL80211_CHAN_WIDTH_8,
	NL80211_CHAN_WIDTH_16,
};

/* ------------------------------------------------------------------ */
/* channels, rates and bands                                           */
/* ------------------------------------------------------------------ */

/* Channel flags the band tables of a 2.4 GHz-only part leave clear. */
#define	IEEE80211_CHAN_DISABLED		0x0001
#define	IEEE80211_CHAN_NO_IR		0x0002
#define	IEEE80211_CHAN_RADAR		0x0008
#define	IEEE80211_CHAN_NO_HT40PLUS	0x0010
#define	IEEE80211_CHAN_NO_HT40MINUS	0x0020
#define	IEEE80211_CHAN_NO_20MHZ		0x1000

struct ieee80211_channel {
	enum nl80211_band	band;
	u32			center_freq;
	u16			freq_offset;
	u16			hw_value;
	u32			flags;
	int			max_antenna_gain;
	int			max_power;
	int			max_reg_power;
	bool			beacon_found;
	unsigned int		dfs_cac_ms;
};

/* Rate flags: the bitrate field is in units of 100 kbit/s. */
#define	IEEE80211_RATE_SHORT_PREAMBLE	0x0001
#define	IEEE80211_RATE_MANDATORY_A	0x0004
#define	IEEE80211_RATE_ERP_G		0x0010
#define	IEEE80211_RATE_SUPPORTS_5MHZ	0x0020
#define	IEEE80211_RATE_SUPPORTS_10MHZ	0x0040

struct ieee80211_rate {
	u32	flags;
	u16	bitrate;
	u16	hw_value;
	u16	hw_value_short;
};

struct ieee80211_sta_ht_cap {
	u16				cap;	/* IEEE80211_HT_CAP_* */
	bool				ht_supported;
	u8				ampdu_factor;
	u8				ampdu_density;
	struct ieee80211_mcs_info	mcs;
};

struct ieee80211_sta_vht_cap {
	bool				vht_supported;
	u32				cap;
	struct ieee80211_vht_mcs_info	vht_mcs;
};

struct ieee80211_supported_band {
	struct ieee80211_channel	*channels;
	struct ieee80211_rate		*bitrates;
	enum nl80211_band		band;
	int				n_channels;
	int				n_bitrates;
	struct ieee80211_sta_ht_cap	ht_cap;
	struct ieee80211_sta_vht_cap	vht_cap;
};

struct cfg80211_chan_def {
	struct ieee80211_channel	*chan;
	enum nl80211_chan_width		width;
	u32				center_freq1;
	u32				center_freq2;
};

/* ------------------------------------------------------------------ */
/* interface combinations                                              */
/* ------------------------------------------------------------------ */

struct ieee80211_iface_limit {
	u16	max;
	u16	types;
};

struct ieee80211_iface_combination {
	const struct ieee80211_iface_limit	*limits;
	u32					num_different_channels;
	u16					max_interfaces;
	u8					n_limits;
	bool					beacon_int_infra_match;
	u8					radar_detect_widths;
	u8					radar_detect_regions;
	u32					beacon_int_min_gcd;
};

/* ------------------------------------------------------------------ */
/* what a driver reports about a peer's rate                           */
/* ------------------------------------------------------------------ */

enum rate_info_flags {
	RATE_INFO_FLAGS_MCS		= BIT(0),
	RATE_INFO_FLAGS_VHT_MCS		= BIT(1),
	RATE_INFO_FLAGS_SHORT_GI	= BIT(2),
	RATE_INFO_FLAGS_DMG		= BIT(3),
	RATE_INFO_FLAGS_HE_MCS		= BIT(4),
	RATE_INFO_FLAGS_EDMG		= BIT(5),
	RATE_INFO_FLAGS_EXTENDED_SC_DMG	= BIT(6),
	RATE_INFO_FLAGS_EHT_MCS		= BIT(7),
};

enum rate_info_bw {
	RATE_INFO_BW_20 = 0,
	RATE_INFO_BW_5,
	RATE_INFO_BW_10,
	RATE_INFO_BW_40,
	RATE_INFO_BW_80,
	RATE_INFO_BW_160,
	RATE_INFO_BW_HE_RU,
	RATE_INFO_BW_320,
	RATE_INFO_BW_1,
	RATE_INFO_BW_2,
	RATE_INFO_BW_4,
	RATE_INFO_BW_8,
	RATE_INFO_BW_16,
};

struct rate_info {
	u16	flags;
	u16	legacy;
	u8	mcs;
	u8	nss;
	u8	bw;
	u8	he_gi;
	u8	he_dcm;
	u8	he_ru_alloc;
	u8	n_bonded_ch;
	u8	eht_gi;
	u8	eht_ru_alloc;
};

/*
 * The station attributes a driver can mark as reported. Only the prefix of the
 * interface's enumeration is carried; the members beyond it arrive with their
 * consumers, and their values are the interface's own (an attribute's number is a
 * uapi fact, so a partial enumeration must never be renumbered).
 */
enum nl80211_sta_info {
	__NL80211_STA_INFO_INVALID,
	NL80211_STA_INFO_INACTIVE_TIME,
	NL80211_STA_INFO_RX_BYTES,
	NL80211_STA_INFO_TX_BYTES,
	NL80211_STA_INFO_LLID,
	NL80211_STA_INFO_PLID,
	NL80211_STA_INFO_PLINK_STATE,
	NL80211_STA_INFO_SIGNAL,
	NL80211_STA_INFO_TX_BITRATE,
};

struct station_info {
	u64			filled;
	struct rate_info	txrate;
	s8			signal;
};

/* ------------------------------------------------------------------ */
/* the wiphy                                                           */
/* ------------------------------------------------------------------ */

enum wiphy_flags {
	WIPHY_FLAG_AP_UAPSD			= BIT(14),
	WIPHY_FLAG_SUPPORTS_TDLS		= BIT(15),
	WIPHY_FLAG_HAVE_AP_SME			= BIT(17),
	WIPHY_FLAG_REPORTS_OBSS			= BIT(18),
	WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD	= BIT(19),
	WIPHY_FLAG_OFFCHAN_TX			= BIT(20),
	WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL	= BIT(21),
	WIPHY_FLAG_HAS_CHANNEL_SWITCH		= BIT(23),
	WIPHY_FLAG_NOTIFY_REGDOM_BY_DRIVER	= BIT(24),
};

/*
 * The extended features a driver can advertise. Transcribed in full rather than in
 * prefix: unlike the attributes above these are bitmap indices read back as a
 * feature number, so a truncated list would silently renumber every entry that
 * follows it.
 */
enum nl80211_ext_feature_index {
	NL80211_EXT_FEATURE_VHT_IBSS,
	NL80211_EXT_FEATURE_RRM,
	NL80211_EXT_FEATURE_MU_MIMO_AIR_SNIFFER,
	NL80211_EXT_FEATURE_SCAN_START_TIME,
	NL80211_EXT_FEATURE_BSS_PARENT_TSF,
	NL80211_EXT_FEATURE_SET_SCAN_DWELL,
	NL80211_EXT_FEATURE_BEACON_RATE_LEGACY,
	NL80211_EXT_FEATURE_BEACON_RATE_HT,
	NL80211_EXT_FEATURE_BEACON_RATE_VHT,
	NL80211_EXT_FEATURE_FILS_STA,
	NL80211_EXT_FEATURE_MGMT_TX_RANDOM_TA,
	NL80211_EXT_FEATURE_MGMT_TX_RANDOM_TA_CONNECTED,
	NL80211_EXT_FEATURE_SCHED_SCAN_RELATIVE_RSSI,
	NL80211_EXT_FEATURE_CQM_RSSI_LIST,
	NL80211_EXT_FEATURE_FILS_SK_OFFLOAD,
	NL80211_EXT_FEATURE_4WAY_HANDSHAKE_STA_PSK,
	NL80211_EXT_FEATURE_4WAY_HANDSHAKE_STA_1X,
	NL80211_EXT_FEATURE_FILS_MAX_CHANNEL_TIME,
	NL80211_EXT_FEATURE_ACCEPT_BCAST_PROBE_RESP,
	NL80211_EXT_FEATURE_OCE_PROBE_REQ_HIGH_TX_RATE,
	NL80211_EXT_FEATURE_OCE_PROBE_REQ_DEFERRAL_SUPPRESSION,
	NL80211_EXT_FEATURE_MFP_OPTIONAL,
	NL80211_EXT_FEATURE_LOW_SPAN_SCAN,
	NL80211_EXT_FEATURE_LOW_POWER_SCAN,
	NL80211_EXT_FEATURE_HIGH_ACCURACY_SCAN,
	NL80211_EXT_FEATURE_DFS_OFFLOAD,
	NL80211_EXT_FEATURE_CONTROL_PORT_OVER_NL80211,
	NL80211_EXT_FEATURE_ACK_SIGNAL_SUPPORT,
	NL80211_EXT_FEATURE_DATA_ACK_SIGNAL_SUPPORT = NL80211_EXT_FEATURE_ACK_SIGNAL_SUPPORT,
	NL80211_EXT_FEATURE_TXQS,
	NL80211_EXT_FEATURE_SCAN_RANDOM_SN,
	NL80211_EXT_FEATURE_SCAN_MIN_PREQ_CONTENT,
	NL80211_EXT_FEATURE_CAN_REPLACE_PTK0,
	NL80211_EXT_FEATURE_ENABLE_FTM_RESPONDER,
	NL80211_EXT_FEATURE_AIRTIME_FAIRNESS,
	NL80211_EXT_FEATURE_AP_PMKSA_CACHING,
	NL80211_EXT_FEATURE_SCHED_SCAN_BAND_SPECIFIC_RSSI_THOLD,
	NL80211_EXT_FEATURE_EXT_KEY_ID,
	NL80211_EXT_FEATURE_STA_TX_PWR,
	NL80211_EXT_FEATURE_SAE_OFFLOAD,
	NL80211_EXT_FEATURE_VLAN_OFFLOAD,
	NL80211_EXT_FEATURE_AQL,
	NL80211_EXT_FEATURE_BEACON_PROTECTION,
	NL80211_EXT_FEATURE_CONTROL_PORT_NO_PREAUTH,
	NL80211_EXT_FEATURE_PROTECTED_TWT,
	NL80211_EXT_FEATURE_DEL_IBSS_STA,
	NL80211_EXT_FEATURE_MULTICAST_REGISTRATIONS,
	NL80211_EXT_FEATURE_BEACON_PROTECTION_CLIENT,
	NL80211_EXT_FEATURE_SCAN_FREQ_KHZ,
	NL80211_EXT_FEATURE_CONTROL_PORT_OVER_NL80211_TX_STATUS,
	NL80211_EXT_FEATURE_OPERATING_CHANNEL_VALIDATION,
	NL80211_EXT_FEATURE_4WAY_HANDSHAKE_AP_PSK,
	NL80211_EXT_FEATURE_SAE_OFFLOAD_AP,
	NL80211_EXT_FEATURE_FILS_DISCOVERY,
	NL80211_EXT_FEATURE_UNSOL_BCAST_PROBE_RESP,
	NL80211_EXT_FEATURE_BEACON_RATE_HE,
	NL80211_EXT_FEATURE_SECURE_LTF,
	NL80211_EXT_FEATURE_SECURE_RTT,
	NL80211_EXT_FEATURE_PROT_RANGE_NEGO_AND_MEASURE,
	NL80211_EXT_FEATURE_BSS_COLOR,
	NL80211_EXT_FEATURE_FILS_CRYPTO_OFFLOAD,
	NL80211_EXT_FEATURE_RADAR_BACKGROUND,
	NL80211_EXT_FEATURE_POWERED_ADDR_CHANGE,
	NL80211_EXT_FEATURE_PUNCT,
	NL80211_EXT_FEATURE_SECURE_NAN,
	NL80211_EXT_FEATURE_AUTH_AND_DEAUTH_RANDOM_TA,
	NL80211_EXT_FEATURE_OWE_OFFLOAD,
	NL80211_EXT_FEATURE_OWE_OFFLOAD_AP,
	NL80211_EXT_FEATURE_DFS_CONCURRENT,
	NL80211_EXT_FEATURE_SPP_AMSDU_SUPPORT,
	NL80211_EXT_FEATURE_BEACON_RATE_EHT,
	NL80211_EXT_FEATURE_EPPKE,
	NL80211_EXT_FEATURE_ASSOC_FRAME_ENCRYPTION,
	NL80211_EXT_FEATURE_IEEE8021X_AUTH,
	NL80211_EXT_FEATURE_ROC_ADDR_FILTER,
	NL80211_EXT_FEATURE_SET_KEY_LTF_SEED,
	NL80211_EXT_FEATURE_PROBE_AP,

	NUM_NL80211_EXT_FEATURES,
	MAX_NL80211_EXT_FEATURES = NUM_NL80211_EXT_FEATURES - 1,
};

struct wiphy {
	/* Bookkeeping the interface keeps per physical device. */
	struct device			*dev;
	u8				perm_addr[ETH_ALEN];
	u8				addr_mask[ETH_ALEN];

	/* Capabilities a driver sets before registering. */
	struct ieee80211_supported_band	*bands[NUM_NL80211_BANDS];
	const struct ieee80211_iface_combination *iface_combinations;
	int				n_iface_combinations;
	u16				interface_modes;
	u16				max_scan_ssids;
	u16				max_scan_ie_len;
	u16				max_ap_assoc_sta;
	u32				rts_threshold;
	u32				flags;
	u32				available_antennas_tx;
	u32				available_antennas_rx;
	u32				ext_features[DIV_ROUND_UP(NUM_NL80211_EXT_FEATURES, 32)];

	/* The diagnostics directory a driver may hang files under. */
	struct dentry			*debugfsdir;
};

/*
 * The device behind a wiphy is a stored pointer the caller both reads and assigns
 * (drivers set it from the interface's device), so both spellings are lvalues, as
 * the interface has them.
 */
#define	wiphy_dev(wiphy)		((wiphy)->dev)
#define	set_wiphy_dev(wiphy, dev)	(((wiphy)->dev) = (dev))

static inline void wiphy_ext_feature_set(struct wiphy *wiphy,
					 enum nl80211_ext_feature_index ftidx)
{
	u32 index = (u32)ftidx / 32;

	if (index < ARRAY_SIZE(wiphy->ext_features))
		wiphy->ext_features[index] |= (u32)BIT(ftidx % 32);
}

/*
 * Convert a reported rate into a bitrate. The driver uses this to fill the
 * bitrate field of a rate report it hands upward; the mapping is the interface's
 * (legacy table entries carry the rate directly, MCS entries are computed from
 * the modulation and the bandwidth).
 */
u32 cfg80211_calculate_bitrate(struct rate_info *rate);

#endif /* _LINUXKPI_NET_CFG80211_H_ */