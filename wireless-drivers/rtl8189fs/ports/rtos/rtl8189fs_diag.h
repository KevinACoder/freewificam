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
