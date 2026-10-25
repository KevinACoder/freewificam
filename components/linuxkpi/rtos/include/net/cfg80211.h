/* SPDX-License-Identifier: BSD-3-Clause */
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
/* [LNX] include/net/cfg80211.h: the interface includes the ethtool surface its wiphy
 * record sizes a member with */
#include <linux/ethtool.h>
#include <linux/rfkill.h>
/* [LNX] include/net/cfg80211.h:26: the interface includes the regulatory flags itself */
#include <net/regulatory.h>

struct cfg80211_gtk_rekey_data;
struct ieee80211_scan_request;
struct ieee80211_low_level_stats;

struct cfg80211_ops;
struct regulatory_request;

/* Tags the compiled set reaches only through pointers; their bodies arrive with a
 * caller that reads a member. */
struct vif_params;
struct station_parameters;
struct cfg80211_update_ft_ies_params;
struct netlink_ext_ack;
struct ieee80211_ht_operation;
struct ieee80211_vht_operation;
struct ieee80211_eht_operation;
struct ieee80211_uhr_operation;
struct cfg80211_mbssid_elems;
struct cfg80211_rnr_elems;
struct cfg80211_roam_info;
struct cfg80211_fils_discovery;
struct cfg80211_unsol_bcast_probe_resp;
struct cfg80211_s1g_short_beacon;
struct ieee80211_he_obss_pd;
struct cfg80211_mbssid_config;
struct cfg80211_acl_data;
struct mac_address;

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

/* The DFS region enumeration lives in the regulatory face (net/regulatory.h), which is
 * the interface's own home for it and the file the domain record that names it is
 * declared in; this header includes that one. */

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
/* [LNX] include/net/cfg80211.h:141 */
#define	IEEE80211_CHAN_INDOOR_ONLY	0x0200
#define	IEEE80211_CHAN_NO_20MHZ		0x1000
#define	IEEE80211_CHAN_IR_CONCURRENT	0x2000
/* [LNX] include/net/cfg80211.h:151-154 */
#define	IEEE80211_CHAN_NO_6GHZ_VLP_CLIENT	0x00400000
#define	IEEE80211_CHAN_NO_6GHZ_AFC_CLIENT	0x00800000
#define	IEEE80211_CHAN_CAN_MONITOR		0x01000000
#define	IEEE80211_CHAN_ALLOW_6GHZ_VLP_AP	0x02000000

#define	IEEE80211_DFS_MIN_NOP_TIME_MS	(30 * 60 * 1000)

enum ieee80211_bss_type {
	IEEE80211_BSS_TYPE_ESS,
	IEEE80211_BSS_TYPE_PBSS,
	IEEE80211_BSS_TYPE_IBSS,
	IEEE80211_BSS_TYPE_MBSS,
	IEEE80211_BSS_TYPE_ANY
};

/* [LNX] include/net/cfg80211.h:3204-3210 (record carried whole) */
struct cfg80211_bss_ies {
	u64 tsf;
	struct rcu_head rcu_head;
	int len;
	bool from_beacon;
	u8 data[];
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

/* [LNX] include/linux/ieee80211-eht.h:72-82 (record carried whole) */
struct ieee80211_eht_mcs_nss_supp_20mhz_only {
	union {
		struct {
			u8 rx_tx_mcs7_max_nss;
			u8 rx_tx_mcs9_max_nss;
			u8 rx_tx_mcs11_max_nss;
			u8 rx_tx_mcs13_max_nss;
		};
		u8 rx_tx_max_nss[4];
	};
};

/* [LNX] include/linux/ieee80211-eht.h:103-112 (record carried whole) */
struct ieee80211_eht_mcs_nss_supp_bw {
	union {
		struct {
			u8 rx_tx_mcs9_max_nss;
			u8 rx_tx_mcs11_max_nss;
			u8 rx_tx_mcs13_max_nss;
		};
		u8 rx_tx_max_nss[3];
	};
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

/* [LNX] include/net/cfg80211.h:374,417: the two PPE-threshold array bounds. The
 * records below were sized by eye before a compiled consumer named them; a bound
 * of 3 against the interface's 25/32 both truncates what a caller may store and
 * makes the driver's own four-byte default read as an initialiser overflow. */
#define IEEE80211_HE_PPE_THRES_MAX_LEN		25
#define IEEE80211_EHT_PPE_THRES_MAX_LEN		32

struct ieee80211_sta_he_cap {
	bool					has_he;
	struct ieee80211_he_cap_elem		he_cap_elem;
	struct ieee80211_he_mcs_nss_supp	he_mcs_nss_supp;
	u8					ppe_thres[IEEE80211_HE_PPE_THRES_MAX_LEN];
};

struct ieee80211_sta_eht_cap {
	bool					has_eht;
	struct ieee80211_eht_cap_elem_fixed	eht_cap_elem;
	struct ieee80211_eht_mcs_nss_supp	eht_mcs_nss_supp;
	u8					eht_ppe_thres[IEEE80211_EHT_PPE_THRES_MAX_LEN];
};

/* [LNX] include/linux/ieee80211-uhr.h:454,488: the UHR capability records a band's
 * per-interface-type data carries. */
struct ieee80211_uhr_cap_mac {
	u8	mac_cap[6];
} __packed;

struct ieee80211_uhr_cap_phy {
	__le32	cap;
	u8	reserved;
} __packed;

struct ieee80211_sta_uhr_cap {
	bool				has_uhr;
	struct ieee80211_uhr_cap_mac	mac;
	struct ieee80211_uhr_cap_phy	phy;
};

struct ieee80211_sband_iftype_data {
	u16				types_mask;
	struct ieee80211_sta_he_cap	he_cap;
	struct ieee80211_he_6ghz_capa	he_6ghz_capa;
	struct ieee80211_sta_eht_cap	eht_cap;
	struct ieee80211_sta_uhr_cap	uhr_cap;
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
	RATE_INFO_BW_EHT_RU = 8
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
/* [LNX] include/net/cfg80211.h:2205 */
#define	IEEE80211_MAX_CHAINS	4

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
/* [LNX] include/net/cfg80211.h:6047 (record carried whole) */
struct wiphy_iftype_ext_capab {
	enum nl80211_iftype iftype;
	const u8 *extended_capabilities;
	const u8 *extended_capabilities_mask;
	u8 extended_capabilities_len;
	u16 eml_capabilities;
	u16 mld_capa_and_ops;
	u16 ext_mld_capa_and_ops;
};

/* [LNX] include/net/cfg80211.h:2500 (record carried whole) */
/* [LNX] include/net/cfg80211.h:2486 (record carried whole) */
struct cfg80211_sar_freq_ranges {
	u32 start_freq;
	u32 end_freq;
};

/* [LNX] include/uapi/linux/nl80211.h:8780 (the enumerators the compiled set names, at their computed positions) */
enum nl80211_sar_type {
	NL80211_SAR_TYPE_POWER = 0
};

struct cfg80211_sar_capa {
	enum nl80211_sar_type type;
	u32 num_freq_ranges;
	const struct cfg80211_sar_freq_ranges *freq_ranges;
};

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

	char			fw_version[ETHTOOL_FWVERS_LEN];

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

	/* [LNX] include/net/cfg80211.h:6499-6511: the per-TID config support the compiled
	 * set reads (attribute bitmaps per peer and per vif) */
	struct {
		u64 peer, vif;
		u8 max_retry;
	} tid_config_support;

	struct rfkill *rfkill;
	const struct wiphy_iftype_ext_capab *iftype_ext_capab;
	unsigned int		num_iftype_ext_capab;
	const struct cfg80211_sar_capa *sar_capa;

	/* [LNX] include/net/cfg80211.h: the remaining members the compiled set
	 * names, at their interface types. */
	const struct cfg80211_pmsr_capabilities *pmsr_capa;
	struct mac_address	*addresses;
	u16			n_addresses;
	u32			hw_version;
	u16			hw_timestamp_max_peers;

	struct mutex		mtx;

