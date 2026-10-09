/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    net/cfg80211.h
 * @brief   The configuration surface a Linux wireless driver talks to: the wireless
 *          device and wiphy objects, the driver operation table, bands/channels/rates,
 *          interface combinations, the scan/connection/AP/key request records and the
 *          station rate reporting types.
 *
 * @design declaration-only compatibility face. Definitions are the interface's
 *         (include/net/cfg80211.h plus the uapi enumerations it includes), transcribed
 *         with their exact values because several of them are written into hardware
 *         descriptors or compared against hardware register fields; the bodies are
 *         first-party and live in the mac80211 face of this project. Subset by
 *         consumer: the mesh, NAN, pmsr and scheduled-scan bodies of the operation
 *         table arrive when a consumer needs them, and each is stated with the
 *         interface's member name and its interface type in the interface's order.
 *         Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_NET_CFG80211_H_
#define _LINUXKPI_NET_CFG80211_H_

#include "../../linuxkpi_core.h"
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/skbuff.h>

struct cfg80211_ops;
struct regulatory_request;

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
	NL80211_IFTYPE_MAX = NUM_NL80211_IFTYPES - 1,
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
	NL80211_CHAN_WIDTH_320,
};

/* The retired channel-type spelling: a uapi value a driver still converts to and
 * from, kept whole because the enumeration defines the conversion's outputs. */
enum nl80211_channel_type {
	NL80211_CHAN_NO_HT,
	NL80211_CHAN_HT20,
	NL80211_CHAN_HT40MINUS,
	NL80211_CHAN_HT40PLUS
};

/* ------------------------------------------------------------------ */
/* uapi enumerations the driver compares against                        */
/* ------------------------------------------------------------------ */

enum nl80211_auth_type {
	NL80211_AUTHTYPE_OPEN_SYSTEM,
	NL80211_AUTHTYPE_SHARED_KEY,
	NL80211_AUTHTYPE_FT,
	NL80211_AUTHTYPE_NETWORK_EAP,
	NL80211_AUTHTYPE_SAE,
	NL80211_AUTHTYPE_FILS_SK,
	NL80211_AUTHTYPE_FILS_SK_PFS,
	NL80211_AUTHTYPE_FILS_PK,
	NL80211_AUTHTYPE_EPPKE,
	NL80211_AUTHTYPE_IEEE8021X,

	/* keep last */
	__NL80211_AUTHTYPE_NUM,
	NL80211_AUTHTYPE_MAX = __NL80211_AUTHTYPE_NUM - 1,
	NL80211_AUTHTYPE_AUTOMATIC
};

enum nl80211_key_type {
	NL80211_KEYTYPE_GROUP,
	NL80211_KEYTYPE_PAIRWISE,
	NL80211_KEYTYPE_PEERKEY,

	NUM_NL80211_KEYTYPES
};

enum nl80211_wpa_versions {
	NL80211_WPA_VERSION_1 = 1 << 0,
	NL80211_WPA_VERSION_2 = 1 << 1,
	NL80211_WPA_VERSION_3 = 1 << 2,
};

enum nl80211_key_mode {
	NL80211_KEY_RX_TX,
	NL80211_KEY_NO_TX,
	NL80211_KEY_SET_TX
};

enum nl80211_sae_pwe_mechanism {
	NL80211_SAE_PWE_UNSPECIFIED,
	NL80211_SAE_PWE_HUNT_AND_PECK,
	NL80211_SAE_PWE_HASH_TO_ELEMENT,
	NL80211_SAE_PWE_BOTH,
};

enum nl80211_external_auth_action {
	NL80211_EXTERNAL_AUTH_START,
	NL80211_EXTERNAL_AUTH_ABORT,
};

enum nl80211_acl_policy {
	NL80211_ACL_POLICY_ACCEPT_UNLESS_LISTED,
	NL80211_ACL_POLICY_DENY_UNLESS_LISTED,
};

enum nl80211_dfs_state {
	NL80211_DFS_USABLE,
	NL80211_DFS_UNAVAILABLE,
	NL80211_DFS_AVAILABLE,
};

