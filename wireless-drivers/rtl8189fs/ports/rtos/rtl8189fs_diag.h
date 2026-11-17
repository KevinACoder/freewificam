/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * @file   diag.h
 * @brief  Plain-C snapshot of the vendor driver's own queue gauges.
 *
 * The RTOS build links the vendor driver's Linux-facing translation units, but
 * the observation case lives outside that world. This header is the whole
 * crossing: scalars only, no vendor type, no lwIP type.
 */

#ifndef RTL8189F_RTOS_DIAG_H
#define RTL8189F_RTOS_DIAG_H

struct rtl8189f_diag {
	unsigned int recv_free;		/* free_recvframe_cnt */
	unsigned int recvbuf_free;	/* free_recv_buf_queue_cnt */
	unsigned int rx_fifo;		/* SdioRxFIFOSize, bytes */
	unsigned int txbuf_free;	/* free_xmitbuf_cnt */
	unsigned int txframe_free;	/* free_xmitframe_cnt */
	unsigned int tx_oqt_free;	/* SdioTxOQTFreeSpace */
	unsigned int tx_pg_hi;		/* SdioTxFIFOFreePage[0] */
	unsigned int tx_pg_mid;		/* [1] */
	unsigned int tx_pg_lo;		/* [2] */
	unsigned int tx_pg_pub;		/* [3] */
	unsigned int cmd_issued;
	unsigned int cmd_done;
	/* The vendor's own receive account.  A frame the card accepted but the
	 * driver dropped leaves no trace in the egress seam's counters, so these
	 * are the layer between the bus and the netdev (docs/HANDOFF-8189FTV-
	 * STA-RX.md: "does the board receive the AP's broadcast ARP request").
	 * The `dbg_*` fields are the vendor's unconditional drop counters; the
	 * `rx_logs` block is compiled out of this face (CONFIG_DBG_COUNTER off),
	 * which is why it is not surfaced here. */
	unsigned int rx_pkts;
	unsigned int rx_bytes;
	unsigned int rx_drops;		/* recvpriv.rx_drop */
	unsigned int rx_dbg_drop;	/* dbg_rx_drop_count */
	unsigned int rx_dbg_ampdu_drop;	/* dbg_rx_ampdu_drop_count */
	unsigned int rx_icv_err;	/* hardware decryption failures */
	unsigned int rx_crc_err;	/* large+middle+small packet CRC */
	/* The vendor's receive-stage counters (CONFIG_DBG_COUNTER, diagnostic
	 * faces only).  One frame is counted at each check it must pass, so the
	 * counter that stops growing names the check the frame died at: the FIFO
	 * reads and these are the layer between the bus and the netdev.  All
	 * zero when the face is off. */
	unsigned int rx_stage_core;	/* frames entering recv_func */
	unsigned int rx_stage_pre;	/* recv_func_prehandle entered */
	unsigned int rx_stage_ver;	/* 802.11 version check refused */
	unsigned int rx_stage_mgmt;
	unsigned int rx_stage_ctrl;
	unsigned int rx_stage_data;
	unsigned int rx_stage_unknown;
	unsigned int rx_stage_post;	/* past prehandle, into decrypt/port */
	unsigned int rx_stage_indicate;	/* delivered to the net stack */
	unsigned int rx_stage_ind_err;	/* delivery refused */
	/* Where a join stopped.  The station case's wait only sees
	 * cfg80211's connected flag, so these read the vendor's own MLME: the
	 * scan state, the station state machine (0x200 auth, 0x2000 assoc),
	 * the join result and the auth sequence.  A join that never starts,
	 * one that starts and never authenticates, and one that authenticates
	 * and never associates are three different findings. */
	unsigned int fw_state;		/* mlmepriv.fw_state */
	unsigned int to_join;		/* mlmepriv.to_join */
	unsigned int mlme_state;	/* mlmext_info.state */
	unsigned int auth_seq;		/* mlmext_info.auth_seq */
	unsigned int join_res;		/* cur_network.join_res */
	unsigned int scan_state;	/* sitesurvey_res.state */
	unsigned int scan_ch_idx;	/* sitesurvey_res.channel_idx */
	/* The vendor's SDIO interrupt state: the last HISR the driver read and the
	 * mask it compares it against.  The receive path runs only when the two
	 * intersect, so "the card has bytes waiting" (rx_fifo) and "the driver
	 * serviced them" (rx_pkts) can disagree with both of these quiet. */
	unsigned int sdio_hisr;
	unsigned int sdio_himr;
	/* The two vendor timers that drive a scan and a join: whether each is
	 * still armed, and the expiry (in the engine's jiffies) it was armed for.
	 * A scan that walks channels and a join that never leaves "auth" are
	 * both decided by these two, so "armed and expired but never ran" and
	 * "never armed" have to be told apart from the board. */
	unsigned int survey_timer_pending;
	unsigned int survey_timer_expires;
	unsigned int link_timer_pending;
	unsigned int link_timer_expires;
	unsigned int jiffies_now;
	unsigned int tx_pkts;		/* dvobj traffic_stat, whole adapter */
	/* The SDIO transmit worker: the vendor hands every transmit to a
	 * kthread (RTWHALXT) that drains the pending xmit-buffer queue.  A null
	 * pointer means the worker was never created; frames would then queue
	 * and never reach the card.  txframe_free/txbuf_free above say whether
	 * one is parked there (a pending buffer is not free). */
	unsigned int xmit_thread;	/* xmitpriv.SdioXmitThread */
	/* The management-frame buffer pool and the pending write queue.  The
	 * vendor allocates management frames (probe requests, auth, assoc) from
	 * a pool separate from the data buffers; a pool of one, never returned,
	 * is "every management transmit fails silently". */
	unsigned int xmit_ext_free;	/* xmitpriv.free_xmit_extbuf_cnt */
	unsigned int xmit_pending;	/* a write is queued for the worker */
	/* The three flags that make the vendor's transmit helpers drop a frame
	 * without a log: a stopped driver, a surprise-removed device (the
	 * management-frame helper frees the buffer), and a HAL whose init never
	 * completed (the SDIO write port refuses before the transfer). */
	unsigned int drv_stopped;
	unsigned int surprise_removed;
	unsigned int hw_init_done;
	unsigned int valid;		/* driver/adapter present */
};