	/* the driver private area, sized by wiphy_new() and reached through
	 * wiphy_priv(); private to the interface otherwise */
	int			priv[];
};

/* ------------------------------------------------------------------ */
/* the wireless device                                                 */
/* ------------------------------------------------------------------ */

/*
 * The bound the per-interface record's link array is declared with. The interface
 * splits it out in include/linux/ieee80211-eht.h; the face keeps it here, next to its
 * only consumer.
 */
#define	IEEE80211_MLD_MAX_NUM_LINKS	15

/*
 * The bound the per-interface record's link array is declared with. The interface
 * splits it out in include/linux/ieee80211-eht.h; the face keeps it here, next to
 * its only consumer.
 */
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
/* [LNX] include/net/regulatory.h:78-89 (record carried whole; stated next to the
 * enumerations it names) */
/* [LNX] include/net/regulatory.h:34-38 (enumerators at their computed positions);
 * stated next to the request record that names it, for the include-edge reason */
enum environment_cap {
	ENVIRON_ANY = 0,
	ENVIRON_INDOOR = 1,
	ENVIRON_OUTDOOR = 2
};

struct regulatory_request {
	struct rcu_head rcu_head;
	int wiphy_idx;
	enum nl80211_reg_initiator initiator;
	enum nl80211_user_reg_hint_type user_reg_hint_type;
	char alpha2[3];
	enum nl80211_dfs_regions dfs_region;
	bool intersect;
	bool processed;
	enum environment_cap country_ie_env;
	struct list_head list;
};

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

/* [LNX] include/net/cfg80211.h:2897 (record carried whole) */
struct cfg80211_ssid {
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 ssid_len;
};

/* [LNX] include/net/cfg80211.h:892 (record carried whole) */
/* [LNX] include/uapi/linux/nl80211.h:5925,5935,5950 */
#define	NL80211_VHT_NSS_MAX		8
#define	NL80211_HE_NSS_MAX		8
#define	NL80211_EHT_NSS_MAX		16

struct cfg80211_bitrate_mask {
	struct {
		u32 legacy;
		u8 ht_mcs[IEEE80211_HT_MCS_MASK_LEN];
		u16 vht_mcs[NL80211_VHT_NSS_MAX];
		u16 he_mcs[NL80211_HE_NSS_MAX];
		u16 eht_mcs[NL80211_EHT_NSS_MAX];
		enum nl80211_txrate_gi gi;
		enum nl80211_he_gi he_gi;
		enum nl80211_eht_gi eht_gi;
		enum nl80211_he_ltf he_ltf;
		enum nl80211_eht_ltf eht_ltf;
	} control[NUM_NL80211_BANDS];
};

/* [LNX] include/net/cfg80211.h:1321 (record carried whole) */
struct survey_info {
	struct ieee80211_channel *channel;
	u64 time;
	u64 time_busy;
	u64 time_ext_busy;
	u64 time_rx;
	u64 time_tx;
	u64 time_scan;
	u64 time_bss_rx;
	u32 filled;
	s8 noise;
};

/* [LNX] include/net/cfg80211.h:924 (record carried whole) */
/* [LNX] include/uapi/linux/nl80211.h:6083-6086 (enumerators at their computed positions) */
enum nl80211_tx_rate_setting {
	NL80211_TX_RATE_AUTOMATIC = 0,
	NL80211_TX_RATE_LIMITED = 1,
	NL80211_TX_RATE_FIXED = 2
};

/* [LNX] include/net/cfg80211.h:924-935 (record carried whole) */
/* [LNX] include/uapi/linux/nl80211.h:6073 (the enumerators the compiled set names, at their computed positions) */
enum nl80211_tid_config {
	NL80211_TID_CONFIG_ENABLE = 0
};

struct cfg80211_tid_cfg {
	bool config_override;
	u8 tids;
	u64 mask;
	enum nl80211_tid_config noack;
	u8 retry_long, retry_short;
	enum nl80211_tid_config ampdu;
	enum nl80211_tid_config rtscts;
	enum nl80211_tid_config amsdu;
	enum nl80211_tx_rate_setting txrate_type;
	struct cfg80211_bitrate_mask txrate_mask;
};

/* [LNX] include/net/cfg80211.h:3025 (record carried whole) */
struct cfg80211_match_set {
	struct cfg80211_ssid ssid;
	u8 bssid[ETH_ALEN];
	s32 rssi_thold;
};

/* [LNX] include/net/cfg80211.h:3040 (record carried whole) */
struct cfg80211_sched_scan_plan {
	u32 interval;
	u32 iterations;
};

/* [LNX] include/net/cfg80211.h:2463 (record carried whole) */
struct cfg80211_sar_sub_specs {
	s32 power;
	u32 freq_range_index;
};



/* [LNX] include/net/cfg80211.h:2474 (record carried whole) */
struct cfg80211_sar_specs {
	enum nl80211_sar_type type;
	u32 num_sub_specs;
	struct cfg80211_sar_sub_specs sub_specs[] __counted_by(num_sub_specs);
};

/* [LNX] include/net/cfg80211.h:2930 (record carried whole) */
struct cfg80211_scan_6ghz_params {
	u32 short_ssid;
	u32 channel_idx;
	u8 bssid[ETH_ALEN];
	bool unsolicited_probe;
	bool short_ssid_valid;
	bool psc_no_listen;
	s8 psd_20;
};

/* [LNX] include/net/cfg80211.h:3105 (record carried whole) */
/* [LNX] include/net/cfg80211.h:3051 (record carried whole) */
struct cfg80211_bss_select_adjust {
	enum nl80211_band band;
	s8 delta;
};

struct cfg80211_sched_scan_request {
	u64 reqid;
	struct cfg80211_ssid *ssids;
	int n_ssids;
	u32 n_channels;
	const u8 *ie;
	size_t ie_len;
	u32 flags;
	struct cfg80211_match_set *match_sets;
	int n_match_sets;
	s32 min_rssi_thold;
	u32 delay;
	struct cfg80211_sched_scan_plan *scan_plans;
	int n_scan_plans;

	u8 mac_addr[ETH_ALEN] __aligned(2);
	u8 mac_addr_mask[ETH_ALEN] __aligned(2);

	bool relative_rssi_set;
	s8 relative_rssi;
	struct cfg80211_bss_select_adjust rssi_adjust;

	/* internal */
	struct wiphy *wiphy;
	struct net_device *dev;
	unsigned long scan_start;
	bool report_results;
	struct rcu_head rcu_head;
	u32 owner_nlportid;
	bool nl_owner_dead;
	struct list_head list;

	/* keep last */
	struct ieee80211_channel *channels[] __counted_by(n_channels);
};

/* [LNX] include/net/cfg80211.h:2911 (record carried whole) */
struct cfg80211_scan_info {
	u64 scan_start_tsf;
	u8 tsf_bssid[ETH_ALEN] __aligned(2);
	bool aborted;
};

/* [LNX] include/net/cfg80211.h: the fine-timing-measurement responder counters a
 * driver fills and reports. */
struct cfg80211_ftm_responder_stats {
	u32 filled;
	u32 success_num;
	u32 partial_num;
	u32 failed_num;
	u32 asap_num;
	u32 non_asap_num;
	u64 total_duration_ms;
	u32 unknown_triggers_num;
	u32 reschedule_requests_num;
	u32 out_of_window_triggers_num;
};

/* [LNX] include/uapi/linux/nl80211.h: the peer-measurement result and capability
 * enumerations a driver's FTM path names. */
enum nl80211_preamble {
	NL80211_PREAMBLE_LEGACY,
	NL80211_PREAMBLE_HT,
	NL80211_PREAMBLE_VHT,
	NL80211_PREAMBLE_DMG,
	NL80211_PREAMBLE_HE,
};

/* [LNX] include/uapi/linux/nl80211.h:3837-3838 */
#define	NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY	16
#define	NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY	24

enum nl80211_peer_measurement_status {
	NL80211_PMSR_STATUS_SUCCESS,
	NL80211_PMSR_STATUS_REFUSED,
	NL80211_PMSR_STATUS_TIMEOUT,
	NL80211_PMSR_STATUS_FAILURE,
};

enum nl80211_peer_measurement_type {
	NL80211_PMSR_TYPE_INVALID,
	NL80211_PMSR_TYPE_FTM,
	NUM_NL80211_PMSR_TYPES,
	NL80211_PMSR_TYPE_MAX = NUM_NL80211_PMSR_TYPES - 1
};

enum nl80211_peer_measurement_ftm_failure_reasons {
	NL80211_PMSR_FTM_FAILURE_UNSPECIFIED,
	NL80211_PMSR_FTM_FAILURE_NO_RESPONSE,
	NL80211_PMSR_FTM_FAILURE_REJECTED,
	NL80211_PMSR_FTM_FAILURE_WRONG_CHANNEL,
	NL80211_PMSR_FTM_FAILURE_PEER_NOT_CAPABLE,
	NL80211_PMSR_FTM_FAILURE_INVALID_TIMESTAMP,
	NL80211_PMSR_FTM_FAILURE_PEER_BUSY,
	NL80211_PMSR_FTM_FAILURE_BAD_CHANGED_PARAMS,
};

/* [LNX] include/net/cfg80211.h: the FTM measurement result. The interface's record
 * is much wider (the location, the rates, the distance statistics); the members
 * carried here are the ones the compiled set names, in the interface's order. */
struct cfg80211_pmsr_ftm_result {
	const u8 *lci;
	const u8 *civicloc;
	size_t	lci_len;
	size_t	civicloc_len;
	enum nl80211_peer_measurement_ftm_failure_reasons failure_reason;
	u32	num_ftmr_attempts, num_ftmr_successes;
	s16	burst_index;
	u8	busy_retry_time;
	s32	rssi_avg;
	s32	rssi_spread;
	s64	rtt_avg;
	s64	rtt_variance;
	s64	rtt_spread;