enum nl80211_radar_event {
	NL80211_RADAR_DETECTED,
	NL80211_RADAR_CAC_FINISHED,
	NL80211_RADAR_CAC_ABORTED,
	NL80211_RADAR_NOP_FINISHED,
	NL80211_RADAR_PRE_CAC_EXPIRED,
	NL80211_RADAR_CAC_STARTED,
};

enum nl80211_reg_initiator {
	NL80211_REGDOM_SET_BY_CORE,
	NL80211_REGDOM_SET_BY_USER,
	NL80211_REGDOM_SET_BY_DRIVER,
	NL80211_REGDOM_SET_BY_COUNTRY_IE,
};

enum nl80211_user_reg_hint_type {
	NL80211_USER_REG_HINT_USER	= 0,
	NL80211_USER_REG_HINT_CELL_BASE	= 1,
	NL80211_USER_REG_HINT_INDOOR	= 2,
};

enum nl80211_dfs_regions {
	NL80211_DFS_UNSET	= 0,
	NL80211_DFS_FCC		= 1,
	NL80211_DFS_ETSI	= 2,
	NL80211_DFS_JP		= 3,
};

enum nl80211_tx_power_setting {
	NL80211_TX_POWER_AUTOMATIC,
	NL80211_TX_POWER_LIMITED,
	NL80211_TX_POWER_FIXED,
};

enum nl80211_hidden_ssid {
	NL80211_HIDDEN_SSID_NOT_IN_USE,
	NL80211_HIDDEN_SSID_ZERO_LEN,
	NL80211_HIDDEN_SSID_ZERO_CONTENTS
};

enum nl80211_mfp {
	NL80211_MFP_NO,
	NL80211_MFP_REQUIRED,
	NL80211_MFP_OPTIONAL,
};

enum nl80211_ac {
	NL80211_AC_VO,
	NL80211_AC_VI,
	NL80211_AC_BE,
	NL80211_AC_BK,
	NL80211_NUM_ACS
};

/* The TX queue's access class is spelled with the AC values, as the interface
 * spells it; the enumeration itself carries no separate members. */
#define	NL80211_TXQ_Q_VO	NL80211_AC_VO
#define	NL80211_TXQ_Q_VI	NL80211_AC_VI
#define	NL80211_TXQ_Q_BE	NL80211_AC_BE
#define	NL80211_TXQ_Q_BK	NL80211_AC_BK

enum nl80211_txrate_gi {
	NL80211_TXRATE_DEFAULT_GI,
	NL80211_TXRATE_FORCE_SGI,
	NL80211_TXRATE_FORCE_LGI,
};

enum nl80211_he_gi {
	NL80211_RATE_INFO_HE_GI_0_8,
	NL80211_RATE_INFO_HE_GI_1_6,
	NL80211_RATE_INFO_HE_GI_3_2,
};

enum nl80211_he_ltf {
	NL80211_RATE_INFO_HE_1XLTF,
	NL80211_RATE_INFO_HE_2XLTF,
	NL80211_RATE_INFO_HE_4XLTF,
};

enum nl80211_eht_gi {
	NL80211_RATE_INFO_EHT_GI_0_8,
	NL80211_RATE_INFO_EHT_GI_1_6,
	NL80211_RATE_INFO_EHT_GI_3_2,
};

enum nl80211_eht_ltf {
	NL80211_RATE_INFO_EHT_1XLTF,
	NL80211_RATE_INFO_EHT_2XLTF,
	NL80211_RATE_INFO_EHT_4XLTF,
	NL80211_RATE_INFO_EHT_6XLTF,
	NL80211_RATE_INFO_EHT_8XLTF,
};

enum nl80211_tdls_operation {
	NL80211_TDLS_DISCOVERY_REQ,
	NL80211_TDLS_SETUP,
	NL80211_TDLS_TEARDOWN,
	NL80211_TDLS_ENABLE_LINK,
	NL80211_TDLS_DISABLE_LINK,
};

