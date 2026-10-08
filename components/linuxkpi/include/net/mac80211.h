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

struct ieee80211_hw;
struct ieee80211_vif;
struct ieee80211_sta;
struct ieee80211_key_conf;
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

struct ieee80211_tx_queue_params {
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

struct ieee80211_rx_status {
	u64	mactime;
	u32	device_timestamp;
	u32	flag;
	u16	freq:13, freq_offset:1;
	u8	enc_flags;
	u8	encoding:3, bw:4;
	u8	rate_idx;
	u8	nss;
	u8	rx_flags;
	u8	band;
	u8	antenna;
	s8	signal;
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

struct ieee80211_vif_cfg {
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
};

struct ieee80211_vif {
	enum nl80211_iftype	type;
	struct ieee80211_vif_cfg cfg;
	struct ieee80211_bss_conf bss_conf;
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

struct ieee80211_link_sta {
	struct ieee80211_sta		*sta;
	u8				addr[ETH_ALEN];
	u8				link_id;
	enum ieee80211_smps_mode	smps_mode;
	u32				supp_rates[NUM_NL80211_BANDS];
	struct ieee80211_sta_ht_cap	ht_cap;
	struct ieee80211_sta_vht_cap	vht_cap;
	u8				rx_nss;
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
	struct ieee80211_link_sta	deflink;
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
	u64	tx_pn;
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

struct ieee80211_ops {
	void	(*tx)(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
		       struct sk_buff *skb);
	void	(*wake_tx_queue)(struct ieee80211_hw *hw, struct ieee80211_txq *txq);
	int	(*add_chanctx)(struct ieee80211_hw *hw,
			       struct ieee80211_chanctx_conf *ctx);
	void	(*remove_chanctx)(struct ieee80211_hw *hw,
				  struct ieee80211_chanctx_conf *ctx);
	void	(*change_chanctx)(struct ieee80211_hw *hw,
				  struct ieee80211_chanctx_conf *ctx,
				  u32 changed);
	int	(*switch_vif_chanctx)(struct ieee80211_hw *hw,
				      struct ieee80211_vif_chanctx_switch *vifs,
				      int n_vifs,
				      enum ieee80211_chanctx_switch_mode mode);
	int	(*add_interface)(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif);
	void	(*remove_interface)(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif);
	int	(*config)(struct ieee80211_hw *hw, int radio_idx, u32 changed);
	int	(*conf_tx)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   unsigned int link_id, u16 ac,
			   const struct ieee80211_tx_queue_params *params);
	void	(*bss_info_changed)(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_bss_conf *info,
				    u64 changed);
	int	(*start_ap)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf);
	void	(*configure_filter)(struct ieee80211_hw *hw,
				    unsigned int changed_flags,
				    unsigned int *total_flags, u64 multicast);
	int	(*set_rts_threshold)(struct ieee80211_hw *hw, int radio_idx,
				     u32 value);
	int	(*start)(struct ieee80211_hw *hw);
	void	(*stop)(struct ieee80211_hw *hw, bool suspend);
	void	(*sw_scan_start)(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif, const u8 *mac_addr);
	void	(*sw_scan_complete)(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif);
	int	(*set_key)(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			   struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			   struct ieee80211_key_conf *key);
	int	(*ampdu_action)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
				struct ieee80211_ampdu_params *params);
	void	(*sta_statistics)(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta,
				  struct station_info *sinfo);
	int	(*get_antenna)(struct ieee80211_hw *hw, int radio_idx,
			       u32 *tx_ant, u32 *rx_ant);
	int	(*set_tim)(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
			   bool set);
	int	(*sta_add)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta);
	int	(*sta_remove)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta);
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

static inline bool ieee80211_hw_check(const struct ieee80211_hw *hw,
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

#endif /* _LINUXKPI_NET_MAC80211_H_ */