	u32	rssi_avg_valid:1,
		rssi_spread_valid:1,
		rtt_avg_valid:1,
		rtt_variance_valid:1,
		rtt_spread_valid:1,
		num_ftmr_attempts_valid:1,
		num_ftmr_successes_valid:1;
};

/* [LNX] include/net/cfg80211.h: the peer-measurement result record a driver's
 * callback fills. */
struct cfg80211_pmsr_result {
	u64	host_time, ap_tsf;
	enum nl80211_peer_measurement_status status;
	u8	addr[ETH_ALEN];
	u8	final:1,
		ap_tsf_valid:1;
	enum nl80211_peer_measurement_type type;
	union {
		struct cfg80211_pmsr_ftm_result ftm;
	};
};

/* [LNX] include/net/cfg80211.h: the peer-measurement capability record a driver
 * registers. The interface's FTM block carries the whole ranging-capability set;
 * the members carried here are the ones the compiled set names, in the
 * interface's order. */
struct cfg80211_pmsr_capabilities {
	unsigned int	max_peers;
	u8		report_ap_tsf:1,
			randomize_mac_addr:1;
	struct {
		u32	preambles;
		u32	bandwidths;
		s8	max_bursts_exponent;
		u8	max_ftms_per_burst;
		u8	supported:1,
			asap:1,
			non_asap:1,
			request_lci:1,
			request_civicloc:1,
			trigger_based:1,
			non_trigger_based:1;
	} ftm;
};

/* [LNX] include/net/cfg80211.h: the per-peer FTM request and the peer record a
 * peer-measurement request carries. The request record is carried as the
 * interface's header plus a flexible peer array; the fields are the ones the
 * compiled set names. */
struct cfg80211_pmsr_ftm_request_peer {
	enum nl80211_preamble preamble;
	u16	burst_period;
	u8	requested:1,
		asap:1,
		request_lci:1,
		request_civicloc:1,
		trigger_based:1,
		non_trigger_based:1,
		lmr_feedback:1,
		rsta:1;
	u8	num_bursts_exp;
	u8	burst_duration;
	u8	ftms_per_burst;
	u8	ftmr_retries;
	u8	bss_color;
	u32	request_type;
};

struct cfg80211_pmsr_request_peer {
	u8	addr[ETH_ALEN];
	struct cfg80211_chan_def chandef;
	u8	report_ap_tsf:1;
	struct cfg80211_pmsr_ftm_request_peer ftm;
};

struct cfg80211_pmsr_request {
	u64	cookie;
	void	*drv_data;
	u32	n_peers;
	u32	nl_portid;
	u32	timeout;
	u8	mac_addr[ETH_ALEN] __aligned(2);
	u8	mac_addr_mask[ETH_ALEN] __aligned(2);
	struct list_head list;
	struct cfg80211_pmsr_request_peer peers[];
};

/*
 * [LNX] include/net/cfg80211.h:10529,10543: the peer-measurement result and
 * completion reports a driver makes upward. Unreachable on this line: the report
 * path is the FTM initiator (mvm/ftm-initiator.c), which only runs when a
 * userspace peer-measurement request arrives through nl80211, and this port has no
 * nl80211 socket. The bodies (linuxkpi_cfg80211.c) are inert for that reason.
 */
void cfg80211_pmsr_report(struct wireless_dev *wdev,
			  struct cfg80211_pmsr_request *req,
			  struct cfg80211_pmsr_result *result,
			  gfp_t gfp);
void cfg80211_pmsr_complete(struct wireless_dev *wdev,
			    struct cfg80211_pmsr_request *req,
			    gfp_t gfp);

/* [LNX] include/net/cfg80211.h:3254 (record carried whole) */
struct cfg80211_bss {
	struct ieee80211_channel *channel;

	const struct cfg80211_bss_ies __rcu *ies;
	const struct cfg80211_bss_ies __rcu *beacon_ies;
	const struct cfg80211_bss_ies __rcu *proberesp_ies;

	struct cfg80211_bss *hidden_beacon_bss;
	struct cfg80211_bss *transmitted_bss;
	struct list_head nontrans_list;

	s32 signal;

	u64 ts_boottime;

	u16 beacon_interval;
	u16 capability;

	u8 bssid[ETH_ALEN];
	u8 chains;
	s8 chain_signal[IEEE80211_MAX_CHAINS];

	u8 proberesp_ecsa_stuck:1;

	u8 bssid_index;
	u8 max_bssid_indicator;

	u8 use_for;
	u8 cannot_use_reasons;

	u8 priv[] __aligned(sizeof(void *));
};

/* [LNX] include/net/cfg80211.h:332 (record carried whole) */
struct cfg80211_he_bss_color {
	u8 color;
	bool enabled;
	bool partial;
};

/* [LNX] include/net/cfg80211.h:943 (record carried whole) */
struct cfg80211_tid_config {
	const u8 *peer;
	u32 n_tid_conf;
	struct cfg80211_tid_cfg tid_conf[] __counted_by(n_tid_conf);
};

/* [LNX] helper the compiled set names */
/* [LNX] include/net/cfg80211.h:8522 */
void cfg80211_bss_iter(struct wiphy *wiphy,
		       struct cfg80211_chan_def *chandef,
		       void (*iter)(struct wiphy *wiphy,
				    struct cfg80211_bss *bss,
				    void *data),
		       void *iter_data);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/cfg80211.h:1010 */
void cfg80211_chandef_create(struct cfg80211_chan_def *chandef,
			     struct ieee80211_channel *channel,
			     enum nl80211_channel_type chantype);

bool cfg80211_channel_is_psc(struct ieee80211_channel *chan);





const u8 *cfg80211_find_ie(u8 eid, const u8 *ies, int len);

/* [LNX] include/net/cfg80211.h:8365: the interface's own SSID comparison, an
 * inline the 8821CU driver calls to match a scan result against a request. */
static inline bool cfg80211_ssid_eq(struct cfg80211_ssid *a, struct cfg80211_ssid *b)
{
	if (WARN_ON(!a || !b))
		return false;

	if (a->ssid_len != b->ssid_len)
		return false;

	return memcmp(a->ssid, b->ssid, a->ssid_len) == 0;
}



/* [LNX] helper the compiled set names */
/* [LNX] include/net/cfg80211.h:8354 */
int cfg80211_get_ies_channel_number(const u8 *ie, size_t ielen,
				    enum nl80211_band band);

/* [LNX] helper the compiled set names */
/* [LNX] include/net/cfg80211.h:6813 */
#define lockdep_assert_wiphy(wiphy) lockdep_assert_held(&(wiphy)->mtx)

/* [LNX] helper the compiled set names */
/* [LNX] include/net/cfg80211.h:8088 */
int regulatory_hint(struct wiphy *wiphy, const char *alpha2);

/*
 * [LNX] include/net/cfg80211.h:8101,8115: the regdomain submission a self-managed
 * driver makes. The interface validates and installs the domain through the
 * regulatory core and requires the wiphy to be flagged
 * REGULATORY_WIPHY_SELF_MANAGED; this port has no regulatory core, so the domain
 * is stored on the wiphy (the member the face already carries) and the sync form
 * is the same store - stated at the body (linuxkpi_cfg80211.c). The driver calls
 * both from its regulatory setup (mvm/nvm.c:615/:667).
 */
int regulatory_set_wiphy_regd(struct wiphy *wiphy,
			      struct ieee80211_regdomain *rd);
int regulatory_set_wiphy_regd_sync(struct wiphy *wiphy,
				   struct ieee80211_regdomain *rd);

/*
 * [LNX] include/net/cfg80211.h:8200: the WMM-rule query against the internal
 * regulatory database. The body (linuxkpi_cfg80211.c) answers the interface's own
 * "no rule" result, -ENODATA, because this port carries no database; the caller
 * (iwl-nvm-parse.c:1868) only applies a rule when the query succeeds.
 */
int reg_query_regdb_wmm(char *alpha2, int freq,
			struct ieee80211_reg_rule *rule);

/* [LNX] include/net/cfg80211.h:8133-8134: the custom-domain submission the compiled
 * set calls with its static domain tables. */
void wiphy_apply_custom_regulatory(struct wiphy *wiphy,
				   const struct ieee80211_regdomain *regd);

void wiphy_lock(struct wiphy *wiphy)
	__acquires(&wiphy->mtx);



void wiphy_rfkill_set_hw_state(struct wiphy *wiphy, bool blocked);



/* [LNX] helper the compiled set names */
/* [LNX] include/net/cfg80211.h:8749 */
void wiphy_rfkill_start_polling(struct wiphy *wiphy);

void wiphy_rfkill_stop_polling(struct wiphy *wiphy);



void wiphy_unlock(struct wiphy *wiphy)
	__releases(&wiphy->mtx);

/*
 * [LNX] include/net/cfg80211.h:10394: shut down every interface of a wiphy after a
 * fatal device error. Unreachable on this line: its one caller is the MEI
 * nic-stolen handler (mvm/ops.c:1185), which is built only with CONFIG_IWLMEI and
 * this line excludes the mei units. The body (linuxkpi_cfg80211.c) is inert for
 * that reason - there is no interface to close and no error to recover from here.
 */
void cfg80211_shutdown_all_interfaces(struct wiphy *wiphy);




/* ------------------------------------------------------------------ */
/* the wiphy work family                                               */
/* ------------------------------------------------------------------ */

/* [LNX] include/net/cfg80211.h:6910-6923,6966-6971: work items the stack runs with the
 * wiphy lock held. The interface's two initializers are inline and transcribed as such
 * (they name INIT_LIST_HEAD and a member store); the queue/cancel/flush entry points are
 * declared - their bodies are the serializing worker's, which the port owns. */
struct wiphy_work;
typedef void (*wiphy_work_func_t)(struct wiphy *, struct wiphy_work *);

struct wiphy_work {
	struct list_head entry;
	wiphy_work_func_t func;
};

static __always_inline __unused void
wiphy_work_init(struct wiphy_work *work, wiphy_work_func_t func)
{
	INIT_LIST_HEAD(&work->entry);
	work->func = func;
}

struct wiphy_delayed_work {
	struct wiphy_work work;
	struct wiphy *wiphy;
	struct timer_list timer;
};

static __always_inline __unused void
wiphy_delayed_work_init(struct wiphy_delayed_work *dwork, wiphy_work_func_t func)
{
	wiphy_work_init(&dwork->work, func);
}

void wiphy_work_queue(struct wiphy *wiphy, struct wiphy_work *work);
void wiphy_work_cancel(struct wiphy *wiphy, struct wiphy_work *work);
void wiphy_work_flush(struct wiphy *wiphy, struct wiphy_work *work);
void wiphy_delayed_work_queue(struct wiphy *wiphy, struct wiphy_delayed_work *dwork,
			      unsigned long delay);
void wiphy_delayed_work_cancel(struct wiphy *wiphy,
			       struct wiphy_delayed_work *dwork);
void wiphy_delayed_work_flush(struct wiphy *wiphy,
			      struct wiphy_delayed_work *dwork);

/* [LNX] include/net/cfg80211.h: the scan request a driver receives, transcribed with its
 * members and their order; the channel array is the record's tail. */
struct cfg80211_scan_request {
	struct cfg80211_ssid *ssids;
	int n_ssids;
	u32 n_channels;
	const u8 *ie;
	size_t ie_len;
	u16 duration;
	bool duration_mandatory;
	u32 flags;