enum nl80211_mesh_power_mode {
	NL80211_MESH_POWER_UNKNOWN,
	NL80211_MESH_POWER_ACTIVE,
	NL80211_MESH_POWER_LIGHT_SLEEP,
	NL80211_MESH_POWER_DEEP_SLEEP,

	__NL80211_MESH_POWER_AFTER_LAST,
	NL80211_MESH_POWER_MAX = __NL80211_MESH_POWER_AFTER_LAST - 1
};

enum nl80211_plink_state {
	NL80211_PLINK_LISTEN,
	NL80211_PLINK_OPN_SNT,
	NL80211_PLINK_OPN_RCVD,
	NL80211_PLINK_CNF_RCVD,
	NL80211_PLINK_ESTAB,
	NL80211_PLINK_HOLDING,
	NL80211_PLINK_BLOCKED,

	/* keep last */
	NUM_NL80211_PLINK_STATES,
	MAX_NL80211_PLINK_STATES = NUM_NL80211_PLINK_STATES - 1
};

enum cfg80211_station_type {
	CFG80211_STA_AP_CLIENT,
	CFG80211_STA_AP_CLIENT_UNASSOC,
	CFG80211_STA_AP_MLME_CLIENT,
	CFG80211_STA_AP_STA,
	CFG80211_STA_IBSS,
	CFG80211_STA_TDLS_PEER_SETUP,
	CFG80211_STA_TDLS_PEER_ACTIVE,
	CFG80211_STA_MESH_PEER_KERNEL,
	CFG80211_STA_MESH_PEER_USER,
	CFG80211_STA_NAN_MGMT,
	CFG80211_STA_NAN_DATA,
};

enum nl80211_sta_flags {
	__NL80211_STA_FLAG_INVALID,
	NL80211_STA_FLAG_AUTHORIZED,
	NL80211_STA_FLAG_SHORT_PREAMBLE,
	NL80211_STA_FLAG_WME,
	NL80211_STA_FLAG_MFP,
	NL80211_STA_FLAG_AUTHENTICATED,
	NL80211_STA_FLAG_TDLS_PEER,
	NL80211_STA_FLAG_ASSOCIATED,
	NL80211_STA_FLAG_SPP_AMSDU,

	/* keep last */
	__NL80211_STA_FLAG_AFTER_LAST,
	NL80211_STA_FLAG_MAX = __NL80211_STA_FLAG_AFTER_LAST - 1
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
#define	IEEE80211_CHAN_NO_HT40		(IEEE80211_CHAN_NO_HT40PLUS | \
					 IEEE80211_CHAN_NO_HT40MINUS)
#define	IEEE80211_CHAN_NO_80MHZ		0x0080
#define	IEEE80211_CHAN_NO_160MHZ	0x0100
#define	IEEE80211_CHAN_NO_20MHZ		0x1000
#define	IEEE80211_CHAN_IR_CONCURRENT	0x2000

#define	IEEE80211_DFS_MIN_NOP_TIME_MS	(30 * 60 * 1000)

enum ieee80211_bss_type {
	IEEE80211_BSS_TYPE_ESS,
	IEEE80211_BSS_TYPE_PBSS,
	IEEE80211_BSS_TYPE_IBSS,
	IEEE80211_BSS_TYPE_MBSS,
	IEEE80211_BSS_TYPE_ANY
};

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
	u32			orig_flags;
	int			orig_mag, orig_mpwr;
	enum nl80211_dfs_state	dfs_state;
	unsigned long		dfs_state_entered;
	unsigned int		dfs_cac_ms;
	u64			cac_start_time;
	s8			psd;
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

/*
 * The HE capability records are the interface's element payloads, kept in the
 * interface's split headers (include/linux/ieee80211-he.h) and packed exactly as the
 * element's fixed fields are, because the driver copies received elements into them
 * byte for byte.
 */
struct ieee80211_he_cap_elem {
	u8	mac_cap_info[6];
	u8	phy_cap_info[11];
} __packed;

struct ieee80211_he_mcs_nss_supp {
	__le16	rx_mcs_80;
	__le16	tx_mcs_80;
	__le16	rx_mcs_160;
	__le16	tx_mcs_160;
	__le16	rx_mcs_80p80;
	__le16	tx_mcs_80p80;
} __packed;