void rtl8189f_diag_get(struct rtl8189f_diag *out);

/* --- the AP role (docs/HANDOFF-8189FTV-RTOS-AP.md) -----------------------
 *
 * The AP's MLME is the vendor's own (autoconf.h defines CONFIG_NATIVEAP_MLME),
 * so the station table that matters is this one.  hostapd's table is not a
 * witness here: this image has no netlink socket, so the vendor's
 * cfg80211_new_sta() indication is a multicast into nothing (ADR
 * 2026-11-10-nl80211-scope, D1) and the daemon never learns of the station.
 *
 * The fields are the vendor record's, named after the GT arm's
 * /proc/net/rtl88XXau/<if>/all_sta_info columns (aid/macid/rate/ba) so the two
 * arms can be compared field by field.  Scalars only: no vendor type crosses.
 */
#define RTL8189F_AP_STA_MAX	4u

struct rtl8189f_ap_sta {
	unsigned char	mac[6];
	unsigned int	aid;
	unsigned int	mac_id;
	unsigned int	state;		/* vendor sta_info.state */
	unsigned int	flags;		/* vendor sta_info.flags */
	unsigned int	rate_id;	/* cmn.ra_info.rate_id */
	unsigned int	cur_rate;	/* cmn.ra_info.curr_tx_rate */
	unsigned int	cur_bw;		/* cmn.ra_info.curr_tx_bw */
	unsigned int	bw_mode;	/* cmn.bw_mode */
	unsigned int	sgi;		/* cmn.ra_info.is_support_sgi */
	unsigned int	rx_data_pkts;
	unsigned int	tx_pkts;
	unsigned int	tx_bytes;
};

struct rtl8189f_ap_diag {
	unsigned int	valid;
	unsigned int	fwstate;	/* mlmepriv.fw_state */
	unsigned int	ap_state;	/* check_fwstate(WIFI_AP_STATE) */
	unsigned int	bstart_bss;	/* mlmext_info.bstart_bss */
	unsigned int	cur_channel;
	unsigned int	bcn_interval;
	unsigned int	nr_ap_if;	/* the MULTI_BCN list depth */
	unsigned int	nr_sta;		/* associated, from asoc_list */
	unsigned int	tx_pkts;	/* dvobj traffic_stat, whole adapter */
	unsigned int	tx_bytes;
	unsigned int	rx_pkts;
	unsigned int	rx_bytes;
	struct rtl8189f_ap_sta sta[RTL8189F_AP_STA_MAX];
};

void rtl8189f_ap_diag_get(struct rtl8189f_ap_diag *out);

/* --- the security state a WPA2 station's receive path turns on ------------
 *
 * The group key's install is the vendor's own decision, and its cfg80211
 * add_key returns 0 even when it took no action (the "Obtain Sta_info fail"
 * path leaves ret at its initial 0).  A host-side rc=0 therefore says nothing
 * about whether the GTK reached the CAM; these are the vendor's own flags, so
 * a boot can say which link is missing instead of inferring it from frames
 * that arrive malformed (docs/HANDOFF-8189FTV-STA-RX.md).  Scalars only.
 */
struct rtl8189f_sec_diag {
	unsigned int valid;
	unsigned int binstall_grpkey;	/* securitypriv.binstallGrpkey */
	unsigned int grp_keyid;		/* securitypriv.dot118021XGrpKeyid */
	unsigned int grp_privacy;	/* securitypriv.dot118021XGrpPrivacy */
	unsigned int ndis_authtype;	/* securitypriv.ndisauthtype */
	unsigned int ndis_encrypt;	/* securitypriv.ndisencryptstatus */
	unsigned int hw_decrypted;	/* securitypriv.hw_decrypted (last frame) */
	unsigned int ap_sta_present;	/* rtw_get_stainfo(get_bssid()) != NULL */
	unsigned int ap_sta_pairwise;	/* psta->bpairwise_key_installed */
	unsigned int ap_sta_privacy;	/* psta->dot118021XPrivacy */
};

void rtl8189f_sec_diag_get(struct rtl8189f_sec_diag *out);

/* The station group-key fixup (see obs.c): the vendor's station group-key
 * install leaves dot118021XGrpPrivacy unset, which makes the receive path
 * treat encrypted group frames as plaintext.  Called from the port's set_key
 * after a group key is installed; defined only in the 8189 vendor world (the
 * port reaches it through a weak reference), so other drivers link unchanged.
 */
void rtl8189f_grp_privacy_fixup(void);

#endif /* RTL8189F_RTOS_DIAG_H */