	u32 rates[NUM_NL80211_BANDS];

	struct wireless_dev *wdev;

	u8 mac_addr[ETH_ALEN] __aligned(2);
	u8 mac_addr_mask[ETH_ALEN] __aligned(2);
	u8 bssid[ETH_ALEN] __aligned(2);
	struct wiphy *wiphy;
	unsigned long scan_start;
	bool no_cck;
	bool scan_6ghz;
	bool first_part;
	u32 n_6ghz_params;
	struct cfg80211_scan_6ghz_params *scan_6ghz_params;
	s8 tsf_report_link_id;

	/* keep last */
	struct ieee80211_channel *channels[];
};
/* [LNX] include/net/cfg80211.h:7655 */
extern const unsigned char rfc1042_header[6];

/* [LNX] include/net/cfg80211.h:7142 (the enumerators the compiled set names, at their computed positions) */
enum ieee80211_ap_reg_power {
	IEEE80211_REG_UNSET_AP = 0,
	IEEE80211_REG_LPI_AP = 1,
	IEEE80211_REG_SP_AP = 2,
	IEEE80211_REG_VLP_AP = 3
};

/* [LNX] include/net/cfg80211.h:1288 (the enumerators the compiled set names, at their computed positions) */
enum survey_info_flags {
	SURVEY_INFO_NOISE_DBM = 1
};

/* [LNX] include/uapi/linux/nl80211.h:6049 (the enumerators the compiled set names, at their computed positions) */
enum nl80211_cqm_rssi_threshold_event {
	NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW = 0,
	NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH = 1
};

/* [LNX] include/uapi/linux/nl80211.h:6832 (the enumerators the compiled set names, at their computed positions) */
enum nl80211_feature_flags {
	NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR = 536870912
};

/* [LNX] include/uapi/linux/nl80211.h:4018 (the enumerators the compiled set names, at their computed positions) */
enum nl80211_he_ru_alloc {
	NL80211_RATE_INFO_HE_RU_ALLOC_26 = 0,
	NL80211_RATE_INFO_HE_RU_ALLOC_52 = 1,
	NL80211_RATE_INFO_HE_RU_ALLOC_106 = 2,
	NL80211_RATE_INFO_HE_RU_ALLOC_242 = 3,
	NL80211_RATE_INFO_HE_RU_ALLOC_484 = 4,
	NL80211_RATE_INFO_HE_RU_ALLOC_996 = 5,
	NL80211_RATE_INFO_HE_RU_ALLOC_2x996 = 6
};



/* [LNX] include/uapi/linux/nl80211.h:7320 (the enumerators the compiled set names, at their computed positions) */
enum nl80211_scan_flags {
	NL80211_SCAN_FLAG_RANDOM_ADDR = 8,
	NL80211_SCAN_FLAG_RANDOM_SN = 2048,
	NL80211_SCAN_FLAG_COLOCATED_6GHZ = 16384
};



/* [LNX] include/uapi/linux/nl80211.h:6133 (the enumerators the compiled set names, at their computed positions) */
enum nl80211_tid_config_attr {
	NL80211_TID_CONFIG_ATTR_AMPDU_CTRL = 9,
	NL80211_TID_CONFIG_ATTR_AMSDU_CTRL = 11
};

/* [LNX] include/net/cfg80211.h: the interface's frequency/channel pair */
int
ieee80211_channel_to_frequency(int chan, enum nl80211_band band);
int
ieee80211_frequency_to_channel(int freq);

/* [LNX] include/net/cfg80211.h:7849 */
const u8 *
cfg80211_find_ie_match(u8 eid, const u8 *ies, unsigned int len,
		       const u8 *match, unsigned int match_len,
		       unsigned int match_offset);

/* [LNX] include/net/cfg80211.h:8736 */
void wiphy_rfkill_set_hw_state_reason(struct wiphy *wiphy, bool blocked,
				      enum rfkill_hard_block_reasons reason);

/* [LNX] include/net/cfg80211.h:8757 */
void rfkill_pause_polling(struct rfkill *rfkill);

/* [LNX] include/net/cfg80211.h: the element-search helper */
const struct element *cfg80211_find_elem(u8 eid, const u8 *ies, int len);

/* ------------------------------------------------------------------ */
/* the request records the operation table names                       */
/* ------------------------------------------------------------------ */

/* [LNX] include/net/cfg80211.h:1334 and include/uapi/linux/nl80211.h:3843 */
#define	CFG80211_MAX_NUM_AKM_SUITES	10
#define	NL80211_MAX_NR_CIPHER_SUITES	5

/* [LNX] include/net/cfg80211.h:836-846 (record carried whole) */
struct key_params {
	const u8 *key;
	const u8 *seq;
	int key_len;
	int seq_len;
	u16 vlan_id;
	u32 cipher;
	enum nl80211_key_mode mode;
	const u8 *ltf_keyseed;
	size_t ltf_keyseed_len;
};

/* [LNX] include/net/cfg80211.h:1377-1396 (record carried whole) */
struct cfg80211_crypto_settings {
	u32 wpa_versions;
	u32 cipher_group;
	int n_ciphers_pairwise;
	u32 ciphers_pairwise[NL80211_MAX_NR_CIPHER_SUITES];
	int n_akm_suites;
	u32 akm_suites[CFG80211_MAX_NUM_AKM_SUITES];
	bool control_port;
	__be16 control_port_ethertype;
	bool control_port_no_encrypt;
	bool control_port_over_nl80211;
	bool control_port_no_preauth;
	const u8 *psk;
	const u8 *sae_pwd;
	u8 sae_pwd_len;
	enum nl80211_sae_pwe_mechanism sae_pwe;
};

/* [LNX] include/uapi/linux/nl80211.h:7569-7576 (the enumerators the compiled set names,
 * at their computed positions) */
enum nl80211_bss_select_attr {
	__NL80211_BSS_SELECT_ATTR_INVALID,
	NL80211_BSS_SELECT_ATTR_RSSI,
	NL80211_BSS_SELECT_ATTR_BAND_PREF,
	NL80211_BSS_SELECT_ATTR_RSSI_ADJUST,
};

/* [LNX] include/net/cfg80211.h:3593-3600 (record carried whole) */
struct cfg80211_bss_selection {
	enum nl80211_bss_select_attr behaviour;
	union {
		enum nl80211_band band_pref;
		struct cfg80211_bss_select_adjust adjust;
	} param;
};

/* [LNX] include/net/cfg80211.h:512-526 (the enumerators the compiled set names, at
 * their computed positions) */
enum ieee80211_edmg_bw_config {
	IEEE80211_EDMG_BW_CONFIG_4	= 4,
	IEEE80211_EDMG_BW_CONFIG_5	= 5,
	IEEE80211_EDMG_BW_CONFIG_6	= 6,
	IEEE80211_EDMG_BW_CONFIG_7	= 7,
	IEEE80211_EDMG_BW_CONFIG_8	= 8,
	IEEE80211_EDMG_BW_CONFIG_9	= 9,
	IEEE80211_EDMG_BW_CONFIG_10	= 10,
	IEEE80211_EDMG_BW_CONFIG_11	= 11,
	IEEE80211_EDMG_BW_CONFIG_12	= 12,
	IEEE80211_EDMG_BW_CONFIG_13	= 13,
	IEEE80211_EDMG_BW_CONFIG_14	= 14,
	IEEE80211_EDMG_BW_CONFIG_15	= 15,
};

/* [LNX] include/net/cfg80211.h:540-543 (record carried whole) */
struct ieee80211_edmg {
	u8 channels;
	enum ieee80211_edmg_bw_config bw_config;
};

/* [LNX] include/net/cfg80211.h:3664-3699 (record carried whole) */
struct cfg80211_connect_params {
	struct ieee80211_channel *channel;
	struct ieee80211_channel *channel_hint;
	const u8 *bssid;
	const u8 *bssid_hint;
	const u8 *ssid;
	size_t ssid_len;
	enum nl80211_auth_type auth_type;
	const u8 *ie;
	size_t ie_len;
	bool privacy;
	enum nl80211_mfp mfp;
	struct cfg80211_crypto_settings crypto;
	const u8 *key;
	u8 key_len, key_idx;
	u32 flags;
	int bg_scan_period;
	struct ieee80211_ht_cap ht_capa;
	struct ieee80211_ht_cap ht_capa_mask;
	struct ieee80211_vht_cap vht_capa;
	struct ieee80211_vht_cap vht_capa_mask;
	bool pbss;
	struct cfg80211_bss_selection bss_select;
	const u8 *prev_bssid;
	const u8 *fils_erp_username;
	size_t fils_erp_username_len;
	const u8 *fils_erp_realm;
	size_t fils_erp_realm_len;
	u16 fils_erp_next_seq_num;
	const u8 *fils_erp_rrk;
	size_t fils_erp_rrk_len;
	bool want_1x;
	struct ieee80211_edmg edmg;
};

/* [LNX] include/net/cfg80211.h:3780-3790 (record carried whole) */
struct cfg80211_pmksa {
	const u8 *bssid;
	const u8 *pmkid;
	const u8 *pmk;
	size_t pmk_len;
	const u8 *ssid;
	size_t ssid_len;
	const u8 *cache_id;
	u32 pmk_lifetime;
	u8 pmk_reauth_threshold;
};

/* [LNX] include/net/cfg80211.h:3565-3586 (record carried whole) */
struct cfg80211_ibss_params {
	const u8 *ssid;
	const u8 *bssid;
	struct cfg80211_chan_def chandef;
	const u8 *ie;
	u8 ssid_len, ie_len;
	u16 beacon_interval;
	u32 basic_rates;
	bool channel_fixed;
	bool privacy;
	bool control_port;
	bool control_port_over_nl80211;
	bool userspace_handles_dfs;
	int mcast_rate[NUM_NL80211_BANDS];
	struct ieee80211_ht_cap ht_capa;
	struct ieee80211_ht_cap ht_capa_mask;
	struct key_params *wep_keys;
	int wep_tx_key;
};

/* [LNX] include/net/cfg80211.h:1484-1520 (record carried whole) */
struct cfg80211_beacon_data {
	unsigned int link_id;