struct ieee80211_he_6ghz_capa {
	/* uses IEEE80211_HE_6GHZ_CAP_* */
	__le16	capa;
} __packed;

struct ieee80211_eht_cap_elem_fixed {
	u8	mac_cap_info[2];
	u8	phy_cap_info[9];
	struct ieee80211_ppe_thres_info {
		u8	info;
		u8	count;
	} __packed ppe_thres_info;
} __packed;

struct ieee80211_eht_mcs_nss_supp_20mhz_only {
	u8	rx_tx_mcs_0_7_tx_mcs_8_9;
};

struct ieee80211_eht_mcs_nss_supp_bw {
	u8	rx_tx_mcs_0_7_tx_mcs_8_9;
};

struct ieee80211_eht_mcs_nss_supp {
	union {
		struct ieee80211_eht_mcs_nss_supp_20mhz_only only_20mhz;
		struct {
			struct ieee80211_eht_mcs_nss_supp_bw _80;
			struct ieee80211_eht_mcs_nss_supp_bw _160;
			struct ieee80211_eht_mcs_nss_supp_bw _320;
		} __packed bw;
	} __packed;
};

struct ieee80211_sta_he_cap {
	bool					has_he;
	struct ieee80211_he_cap_elem		he_cap_elem;
	struct ieee80211_he_mcs_nss_supp	he_mcs_nss_supp;
	u8					ppe_thres[3];
};

struct ieee80211_sta_eht_cap {
	bool					has_eht;
	struct ieee80211_eht_cap_elem_fixed	eht_cap_elem;
	struct ieee80211_eht_mcs_nss_supp	eht_mcs_nss_supp;
	u8					eht_ppe_thres[3];
};

struct ieee80211_sband_iftype_data {
	u16				types_mask;
	struct ieee80211_sta_he_cap	he_cap;
	struct ieee80211_he_6ghz_capa	he_6ghz_capa;
	struct ieee80211_sta_eht_cap	eht_cap;
	struct {
		const u8	*data;
		unsigned int	len;
	} vendor_elems;
};

struct ieee80211_supported_band {
	struct ieee80211_channel	*channels;
	struct ieee80211_rate		*bitrates;
	enum nl80211_band		band;
	int				n_channels;
	int				n_bitrates;
	struct ieee80211_sta_ht_cap	ht_cap;
	struct ieee80211_sta_vht_cap	vht_cap;
	struct ieee80211_sband_iftype_data *iftype_data;
	u8				n_iftype_data;
};

struct cfg80211_chan_def {
	struct ieee80211_channel	*chan;
	struct ieee80211_channel	*npca_chan;
	enum nl80211_chan_width		width;
	u32				center_freq1;
	u32				center_freq2;
	u16				freq1_offset;
	u16				punctured, npca_punctured;
	bool				s1g_primary_2mhz;
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

/* The bitmap of frame subtypes a driver can report to user space; one entry per
 * management-frame type. */
struct ieee80211_txrx_stypes {
	u16 tx, rx;
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
	s8	mcs;
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
 * The station attributes a driver can mark as reported. Transcribed whole rather than
 * in prefix: unlike the payload records above, these are bitmap indices and a
 * truncated list would silently renumber every entry that follows it.
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
	NL80211_STA_INFO_RX_PACKETS,
	NL80211_STA_INFO_TX_PACKETS,
	NL80211_STA_INFO_TX_RETRIES,
	NL80211_STA_INFO_TX_FAILED,
	NL80211_STA_INFO_SIGNAL_AVG,
	NL80211_STA_INFO_RX_BITRATE,
	NL80211_STA_INFO_BSS_PARAM,
	NL80211_STA_INFO_CONNECTED_TIME,
	NL80211_STA_INFO_STA_FLAGS,
	NL80211_STA_INFO_BEACON_LOSS,
	NL80211_STA_INFO_T_OFFSET,
	NL80211_STA_INFO_LOCAL_PM,
	NL80211_STA_INFO_PEER_PM,
	NL80211_STA_INFO_NONPEER_PM,
	NL80211_STA_INFO_RX_BYTES64,
	NL80211_STA_INFO_TX_BYTES64,
	NL80211_STA_INFO_CHAIN_SIGNAL,
	NL80211_STA_INFO_CHAIN_SIGNAL_AVG,
	NL80211_STA_INFO_EXPECTED_THROUGHPUT,
	NL80211_STA_INFO_RX_DROP_MISC,
	NL80211_STA_INFO_BEACON_RX,
	NL80211_STA_INFO_BEACON_SIGNAL_AVG,
	NL80211_STA_INFO_TID_STATS,
	NL80211_STA_INFO_RX_DURATION,
	NL80211_STA_INFO_PAD,
	NL80211_STA_INFO_ACK_SIGNAL,
	NL80211_STA_INFO_ACK_SIGNAL_AVG,
	NL80211_STA_INFO_RX_MPDUS,
	NL80211_STA_INFO_FCS_ERROR_COUNT,
	NL80211_STA_INFO_CONNECTED_TO_GATE,
	NL80211_STA_INFO_TX_DURATION,
	NL80211_STA_INFO_AIRTIME_WEIGHT,
	NL80211_STA_INFO_AIRTIME_LINK_METRIC,
	NL80211_STA_INFO_ASSOC_AT_BOOTTIME,
	NL80211_STA_INFO_CONNECTED_TO_AS,