	const u8 *head, *tail;
	const u8 *beacon_ies;
	const u8 *proberesp_ies;
	const u8 *assocresp_ies;
	const u8 *probe_resp;
	const u8 *lci;
	const u8 *civicloc;
	struct cfg80211_mbssid_elems *mbssid_ies;
	struct cfg80211_rnr_elems *rnr_ies;
	s8 ftm_responder;

	size_t head_len, tail_len;
	size_t beacon_ies_len;
	size_t proberesp_ies_len;
	size_t assocresp_ies_len;
	size_t probe_resp_len;
	size_t lci_len;
	size_t civicloc_len;
	struct cfg80211_he_bss_color he_bss_color;
	bool he_bss_color_valid;

	bool ht_required, vht_required;
	const struct ieee80211_ht_operation *ht_oper;
	const struct ieee80211_vht_operation *vht_oper;
	const struct ieee80211_he_operation *he_oper;
	const struct ieee80211_eht_operation *eht_oper;
	const struct ieee80211_uhr_operation *uhr_oper;
};

/* [LNX] include/net/cfg80211.h: the FILS / unsolicited-probe / S1G beacon parameter
 * records the AP settings embed by value. */
struct cfg80211_fils_discovery {
	bool update;
	u32 min_interval;
	u32 max_interval;
	size_t tmpl_len;
	const u8 *tmpl;
};

struct cfg80211_unsol_bcast_probe_resp {
	bool update;
	u32 interval;
	size_t tmpl_len;
	const u8 *tmpl;
};

struct cfg80211_s1g_short_beacon {
	bool update;
	const u8 *short_head;
	const u8 *short_tail;
	size_t short_head_len;
	size_t short_tail_len;
};

/* [LNX] include/net/cfg80211.h:1402-1411 (record carried whole) */
struct cfg80211_mbssid_config {
	struct wireless_dev *tx_wdev;
	u8 tx_link_id;
	u8 index;
	bool ema;
};

/* [LNX] include/net/cfg80211.h:315-325 (record carried whole) */
struct ieee80211_he_obss_pd {
	bool enable;
	u8 sr_ctrl;
	u8 non_srg_max_offset;
	u8 min_offset;
	u8 max_offset;
	u8 bss_color_bitmap[8];
	u8 partial_bssid_bitmap[8];
};

/* [LNX] include/net/cfg80211.h: the EHT capability element: the fixed part the face
 * already carries, plus the optional trailing bytes. */
struct ieee80211_eht_cap_elem {
	struct ieee80211_eht_cap_elem_fixed fixed;
	u8 optional[];
} __packed;

/* [LNX] include/net/cfg80211.h:1627-1665 (record carried whole) */
struct cfg80211_ap_settings {
	struct cfg80211_chan_def chandef;

	struct cfg80211_beacon_data beacon;

	int beacon_interval, dtim_period;
	const u8 *ssid;
	size_t ssid_len;
	enum nl80211_hidden_ssid hidden_ssid;
	struct cfg80211_crypto_settings crypto;
	bool privacy;
	enum nl80211_auth_type auth_type;
	int inactivity_timeout;
	u8 p2p_ctwindow;
	bool p2p_opp_ps;
	const struct cfg80211_acl_data *acl;
	bool pbss;
	struct cfg80211_bitrate_mask beacon_rate;

	const struct ieee80211_ht_cap *ht_cap;
	const struct ieee80211_vht_cap *vht_cap;
	const struct ieee80211_he_cap_elem *he_cap;
	const struct ieee80211_eht_cap_elem *eht_cap;
	bool twt_responder;
	u32 flags;
	struct ieee80211_he_obss_pd he_obss_pd;
	struct cfg80211_fils_discovery fils_discovery;
	struct cfg80211_unsol_bcast_probe_resp unsol_bcast_probe_resp;
	struct cfg80211_mbssid_config mbssid_config;
	u8 s1g_long_beacon_period;
	struct cfg80211_s1g_short_beacon s1g_short_beacon;
};

/* [LNX] include/net/cfg80211.h:1671-1685 (record carried whole) */
struct cfg80211_ap_update {
	struct cfg80211_beacon_data beacon;
	struct cfg80211_fils_discovery fils_discovery;
	struct cfg80211_unsol_bcast_probe_resp unsol_bcast_probe_resp;
	struct cfg80211_s1g_short_beacon s1g_short_beacon;
};

/* [LNX] include/net/cfg80211.h: the MAC-address record the ACL list is built from. */
struct mac_address {
	u8 addr[ETH_ALEN];
};

/* [LNX] include/net/cfg80211.h:1528-1536 (record carried whole) */
struct cfg80211_acl_data {
	enum nl80211_acl_policy acl_policy;
	int n_acl_entries;