	/* keep last */
	__NL80211_STA_INFO_AFTER_LAST,
	NL80211_STA_INFO_MAX = __NL80211_STA_INFO_AFTER_LAST - 1
};

struct sta_bss_parameters {
	u8	flags;
	u8	dtim_period;
	u16	beacon_interval;
};

struct nl80211_sta_flag_update {
	__u32	mask;
	__u32	set;
} __attribute__((packed));

struct cfg80211_tid_stats {
	u32			filled;
	u64			rx_msdu;
	u64			tx_msdu;
	u64			tx_msdu_retries;
	u64			tx_msdu_failed;
	struct {
		u64		packets;
		u64		bytes;
		u64		overflow;
		u64		overlimit;
		u64		dropped;
		u64		ecn_marks;
		u64		ack;
		u64		direct_reuse;
		u64		reuse;
		u64		frag_success;
		u64		credit_drop;
		u64		flow_limit_drop;
		u64		flow_limit_wait;
		u64		flow_limit_credit;
	} txq_stats;
};

/*
 * The station record a driver fills per peer. Only the members a compiled unit
 * writes are carried, with their interface names and types; the interface's record
 * also has mesh, MLO and hardware-timestamp members, which arrive with the caller
 * that names them.
 */
struct station_info {
	u64			filled;
	u32			connected_time;
	u32			inactive_time;
	u64			assoc_at;
	u64			rx_bytes;
	u64			tx_bytes;
	s8			signal;
	s8			signal_avg;

	u8			chains;
	s8			chain_signal[IEEE80211_MAX_CHAINS];
	s8			chain_signal_avg[IEEE80211_MAX_CHAINS];

	struct rate_info	txrate;
	struct rate_info	rxrate;
	u32			rx_packets;
	u32			tx_packets;
	u32			tx_retries;
	u32			tx_failed;
	u32			rx_dropped_misc;
	struct sta_bss_parameters bss_param;
	struct nl80211_sta_flag_update sta_flags;

	int			generation;

	u32			beacon_loss_count;

	const u8		*assoc_req_ies;
	size_t			assoc_req_ies_len;

	s64			t_offset;
	u16			llid;
	u16			plid;
	u8			plink_state;
	u8			connected_to_gate;
	u8			connected_to_as;
	u32			airtime_link_metric;
	enum nl80211_mesh_power_mode local_pm;
	enum nl80211_mesh_power_mode peer_pm;
	enum nl80211_mesh_power_mode nonpeer_pm;

	u32			expected_throughput;

	u16			airtime_weight;

	s8			ack_signal;
	s8			avg_ack_signal;
	struct cfg80211_tid_stats *pertid;

	u64			tx_duration;
	u64			rx_duration;
	u64			rx_beacon;
	u8			rx_beacon_signal_avg;

	u32			rx_mpdu_count;
	u32			fcs_err_count;
};

/* ------------------------------------------------------------------ */
/* the wiphy                                                           */
/* ------------------------------------------------------------------ */

enum wiphy_flags {
	WIPHY_FLAG_SUPPORTS_EXT_KEK_KCK		= BIT(0),
	WIPHY_FLAG_SUPPORTS_MLO			= BIT(1),
	WIPHY_FLAG_SPLIT_SCAN_6GHZ		= BIT(2),
	WIPHY_FLAG_NETNS_OK			= BIT(3),
	WIPHY_FLAG_PS_ON_BY_DEFAULT		= BIT(4),
	WIPHY_FLAG_4ADDR_AP			= BIT(5),
	WIPHY_FLAG_4ADDR_STATION		= BIT(6),
	WIPHY_FLAG_CONTROL_PORT_PROTOCOL	= BIT(7),
	WIPHY_FLAG_IBSS_RSN			= BIT(8),
	/* reuse bit 9 */
	WIPHY_FLAG_MESH_AUTH			= BIT(10),
	WIPHY_FLAG_SUPPORTS_EXT_KCK_32		= BIT(11),
	WIPHY_FLAG_SUPPORTS_NSTR_NONPRIMARY	= BIT(12),
	WIPHY_FLAG_SUPPORTS_FW_ROAM		= BIT(13),
	WIPHY_FLAG_AP_UAPSD			= BIT(14),
	WIPHY_FLAG_SUPPORTS_TDLS		= BIT(15),
	WIPHY_FLAG_TDLS_EXTERNAL_SETUP		= BIT(16),
	WIPHY_FLAG_HAVE_AP_SME			= BIT(17),
	WIPHY_FLAG_REPORTS_OBSS			= BIT(18),
	WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD	= BIT(19),
	WIPHY_FLAG_OFFCHAN_TX			= BIT(20),
	WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL	= BIT(21),
	WIPHY_FLAG_HAS_CHANNEL_SWITCH		= BIT(23),
	WIPHY_FLAG_NOTIFY_REGDOM_BY_DRIVER	= BIT(24),
	WIPHY_FLAG_CHANNEL_CHANGE_ON_BEACON	= BIT(25),
};

enum wiphy_wowlan_support_flags {
	WIPHY_WOWLAN_ANY		= BIT(0),
	WIPHY_WOWLAN_MAGIC_PKT		= BIT(1),
	WIPHY_WOWLAN_DISCONNECT		= BIT(2),
	WIPHY_WOWLAN_SUPPORTS_GTK_REKEY	= BIT(3),
	WIPHY_WOWLAN_GTK_REKEY_FAILURE	= BIT(4),
	WIPHY_WOWLAN_EAP_IDENTITY_REQ	= BIT(5),
	WIPHY_WOWLAN_4WAY_HANDSHAKE	= BIT(6),
	WIPHY_WOWLAN_RFKILL_RELEASE	= BIT(7),
	WIPHY_WOWLAN_NET_DETECT		= BIT(8),
};

struct wiphy_wowlan_support {
	u32						flags;
	int						n_patterns;
	int						pattern_max_len;
	int						pattern_min_len;
	int						max_pkt_offset;
	int						max_nd_match_sets;
	const struct wiphy_wowlan_tcp_support		*tcp;
};

/* Parameters a driver sets through set_wiphy_params(). */
#define	WIPHY_PARAM_RTS_THRESHOLD		BIT(0)
#define	WIPHY_PARAM_FRAG_THRESHOLD		BIT(1)

enum cfg80211_signal_type {
	CFG80211_SIGNAL_TYPE_NONE,
	CFG80211_SIGNAL_TYPE_MBM,
	CFG80211_SIGNAL_TYPE_UNSPEC,
};

struct wiphy_vendor_command;
struct nl80211_vendor_cmd_info;
struct cfg80211_wowlan;
struct ieee80211_regdomain;

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

/*
 * The wireless hardware instance. The members carried are the ones a compiled unit
 * names, in the interface's order with their interface types. The interface's record
 * is far larger and its regions are private to cfg80211 (the locking state, the
 * regulatory request bookkeeping, the NAN and MLO state); the leading capability
 * block is what a driver sets before registration and is what is transcribed here.
 * The `priv` area the driver's own data is allocated from is the record's tail
 * member, sized by wiphy_new().
 */
struct wiphy {
	u8			perm_addr[ETH_ALEN];
	u8			addr_mask[ETH_ALEN];