	/* Keep it last */
	struct mac_address mac_addrs[];
};

/* [LNX] include/net/cfg80211.h:4013-4023 (record carried whole) */
struct cfg80211_mgmt_tx_params {
	struct ieee80211_channel *chan;
	bool offchan;
	unsigned int wait;
	const u8 *buf;
	size_t len;
	bool no_cck;
	bool dont_wait_for_ack;
	int n_csa_offsets;
	const u16 *csa_offsets;
	int link_id;
};

/* [LNX] include/net/cfg80211.h:2662-2673 (record carried whole) */
struct bss_parameters {
	int link_id;
	int use_cts_prot;
	int use_short_preamble;
	int use_short_slot_time;
	const u8 *basic_rates;
	u8 basic_rates_len;
	int ap_isolate;
	int ht_opmode;
	s8 p2p_ctwindow, p2p_opp_ps;
};

/* [LNX] include/net/cfg80211.h:2860-2867 (record carried whole) */
struct ieee80211_txq_params {
	enum nl80211_ac ac;
	u16 txop;
	u16 cwmin;
	u16 cwmax;
	u8 aifs;
	int link_id;
};

/* [LNX] include/net/cfg80211.h:1958-1963 (record carried whole) */
struct station_del_parameters {
	const u8 *mac;
	u8 subtype;
	u16 reason_code;
	int link_id;
};

/* [LNX] include/net/cfg80211.h:4843-4846 (record carried whole) */
struct mgmt_frame_regs {
	u32 global_stypes, interface_stypes;
	u32 global_mcast_stypes, interface_mcast_stypes;
};

/* [LNX] include/net/cfg80211.h: the roaming report the compiled set fills by member
 * and hands to cfg80211_roamed(). Only the members a compiled unit writes are carried,
 * in the interface's order; the interface's FILS block between resp_ie_len and links
 * arrives with a caller that names it. */
struct cfg80211_roam_info {
	const u8 *req_ie;
	size_t req_ie_len;
	const u8 *resp_ie;
	size_t resp_ie_len;

	const u8 *ap_mld_addr;
	u16 valid_links;
	struct {
		const u8 *addr;
		const u8 *bssid;
		struct ieee80211_channel *channel;
		struct cfg80211_bss *bss;
	} links[IEEE80211_MLD_MAX_NUM_LINKS];
};

/* [LNX] include/net/cfg80211.h: the BSS-selection privacy filter, at the interface's
 * values, with the interface's macro over it. */
enum ieee80211_privacy {
	IEEE80211_PRIVACY_ON,
	IEEE80211_PRIVACY_OFF,
	IEEE80211_PRIVACY_ANY
};

#define	IEEE80211_PRIVACY(x)	\
	((x) ? IEEE80211_PRIVACY_ON : IEEE80211_PRIVACY_OFF)

/* [LNX] include/uapi/linux/nl80211.h:5605,7242: the two uapi values the helpers below
 * default to. */
enum nl80211_bss_use_for {
	NL80211_BSS_USE_FOR_NORMAL = 1 << 0,
};

enum nl80211_timeout_reason {
	NL80211_TIMEOUT_UNSPECIFIED,
	NL80211_TIMEOUT_SCAN,
	NL80211_TIMEOUT_AUTH,
	NL80211_TIMEOUT_ASSOC,
};

/* ------------------------------------------------------------------ */
/* the operation table                                                 */
/* ------------------------------------------------------------------ */

/*
 * [LNX] include/net/cfg80211.h:5280-5688: the driver operation table. Transcribed with
 * the interface's members and signatures, in the interface's order; the members the
 * compiled set does not name are omitted (they arrive with a caller), and the two
 * legacy spellings the vendored bytes reach are noted at their sites. `action` is the
 * one member 7.3 no longer has: it was replaced by `mgmt_tx`, and this face carries it
 * as a legacy member for the driver generations whose initializers still name it.
 */
struct cfg80211_ops {
	int	(*suspend)(struct wiphy *wiphy, struct cfg80211_wowlan *wow);
	int	(*resume)(struct wiphy *wiphy);

	struct wireless_dev * (*add_virtual_intf)(struct wiphy *wiphy,
						  const char *name,
						  unsigned char name_assign_type,
						  enum nl80211_iftype type,
						  struct vif_params *params);
	int	(*del_virtual_intf)(struct wiphy *wiphy,
				    struct wireless_dev *wdev);
	int	(*change_virtual_intf)(struct wiphy *wiphy,
				       struct net_device *dev,
				       enum nl80211_iftype type,
				       struct vif_params *params);

	int	(*add_key)(struct wiphy *wiphy, struct wireless_dev *wdev,
			   int link_id, u8 key_index, bool pairwise,
			   const u8 *mac_addr, struct key_params *params);
	int	(*get_key)(struct wiphy *wiphy, struct wireless_dev *wdev,
			   int link_id, u8 key_index, bool pairwise,
			   const u8 *mac_addr, void *cookie,
			   void (*callback)(void *cookie, struct key_params*));
	int	(*del_key)(struct wiphy *wiphy, struct wireless_dev *wdev,
			   int link_id, u8 key_index, bool pairwise,
			   const u8 *mac_addr);
	int	(*set_default_key)(struct wiphy *wiphy,
				   struct net_device *netdev, int link_id,
				   u8 key_index, bool unicast, bool multicast);
	int	(*set_default_mgmt_key)(struct wiphy *wiphy,
					struct wireless_dev *wdev, int link_id,
					u8 key_index);

	int	(*start_ap)(struct wiphy *wiphy, struct net_device *dev,
			    struct cfg80211_ap_settings *settings);
	int	(*change_beacon)(struct wiphy *wiphy, struct net_device *dev,
				 struct cfg80211_ap_update *info);
	int	(*stop_ap)(struct wiphy *wiphy, struct net_device *dev,
			   unsigned int link_id);

	int	(*add_station)(struct wiphy *wiphy, struct wireless_dev *wdev,
			       const u8 *mac,
			       struct station_parameters *params);
	int	(*del_station)(struct wiphy *wiphy, struct wireless_dev *wdev,
			       struct station_del_parameters *params);
	int	(*change_station)(struct wiphy *wiphy, struct wireless_dev *wdev,
				  const u8 *mac,
				  struct station_parameters *params);
	int	(*get_station)(struct wiphy *wiphy, struct wireless_dev *wdev,
			       const u8 *mac, struct station_info *sinfo);
	int	(*dump_station)(struct wiphy *wiphy, struct wireless_dev *wdev,
				int idx, u8 *mac, struct station_info *sinfo);

	int	(*change_bss)(struct wiphy *wiphy, struct net_device *dev,
			      struct bss_parameters *params);

	int	(*set_txq_params)(struct wiphy *wiphy, struct net_device *dev,
				  struct ieee80211_txq_params *params);

	int	(*set_monitor_channel)(struct wiphy *wiphy,
				       struct net_device *dev,
				       struct cfg80211_chan_def *chandef);

	int	(*scan)(struct wiphy *wiphy,
			struct cfg80211_scan_request *request);

	int	(*connect)(struct wiphy *wiphy, struct net_device *dev,
			   struct cfg80211_connect_params *sme);
	int	(*disconnect)(struct wiphy *wiphy, struct net_device *dev,
			      u16 reason_code);

	int	(*join_ibss)(struct wiphy *wiphy, struct net_device *dev,
			     struct cfg80211_ibss_params *params);
	int	(*leave_ibss)(struct wiphy *wiphy, struct net_device *dev);

	int	(*set_wiphy_params)(struct wiphy *wiphy, int radio_idx,
				    u32 changed);

	int	(*set_tx_power)(struct wiphy *wiphy, struct wireless_dev *wdev,
				int radio_idx,
				enum nl80211_tx_power_setting type, int mbm);
	int	(*get_tx_power)(struct wiphy *wiphy, struct wireless_dev *wdev,
				int radio_idx, unsigned int link_id, int *dbm);

	void	(*rfkill_poll)(struct wiphy *wiphy);

	int	(*dump_survey)(struct wiphy *wiphy, struct net_device *netdev,
			int idx, struct survey_info *info);

	int	(*set_pmksa)(struct wiphy *wiphy, struct net_device *netdev,
			     struct cfg80211_pmksa *pmksa);
	int	(*del_pmksa)(struct wiphy *wiphy, struct net_device *netdev,
			     struct cfg80211_pmksa *pmksa);
	int	(*flush_pmksa)(struct wiphy *wiphy, struct net_device *netdev);

	int	(*remain_on_channel)(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     struct ieee80211_channel *chan,
				     unsigned int duration,
				     u64 cookie, const u8 *rx_addr);
	int	(*cancel_remain_on_channel)(struct wiphy *wiphy,
					    struct wireless_dev *wdev,
					    u64 cookie);

	int	(*mgmt_tx)(struct wiphy *wiphy, struct wireless_dev *wdev,
			   struct cfg80211_mgmt_tx_params *params,
			   u64 cookie);
	/*
	 * The legacy management-frame transmit callback: 7.3 replaced it with
	 * `mgmt_tx`, and it is carried here for the older driver initializers that
	 * still name `.action`.
	 */
	int	(*action)(struct wiphy *wiphy, struct wireless_dev *wdev,
			  struct cfg80211_mgmt_tx_params *params,
			  u64 *cookie);