	const struct ieee80211_txrx_stypes *mgmt_stypes;

	const struct ieee80211_iface_combination *iface_combinations;
	int			n_iface_combinations;
	u16			software_iftypes;
	u32			nl80211_ifyptes_unused;
	u16			interface_modes;
	u16			max_acl_mac_addrs;

	u32			flags, regulatory_flags, features;
	u8			ext_features[DIV_ROUND_UP(NUM_NL80211_EXT_FEATURES, 8)];

	enum cfg80211_signal_type	signal_type;

	u8			max_scan_ssids;
	u8			max_sched_scan_reqs;
	u8			max_sched_scan_ssids;
	u8			max_match_sets;
	u16			max_scan_ie_len;
	u16			max_sched_scan_ie_len;
	u32			max_sched_scan_plans;
	u32			max_sched_scan_plan_interval;
	u32			max_sched_scan_plan_iterations;

	int			n_cipher_suites;
	const u32		*cipher_suites;

	u32			frag_threshold;
	u32			rts_threshold;

	const struct wiphy_wowlan_support	*wowlan;
	struct cfg80211_wowlan			*wowlan_config;

	u16			max_remain_on_channel_duration;

	u8			max_num_pmkids;

	u32			available_antennas_tx;
	u32			available_antennas_rx;

	struct ieee80211_supported_band	*bands[NUM_NL80211_BANDS];

	void (*reg_notifier)(struct wiphy *wiphy,
			     struct regulatory_request *request);

	const struct ieee80211_regdomain *regd;

	const struct wiphy_vendor_command *vendor_commands;
	const struct nl80211_vendor_cmd_info *vendor_events;
	int			n_vendor_commands, n_vendor_events;

	u16			max_ap_assoc_sta;

	u8			max_num_csa_counters;

	/* the diagnostics directory a driver may hang files under */
	struct dentry		*debugfsdir;

	/* the device this wiphy reports through, as the interface embeds it */
	struct device		dev;

	/* the driver private area, sized by wiphy_new() and reached through
	 * wiphy_priv(); private to the interface otherwise */
	int			priv[];
};

/* ------------------------------------------------------------------ */
/* the wireless device                                                 */
/* ------------------------------------------------------------------ */

/*
 * The bound the per-interface record's link array is declared with. The interface
 * splits it out in include/linux/ieee80211-eht.h; the face keeps it here, next to
 * its only consumer.
 */
#define	IEEE80211_MLD_MAX_NUM_LINKS	15

/*
 * The per-interface record. Its size is taken (a monitor and a P2P-device wdev are
 * allocated with it) and its leading members are read and written by name, so the
 * prefix is transcribed in the interface's order. The interface's record continues
 * with cfg80211-private bookkeeping - the mgmt-registration list, the connection
 * state machine's cache, the CQM and pmsr state, the per-band union and the MLO link
 * array - none of which a compiled unit names; those members arrive with the body
 * that owns them. `u` keeps the interface's member name and type at the cut, so the
 * size taken here matches the interface's leading region.
 */
struct wireless_dev {
	struct wiphy		*wiphy;
	enum nl80211_iftype	iftype;

	/* the remainder of this struct is private to cfg80211 */
	struct list_head	list;
	struct net_device	*netdev;

	u32			identifier;

	struct list_head	mgmt_registrations;
	u8			mgmt_registrations_need_update:1;

	bool			use_4addr, is_running, registered, registering;

	u8			address[ETH_ALEN] __aligned(sizeof(u16));

	u8			connected:1;

	bool			ps;
	int			ps_timeout;