	int	(*set_power_mgmt)(struct wiphy *wiphy, struct net_device *dev,
				  bool enabled, int timeout);

	void	(*update_mgmt_frame_registrations)(struct wiphy *wiphy,
						   struct wireless_dev *wdev,
						   struct mgmt_frame_regs *upd);

	int	(*sched_scan_start)(struct wiphy *wiphy,
				struct net_device *dev,
				struct cfg80211_sched_scan_request *request);
	int	(*sched_scan_stop)(struct wiphy *wiphy, struct net_device *dev,
				   u64 reqid);

	int	(*set_rekey_data)(struct wiphy *wiphy, struct net_device *dev,
				  struct cfg80211_gtk_rekey_data *data);

	int	(*tdls_mgmt)(struct wiphy *wiphy, struct net_device *dev,
			     const u8 *peer, int link_id,
			     u8 action_code, u8 dialog_token, u16 status_code,
			     u32 peer_capability, bool initiator,
			     const u8 *buf, size_t len);
	int	(*tdls_oper)(struct wiphy *wiphy, struct net_device *dev,
			     const u8 *peer, enum nl80211_tdls_operation oper);

	int	(*start_p2p_device)(struct wiphy *wiphy,
				    struct wireless_dev *wdev);
	void	(*stop_p2p_device)(struct wiphy *wiphy,
				   struct wireless_dev *wdev);

	int	(*set_mac_acl)(struct wiphy *wiphy, struct net_device *dev,
			       const struct cfg80211_acl_data *params);

	int	(*update_ft_ies)(struct wiphy *wiphy, struct net_device *dev,
				 struct cfg80211_update_ft_ies_params *ftie);
};

/* ------------------------------------------------------------------ */
/* the notification helpers the compiled set calls                     */
/* ------------------------------------------------------------------ */

/* [LNX] include/net/cfg80211.h: the BSS and scan helpers. Declared as the interface
 * declares them; their bodies belong to the cfg80211 seam this port owns. */
struct cfg80211_bss * __must_check
cfg80211_inform_bss_frame(struct wiphy *wiphy,
			  struct ieee80211_channel *rx_channel,
			  struct ieee80211_mgmt *mgmt, size_t len,
			  s32 signal, gfp_t gfp);
struct cfg80211_bss *
cfg80211_get_bss(struct wiphy *wiphy, struct ieee80211_channel *channel,
		 const u8 *bssid, const u8 *ssid, size_t ssid_len,
		 enum ieee80211_bss_type bss_type,
		 enum ieee80211_privacy privacy);
void cfg80211_put_bss(struct wiphy *wiphy, struct cfg80211_bss *bss);
void cfg80211_unlink_bss(struct wiphy *wiphy, struct cfg80211_bss *bss);
void cfg80211_scan_done(struct cfg80211_scan_request *request,
			struct cfg80211_scan_info *info);

/*
 * [LNX] include/net/cfg80211.h:10643: reset every scan entry of a wiphy. The driver
 * calls it when it restarts the firmware (mvm/fw.c:447), to drop the BSS rows whose
 * GP2 timestamps are no longer meaningful. The body (linuxkpi_cfg80211.c) empties
 * this port's BSS table, which is the substitute's whole scan-entry store.
 */
void cfg80211_bss_flush(struct wiphy *wiphy);

/* [LNX] include/net/cfg80211.h: the connection/link notification helpers. */
void cfg80211_connect_result(struct net_device *dev, const u8 *bssid,
			     const u8 *req_ie, size_t req_ie_len,
			     const u8 *resp_ie, size_t resp_ie_len,
			     u16 status, gfp_t gfp);
void cfg80211_disconnected(struct net_device *dev, u16 reason,
			   const u8 *ie, size_t ie_len,
			   bool locally_generated, gfp_t gfp);
void cfg80211_roamed(struct net_device *dev, struct cfg80211_roam_info *info,
		     gfp_t gfp);
void cfg80211_ibss_joined(struct net_device *dev, const u8 *bssid,
			  struct ieee80211_channel *channel, gfp_t gfp);
void cfg80211_new_sta(struct wireless_dev *wdev, const u8 *mac_addr,
		      struct station_info *sinfo, gfp_t gfp);
void cfg80211_del_sta(struct wireless_dev *wdev, const u8 *mac_addr,
		      gfp_t gfp);
void cfg80211_michael_mic_failure(struct net_device *dev, const u8 *addr,
				  enum nl80211_key_type key_type, int key_id,
				  const u8 *tsc, gfp_t gfp);

/* [LNX] include/net/cfg80211.h: the management-frame and remain-on-channel
 * notifications. */
bool cfg80211_rx_mgmt(struct wireless_dev *wdev, int freq, int sig_dbm,
		      const u8 *buf, size_t len, u32 flags);

/*
 * [LNX] include/net/cfg80211.h:8659: report an unprotected deauth/disassoc (or a
 * BIGTK-invalid beacon) that was dropped in station mode. Unreachable on this line:
 * its callers are the MQ receive path (mvm/rxmq.c:409) and the MLD path
 * (mld/rx.c:1901), and the AC 7260 is a non-MQ, non-MLD part (cfg/7000.c leaves
 * mq_rx_supported clear), so its receive path never reaches it. The body
 * (linuxkpi_cfg80211.c) is inert - no nl80211 listener exists here to be told.
 */
void cfg80211_rx_unprot_mlme_mgmt(struct net_device *dev,
				  const u8 *buf, size_t len);
void cfg80211_mgmt_tx_status(struct wireless_dev *wdev, u64 cookie,
			     const u8 *buf, size_t len, bool ack, gfp_t gfp);
void cfg80211_ready_on_channel(struct wireless_dev *wdev, u64 cookie,
			       struct ieee80211_channel *chan,
			       unsigned int duration, gfp_t gfp);
void cfg80211_remain_on_channel_expired(struct wireless_dev *wdev, u64 cookie,
					struct ieee80211_channel *chan,
					gfp_t gfp);
void cfg80211_ch_switch_notify(struct net_device *dev,
			       struct cfg80211_chan_def *chandef,
			       unsigned int link_id);

/*
 * [LNX] include/net/cfg80211.h:7491: the channel lookup the compiled set calls.
 * The interface's body (net/wireless/util.c) walks the wiphy's own band tables,
 * which the caller already holds - no port state is involved - so it is carried
 * as an inline. That is also what keeps the mac80211 substitute (which builds a
 * scan request out of these channel records) from having to link the cfg80211
 * substitute just for this walk.
 */
static __always_inline __unused struct ieee80211_channel *
ieee80211_get_channel(struct wiphy *wiphy, int freq)
{
	unsigned int b, c;

	if (wiphy == NULL)
		return NULL;
	for (b = 0; b < NUM_NL80211_BANDS; b++) {
		struct ieee80211_supported_band *band = wiphy->bands[b];

		if (band == NULL)
			continue;
		for (c = 0; c < (unsigned int)band->n_channels; c++)
			if ((int)band->channels[c].center_freq == freq)
				return &band->channels[c];
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/* the vendor-command surface                                          */
/* ------------------------------------------------------------------ */

/* [LNX] include/uapi/linux/nl80211.h:7488-7491 */
struct nl80211_vendor_cmd_info {
	__u32 vendor_id;
	__u32 subcmd;
};

/* [LNX] include/net/cfg80211.h: the vendor-command flags and the raw-data policy tag the
 * compiled set's vendor command table is written with. */
enum wiphy_vendor_command_flags {
	WIPHY_VENDOR_CMD_NEED_WDEV = BIT(0),
	WIPHY_VENDOR_CMD_NEED_NETDEV = BIT(1),
	WIPHY_VENDOR_CMD_NEED_RUNNING = BIT(2),
};

struct nla_policy;

/* [LNX] include/net/cfg80211.h:6001 */
#define	VENDOR_CMD_RAW_DATA ((const struct nla_policy *)(long)(-ENODATA))

/* [LNX] include/net/cfg80211.h: the vendor command record the compiled set's table is
 * built from. */
struct wiphy_vendor_command {
	struct nl80211_vendor_cmd_info info;
	u32 flags;
	int (*doit)(struct wiphy *wiphy, struct wireless_dev *wdev,
		    const void *data, int data_len);
	int (*dumpit)(struct wiphy *wiphy, struct wireless_dev *wdev,
		      struct sk_buff *skb, const void *data, int data_len,
		      unsigned long *storage);
	const struct nla_policy *policy;
	unsigned int maxattr;
};

/* [LNX] include/uapi/linux/nl80211.h:1597: the vendor command id (at its computed
 * position in the command enumeration). */
#define	NL80211_CMD_VENDOR	103

/* [LNX] include/net/cfg80211.h: the vendor-command/event reply helpers the compiled set
 * calls. Declared as the interface declares them. */
struct sk_buff *
cfg80211_vendor_cmd_alloc_reply_skb(struct wiphy *wiphy, int approxlen);
int cfg80211_vendor_cmd_reply(struct sk_buff *skb);
struct sk_buff *
cfg80211_vendor_event_alloc(struct wiphy *wiphy, struct wireless_dev *wdev,
			    int approxlen, int event_idx, gfp_t gfp);
void cfg80211_vendor_event(struct sk_buff *skb, gfp_t gfp);

/* ------------------------------------------------------------------ */
/* The remaining nl80211 enumerators the compiled set names,
 * each line names the interface header and line it was transcribed from. */
/* ------------------------------------------------------------------ */

	/* [LNX] uapi/linux/nl80211.h:929 */
	#define	NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE	262144
	/* [LNX] uapi/linux/nl80211.h:930 */
	#define	NL80211_FEATURE_DS_PARAM_SET_IE_IN_PROBES	524288
	/* [LNX] uapi/linux/nl80211.h:935 */
	#define	NL80211_FEATURE_DYNAMIC_SMPS	33554432
	/* [LNX] uapi/linux/nl80211.h:917 */
	#define	NL80211_FEATURE_HT_IBSS	2
	/* [LNX] uapi/linux/nl80211.h:921 */
	#define	NL80211_FEATURE_LOW_PRIORITY_SCAN	64
	/* [LNX] uapi/linux/nl80211.h:925 */
	#define	NL80211_FEATURE_P2P_GO_CTWIN	2048
	/* [LNX] uapi/linux/nl80211.h:925 */
	#define	NL80211_FEATURE_P2P_GO_OPPPS	4096
	/* [LNX] uapi/linux/nl80211.h:931 */
	#define	NL80211_FEATURE_QUIET	2097152
	/* [LNX] uapi/linux/nl80211.h:939 */
	#define	NL80211_FEATURE_SCHED_SCAN_RANDOM_MAC_ADDR	1073741824
	/* [LNX] uapi/linux/nl80211.h:6864 */
	#define	NL80211_FEATURE_ND_RANDOM_MAC_ADDR	2147483648U
	/* [LNX] uapi/linux/nl80211.h:935 */
	#define	NL80211_FEATURE_STATIC_SMPS	16777216
	/* [LNX] uapi/linux/nl80211.h:936 */
	#define	NL80211_FEATURE_SUPPORTS_WMM_ADMISSION	67108864
	/* [LNX] uapi/linux/nl80211.h:937 */
	#define	NL80211_FEATURE_TDLS_CHANNEL_SWITCH	268435456
	/* [LNX] uapi/linux/nl80211.h:932 */
	#define	NL80211_FEATURE_TX_POWER_INSERTION	4194304
	/* [LNX] uapi/linux/nl80211.h:930 */
	#define	NL80211_FEATURE_WFA_TPC_IE_IN_PROBES	1048576
	/* [LNX] uapi/linux/nl80211.h:1117 */
	#define	NL80211_FTM_STATS_ASAP_NUM	4
	/* [LNX] uapi/linux/nl80211.h:1117 */
	#define	NL80211_FTM_STATS_FAILED_NUM	3
	/* [LNX] uapi/linux/nl80211.h:1117 */
	#define	NL80211_FTM_STATS_NON_ASAP_NUM	5
	/* [LNX] uapi/linux/nl80211.h:1120 */
	#define	NL80211_FTM_STATS_OUT_OF_WINDOW_TRIGGERS_NUM	9
	/* [LNX] uapi/linux/nl80211.h:1116 */
	#define	NL80211_FTM_STATS_PARTIAL_NUM	2
	/* [LNX] uapi/linux/nl80211.h:1119 */
	#define	NL80211_FTM_STATS_RESCHEDULE_REQUESTS_NUM	8
	/* [LNX] uapi/linux/nl80211.h:1115 */
	#define	NL80211_FTM_STATS_SUCCESS_NUM	1
	/* [LNX] uapi/linux/nl80211.h:1118 */
	#define	NL80211_FTM_STATS_TOTAL_DURATION_MSEC	6
	/* [LNX] uapi/linux/nl80211.h:1119 */
	#define	NL80211_FTM_STATS_UNKNOWN_TRIGGERS_NUM	7
	/* [LNX] uapi/linux/nl80211.h:3835 */
	#define	NL80211_MAX_SUPP_REG_RULES	128
	/* [LNX] uapi/linux/nl80211.h:650 */
	#define	NL80211_RRF_ALLOW_20MHZ_ACTIVITY	33554432
	/* [LNX] uapi/linux/nl80211.h:650 */
	#define	NL80211_RRF_ALLOW_6GHZ_VLP_AP	16777216
	/* [LNX] uapi/linux/nl80211.h:648 */
	#define	NL80211_RRF_DFS_CONCURRENT	2097152
	/* [LNX] uapi/linux/nl80211.h:645 */
	#define	NL80211_RRF_NO_320MHZ	262144
	/* [LNX] uapi/linux/nl80211.h:649 */
	#define	NL80211_RRF_NO_6GHZ_AFC_CLIENT	8388608
	/* [LNX] uapi/linux/nl80211.h:648 */
	#define	NL80211_RRF_NO_6GHZ_VLP_CLIENT	4194304
	/* [LNX] uapi/linux/nl80211.h:645 */
	#define	NL80211_RRF_NO_EHT	524288
	/* [LNX] uapi/linux/nl80211.h:644 */
	#define	NL80211_RRF_NO_HE	131072
	/* [LNX] uapi/linux/nl80211.h:5041-5042 */
	#define	NL80211_RRF_NO_HT40MINUS	8192
	#define	NL80211_RRF_NO_HT40PLUS	16384
	/* [LNX] uapi/linux/nl80211.h:5060 */
	#define	NL80211_RRF_NO_HT40	(NL80211_RRF_NO_HT40MINUS | NL80211_RRF_NO_HT40PLUS)
	/* [LNX] uapi/linux/nl80211.h:651 */
	#define	NL80211_RRF_NO_UHR	67108864
	/* [LNX] uapi/linux/nl80211.h:1018 */
	#define	NL80211_SCAN_FLAG_ACCEPT_BCAST_PROBE_RESP	32
	/* [LNX] uapi/linux/nl80211.h:1017 */
	#define	NL80211_SCAN_FLAG_FILS_MAX_CHANNEL_TIME	16
	/* [LNX] uapi/linux/nl80211.h:1019 */
	#define	NL80211_SCAN_FLAG_OCE_PROBE_REQ_DEFERRAL_SUPPRESSION	128
	/* [LNX] uapi/linux/nl80211.h:1018 */
	#define	NL80211_SCAN_FLAG_OCE_PROBE_REQ_HIGH_TX_RATE	64
	/* [LNX] net/cfg80211.h:382 */
	#define	SURVEY_INFO_TIME	4
	/* [LNX] net/cfg80211.h:382 */
	#define	SURVEY_INFO_TIME_BUSY	8
	/* [LNX] net/cfg80211.h:384 */
	#define	SURVEY_INFO_TIME_RX	32
	/* [LNX] net/cfg80211.h:385 */
	#define	SURVEY_INFO_TIME_SCAN	128
	/* [LNX] net/cfg80211.h:384 */
	#define	SURVEY_INFO_TIME_TX	64

/* [LNX] include/uapi/linux/nl80211.h: the EHT RU-allocation enumeration a compiled
 * consumer names as a type and by member. */
enum nl80211_eht_ru_alloc {
	NL80211_RATE_INFO_EHT_RU_ALLOC_26,
	NL80211_RATE_INFO_EHT_RU_ALLOC_52,
	NL80211_RATE_INFO_EHT_RU_ALLOC_52P26,
	NL80211_RATE_INFO_EHT_RU_ALLOC_106,
	NL80211_RATE_INFO_EHT_RU_ALLOC_106P26,
	NL80211_RATE_INFO_EHT_RU_ALLOC_242,
	NL80211_RATE_INFO_EHT_RU_ALLOC_484,
	NL80211_RATE_INFO_EHT_RU_ALLOC_484P242,
	NL80211_RATE_INFO_EHT_RU_ALLOC_996,
	NL80211_RATE_INFO_EHT_RU_ALLOC_996P484,
	NL80211_RATE_INFO_EHT_RU_ALLOC_996P484P242,
	NL80211_RATE_INFO_EHT_RU_ALLOC_2x996,
	NL80211_RATE_INFO_EHT_RU_ALLOC_2x996P484,
	NL80211_RATE_INFO_EHT_RU_ALLOC_3x996,
	NL80211_RATE_INFO_EHT_RU_ALLOC_3x996P484,
	NL80211_RATE_INFO_EHT_RU_ALLOC_4x996,
};

/* [LNX] include/net/cfg80211.h: the hardware-timestamp configuration a driver's
 * set_hw_timestamp callback receives. */
struct cfg80211_set_hw_timestamp {
	const u8	*macaddr;
	bool		enable;
};

#endif /* _LINUXKPI_NET_CFG80211_H_ */