	/*
	 * The per-interface-type state union. Only the AP and the client arms are
	 * carried - they are the two a compiled unit names - with the interface's
	 * member names and types; the mesh, IBSS, OCB and NAN arms and the MLO link
	 * array that the interface declares after it arrive with their consumers.
	 */
	union {
		struct {
			u8				connected_addr[ETH_ALEN] __aligned(2);
			u8				ssid[IEEE80211_MAX_SSID_LEN];
			u8				ssid_len;
		} client;
		struct {
			struct cfg80211_chan_def	preset_chandef;
			struct cfg80211_chan_def	chandef;
			u8				ssid[IEEE80211_MAX_SSID_LEN];
			u8				ssid_len;
		} ap;
	} u;

	struct {
		u8				addr[ETH_ALEN] __aligned(2);
		union {
			struct {
				unsigned int		beacon_interval;
				struct cfg80211_chan_def chandef;
			} ap;
			struct {
				struct cfg80211_internal_bss *current_bss;
			} client;
		};

		bool				cac_started;
		unsigned long			cac_start_time;
		unsigned int			cac_time_ms;
	} links[IEEE80211_MLD_MAX_NUM_LINKS];
	u16			valid_links;

	u32			radio_mask;

	/*
	 * The compatibility spelling of the single-link client's current BSS: the
	 * interface reaches the first link's client record through it. It is not a
	 * stored member in the interface (which declares it only where the MLD
	 * kernel patch is absent, as this face does), so it cannot be transcribed
	 * as a field - a macro over the same expression the interface's own alias
	 * uses keeps one mechanism.
	 */
};

#define	cfg80211_priv_size(wiphy)	0

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

struct wiphy *wiphy_new_nm(const struct cfg80211_ops *ops, int sizeof_priv,
			   const char *requested_name);

static inline struct wiphy *wiphy_new(const struct cfg80211_ops *ops,
				      int sizeof_priv)
{
	return wiphy_new_nm(ops, sizeof_priv, NULL);
}

int wiphy_register(struct wiphy *wiphy);
void wiphy_unregister(struct wiphy *wiphy);
void wiphy_free(struct wiphy *wiphy);

static inline void *wiphy_priv(struct wiphy *wiphy)
{
	return &wiphy->priv;
}

static inline const char *wiphy_name(const struct wiphy *wiphy)
{
	return dev_name(&wiphy->dev);
}

/* The device behind a wiphy is a stored pointer the caller both reads and
 * assigns (drivers set it from the interface's device). Both spellings are
 * inline helpers rather than function-like macros: a macro whose parameter is
 * named `dev` substitutes into its own `->dev` member access and rewrites the
 * lvalue into `-><argument>` (found when this layer's own substitute called it
 * with an argument that was not literally spelled `dev`). The interface spells
 * these as inline functions, which is the fix here too.
 */
static __always_inline __unused struct device *
wiphy_dev(struct wiphy *wiphy)
{

	return wiphy->dev.parent;
}

static __always_inline __unused void
set_wiphy_dev(struct wiphy *wiphy, struct device *dev)
{

	wiphy->dev.parent = dev;
}

static inline void wiphy_ext_feature_set(struct wiphy *wiphy,
					 enum nl80211_ext_feature_index ftidx)
{
	u8 *ft_byte;

	ft_byte = &wiphy->ext_features[ftidx / 8];
	*ft_byte |= BIT(ftidx % 8);
}

static inline bool
wiphy_ext_feature_isset(struct wiphy *wiphy,
			enum nl80211_ext_feature_index ftidx)
{
	u8 ft_byte;

	ft_byte = wiphy->ext_features[ftidx / 8];
	return (ft_byte & BIT(ftidx % 8)) != 0;
}

/*
 * Convert a reported rate into a bitrate. The driver uses this to fill the
 * bitrate field of a rate report it hands upward; the mapping is the interface's
 * (legacy table entries carry the rate directly, MCS entries are computed from
 * the modulation and the bandwidth).
 */
u32 cfg80211_calculate_bitrate(struct rate_info *rate);

#endif /* _LINUXKPI_NET_CFG80211_H_ */
