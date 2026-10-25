/*
 * @file
 * @brief Diagnostic stubs for the vendor MLME/OS symbols that the R2 keep-set
 *        still references. The real front-end replaces them one by one; the
 *        RX path now owns mgt_dispatcher, the HAL path owns init_hw_mlme_ext.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

	.text
	.globl	__rtl8189f_stub
	.type	__rtl8189f_stub, %function
__rtl8189f_stub:
	mov	x0, #0
	ret
	.size	__rtl8189f_stub, .-__rtl8189f_stub

	.globl	MPT_DeInitAdapter
	.set	MPT_DeInitAdapter, __rtl8189f_stub
	.globl	MPT_InitializeAdapter
	.set	MPT_InitializeAdapter, __rtl8189f_stub
	.globl	MPT_PwrCtlDM
	.set	MPT_PwrCtlDM, __rtl8189f_stub
	.globl	NULL_hdl
	.set	NULL_hdl, __rtl8189f_stub
	.globl	RTW_BUF_DUMP_SEL
	.set	RTW_BUF_DUMP_SEL, __rtl8189f_stub
	.globl	_linked_info_dump
	.set	_linked_info_dump, __rtl8189f_stub
	.globl	_rtw_alloc_network
	.set	_rtw_alloc_network, __rtl8189f_stub
	.globl	_rtw_find_network
	.set	_rtw_find_network, __rtl8189f_stub
	.globl	_rtw_open_pktfile
	.set	_rtw_open_pktfile, __rtl8189f_stub
	.globl	_rtw_pktfile_read
	.set	_rtw_pktfile_read, __rtl8189f_stub
	.globl	_update_beacon
	.set	_update_beacon, __rtl8189f_stub
	.globl	add_ba_hdl
	.set	add_ba_hdl, __rtl8189f_stub
	.globl	add_ba_rsp_hdl
	.set	add_ba_rsp_hdl, __rtl8189f_stub
	.globl	addba_timer_hdl
	.set	addba_timer_hdl, __rtl8189f_stub
	.globl	alloc_mgtxmitframe
	.set	alloc_mgtxmitframe, __rtl8189f_stub
	.globl	chk_bmc_sleepq_cmd
	.set	chk_bmc_sleepq_cmd, __rtl8189f_stub
	.globl	chk_bmc_sleepq_hdl
	.set	chk_bmc_sleepq_hdl, __rtl8189f_stub
	.globl	createbss_hdl
	.set	createbss_hdl, __rtl8189f_stub
	.globl	dhcp_flag_bcast
	.set	dhcp_flag_bcast, __rtl8189f_stub
	.globl	disconnect_hdl
	.set	disconnect_hdl, __rtl8189f_stub
	.globl	dump_mgntframe
	.set	dump_mgntframe, __rtl8189f_stub
	.globl	dump_mgntframe_and_wait
	.set	dump_mgntframe_and_wait, __rtl8189f_stub
	.globl	expire_timeout_chk
	.set	expire_timeout_chk, __rtl8189f_stub
	.globl	fill_txdesc_for_mp
	.set	fill_txdesc_for_mp, __rtl8189f_stub
	.globl	h2c_msg_hdl
	.set	h2c_msg_hdl, __rtl8189f_stub
	.globl	issue_action_SM_PS_wait_ack
	.set	issue_action_SM_PS_wait_ack, __rtl8189f_stub
	.globl	issue_deauth
	.set	issue_deauth, __rtl8189f_stub
	.globl	issue_nulldata
	.set	issue_nulldata, __rtl8189f_stub
	.globl	issue_qos_nulldata
	.set	issue_qos_nulldata, __rtl8189f_stub
	.globl	join_cmd_hdl
	.set	join_cmd_hdl, __rtl8189f_stub
	.globl	led_blink_hdl
	.set	led_blink_hdl, __rtl8189f_stub
	.globl	linked_status_chk
	.set	linked_status_chk, __rtl8189f_stub
	.globl	mlme_evt_hdl
	.set	mlme_evt_hdl, __rtl8189f_stub
	.globl	mlmeext_joinbss_event_callback
	.set	mlmeext_joinbss_event_callback, __rtl8189f_stub
	.globl	mp_start_test
	.set	mp_start_test, __rtl8189f_stub
	.globl	mp_stop_test
	.set	mp_stop_test, __rtl8189f_stub
	.globl	mpt_to_mgnt_rate
	.set	mpt_to_mgnt_rate, __rtl8189f_stub
	.globl	nat25_db_cleanup
	.set	nat25_db_cleanup, __rtl8189f_stub
	.globl	nat25_db_handle
	.set	nat25_db_handle, __rtl8189f_stub
	.globl	p2p_protocol_wk_hdl
	.set	p2p_protocol_wk_hdl, __rtl8189f_stub
	.globl	p2p_ps_wk_cmd
	.set	p2p_ps_wk_cmd, __rtl8189f_stub
	.globl	p2p_ps_wk_hdl
	.set	p2p_ps_wk_hdl, __rtl8189f_stub
	.globl	report_wmm_edca_update
	.set	report_wmm_edca_update, __rtl8189f_stub
	.globl	rm_post_event_hdl
	.set	rm_post_event_hdl, __rtl8189f_stub
	.globl	rtw_ap_restore_network
	.set	rtw_ap_restore_network, __rtl8189f_stub
	.globl	rtw_ap_update_sta_ra_info
	.set	rtw_ap_update_sta_ra_info, __rtl8189f_stub
	.globl	rtw_append_exented_cap
	.set	rtw_append_exented_cap, __rtl8189f_stub
	.globl	rtw_btcoex_connect_notify
	.set	rtw_btcoex_connect_notify, __rtl8189f_stub
	.globl	rtw_cancel_all_timer
	.set	rtw_cancel_all_timer, __rtl8189f_stub
	.globl	rtw_cfg80211_get_is_roch
	.set	rtw_cfg80211_get_is_roch, __rtl8189f_stub
	.globl	rtw_cfg80211_get_last_ro_ch_passing_ms
	.set	rtw_cfg80211_get_last_ro_ch_passing_ms, __rtl8189f_stub
	.globl	rtw_cfg80211_iface_has_p2p_group_cap
	.set	rtw_cfg80211_iface_has_p2p_group_cap, __rtl8189f_stub
	.globl	rtw_cfg80211_is_ro_ch_once
	.set	rtw_cfg80211_is_ro_ch_once, __rtl8189f_stub
	.globl	rtw_cfg80211_pwr_mgmt
	.set	rtw_cfg80211_pwr_mgmt, __rtl8189f_stub
	.globl	rtw_chset_is_chbw_valid
	.set	rtw_chset_is_chbw_valid, __rtl8189f_stub
	.globl	rtw_endofpktfile
	.set	rtw_endofpktfile, __rtl8189f_stub
	.globl	rtw_free_assoc_resources
	.set	rtw_free_assoc_resources, __rtl8189f_stub
	.globl	rtw_free_network_queue
	.set	rtw_free_network_queue, __rtl8189f_stub
	.globl	rtw_fwdl_test_trigger_chksum_fail
	.set	rtw_fwdl_test_trigger_chksum_fail, __rtl8189f_stub
	.globl	rtw_fwdl_test_trigger_wintint_rdy_fail
	.set	rtw_fwdl_test_trigger_wintint_rdy_fail, __rtl8189f_stub
	.globl	rtw_get_beacon_interval_from_ie
	.set	rtw_get_beacon_interval_from_ie, __rtl8189f_stub
	.globl	rtw_get_capability
	.set	rtw_get_capability, __rtl8189f_stub
	.globl	rtw_get_capability_from_ie
	.set	rtw_get_capability_from_ie, __rtl8189f_stub
	.globl	rtw_get_iface_by_macddr
	.set	rtw_get_iface_by_macddr, __rtl8189f_stub
	.globl	rtw_get_oldest_wlan_network
	.set	rtw_get_oldest_wlan_network, __rtl8189f_stub
	.globl	rtw_get_wait_hiq_empty_ms
	.set	rtw_get_wait_hiq_empty_ms, __rtl8189f_stub
	.globl	rtw_getmacreg_hdl
	.set	rtw_getmacreg_hdl, __rtl8189f_stub
	.globl	rtw_handle_tkip_mic_err
	.set	rtw_handle_tkip_mic_err, __rtl8189f_stub
	.globl	rtw_ht_operation_update
	.set	rtw_ht_operation_update, __rtl8189f_stub
	.globl	rtw_ht_use_default_setting
	.set	rtw_ht_use_default_setting, __rtl8189f_stub
	.globl	rtw_init_netdev
	.set	rtw_init_netdev, __rtl8189f_stub
	.globl	rtw_init_netdev_name
	.set	rtw_init_netdev_name, __rtl8189f_stub
	.globl	rtw_intf_start
	.set	rtw_intf_start, __rtl8189f_stub
	.globl	rtw_intf_stop
	.set	rtw_intf_stop, __rtl8189f_stub
	.globl	rtw_ips_pwr_down
	.set	rtw_ips_pwr_down, __rtl8189f_stub
	.globl	rtw_ips_pwr_up
	.set	rtw_ips_pwr_up, __rtl8189f_stub
	.globl	rtw_is_adapter_up
	.set	rtw_is_adapter_up, __rtl8189f_stub
	.globl	rtw_is_scan_deny
	.set	rtw_is_scan_deny, __rtl8189f_stub
	.globl	rtw_issue_addbareq_cmd
	.set	rtw_issue_addbareq_cmd, __rtl8189f_stub
	.globl	rtw_join_timeout_handler
	.set	rtw_join_timeout_handler, __rtl8189f_stub
	.globl	rtw_linked_check
	.set	rtw_linked_check, __rtl8189f_stub
	.globl	rtw_macaddr_acl_deinit
	.set	rtw_macaddr_acl_deinit, __rtl8189f_stub
	.globl	rtw_macaddr_acl_init
	.set	rtw_macaddr_acl_init, __rtl8189f_stub
	.globl	rtw_mgnt_tx_handler
	.set	rtw_mgnt_tx_handler, __rtl8189f_stub
	.globl	rtw_mi_buddy_check_fwstate
	.set	rtw_mi_buddy_check_fwstate, __rtl8189f_stub
	.globl	rtw_mi_buddy_check_mlmeinfo_state
	.set	rtw_mi_buddy_check_mlmeinfo_state, __rtl8189f_stub
	.globl	rtw_mi_buddy_clone_bcmc_packet
	.set	rtw_mi_buddy_clone_bcmc_packet, __rtl8189f_stub
	.globl	rtw_mi_check_fwstate
	.set	rtw_mi_check_fwstate, __rtl8189f_stub
	.globl	rtw_mi_check_pending_xmitbuf
	.set	rtw_mi_check_pending_xmitbuf, __rtl8189f_stub
	.globl	rtw_mi_check_status
	.set	rtw_mi_check_status, __rtl8189f_stub
	.globl	rtw_mi_dequeue_writeport
	.set	rtw_mi_dequeue_writeport, __rtl8189f_stub
	.globl	rtw_mi_dynamic_chk_wk_hdl
	.set	rtw_mi_dynamic_chk_wk_hdl, __rtl8189f_stub
	.globl	rtw_mi_get_assoc_if_num
	.set	rtw_mi_get_assoc_if_num, __rtl8189f_stub
	.globl	rtw_mi_scan_abort
	.set	rtw_mi_scan_abort, __rtl8189f_stub
	.globl	rtw_mi_set_scan_deny
	.set	rtw_mi_set_scan_deny, __rtl8189f_stub
	.globl	rtw_mi_status_merge
	.set	rtw_mi_status_merge, __rtl8189f_stub
	.globl	rtw_mi_status_no_others
	.set	rtw_mi_status_no_others, __rtl8189f_stub
	.globl	rtw_mi_status_no_self
	.set	rtw_mi_status_no_self, __rtl8189f_stub
	.globl	rtw_mi_stayin_union_ch_chk
	.set	rtw_mi_stayin_union_ch_chk, __rtl8189f_stub
	.globl	rtw_mi_tx_beacon_hdl
	.set	rtw_mi_tx_beacon_hdl, __rtl8189f_stub
	.globl	rtw_mi_update_ap_bmc_camid
	.set	rtw_mi_update_ap_bmc_camid, __rtl8189f_stub
	.globl	rtw_mi_update_iface_status
	.set	rtw_mi_update_iface_status, __rtl8189f_stub
	.globl	rtw_mp_mode_check
	.set	rtw_mp_mode_check, __rtl8189f_stub
	.globl	rtw_os_alloc_msdu_pkt
	.set	rtw_os_alloc_msdu_pkt, __rtl8189f_stub
	.globl	rtw_os_alloc_recvframe
	.globl	rtw_os_free_recvframe
	.globl	rtw_os_recv_indicate_pkt
	.set	rtw_os_recv_indicate_pkt, __rtl8189f_stub
	.globl	rtw_os_recv_resource_alloc
	.globl	rtw_os_recv_resource_free
	.globl	rtw_os_recv_resource_init
	.globl	rtw_os_recvbuf_resource_alloc
	.globl	rtw_os_recvbuf_resource_free
	.globl	rtw_os_wake_queue_at_free_stainfo
	.set	rtw_os_wake_queue_at_free_stainfo, __rtl8189f_stub
	.globl	rtw_recv_indicatepkt
	.set	rtw_recv_indicatepkt, __rtl8189f_stub
	.globl	rtw_recv_monitor
	.set	rtw_recv_monitor, __rtl8189f_stub
	.globl	rtw_reset_drv_sw
	.set	rtw_reset_drv_sw, __rtl8189f_stub
	.globl	rtw_reset_securitypriv
	.set	rtw_reset_securitypriv, __rtl8189f_stub
	.globl	rtw_restruct_sec_ie
	.set	rtw_restruct_sec_ie, __rtl8189f_stub
	.globl	rtw_restruct_wmm_ie
	.set	rtw_restruct_wmm_ie, __rtl8189f_stub
	.globl	rtw_restructure_ht_ie
	.set	rtw_restructure_ht_ie, __rtl8189f_stub
	.globl	rtw_rframe_set_os_pkt
	.set	rtw_rframe_set_os_pkt, __rtl8189f_stub
	.globl	rtw_rtnl_lock_needed
	.set	rtw_rtnl_lock_needed, __rtl8189f_stub
	.globl	rtw_rx_ampdu_is_accept
	.set	rtw_rx_ampdu_is_accept, __rtl8189f_stub
	.globl	rtw_rx_ampdu_size
	.set	rtw_rx_ampdu_size, __rtl8189f_stub
	.globl	rtw_scan_abort
	.set	rtw_scan_abort, __rtl8189f_stub
	.globl	rtw_scan_timeout_decision
	.set	rtw_scan_timeout_decision, __rtl8189f_stub
	.globl	rtw_set_chbw_hdl
	.set	rtw_set_chbw_hdl, __rtl8189f_stub
	.globl	rtw_set_key
	.set	rtw_set_key, __rtl8189f_stub
	.globl	rtw_set_rx_chk_limit
	.set	rtw_set_rx_chk_limit, __rtl8189f_stub
	.globl	rtw_set_to_roam
	.set	rtw_set_to_roam, __rtl8189f_stub
	.globl	rtw_set_tx_chksum_offload
	.set	rtw_set_tx_chksum_offload, __rtl8189f_stub
	.globl	rtw_sink_rtp_seq_dbg
	.set	rtw_sink_rtp_seq_dbg, __rtl8189f_stub
	.globl	rtw_sta_media_status_rpt_cmd
	.set	rtw_sta_media_status_rpt_cmd, __rtl8189f_stub
	.globl	rtw_sta_media_status_rpt_cmd_hdl
	.set	rtw_sta_media_status_rpt_cmd_hdl, __rtl8189f_stub
	.globl	rtw_txpwr_init_regd
	.set	rtw_txpwr_init_regd, __rtl8189f_stub
	.globl	run_in_thread_hdl
	.set	run_in_thread_hdl, __rtl8189f_stub
	.globl	rx_ampdu_size_sta_limit
	.set	rx_ampdu_size_sta_limit, __rtl8189f_stub
	.globl	scdb_findEntry
	.set	scdb_findEntry, __rtl8189f_stub
	.globl	set_chplan_hdl
	.set	set_chplan_hdl, __rtl8189f_stub
	.globl	set_csa_hdl
	.set	set_csa_hdl, __rtl8189f_stub
	.globl	set_stakey_hdl
	.set	set_stakey_hdl, __rtl8189f_stub
	.globl	setauth_hdl
	.set	setauth_hdl, __rtl8189f_stub
	.globl	setkey_hdl
	.set	setkey_hdl, __rtl8189f_stub
	.globl	sitesurvey_cmd_hdl
	.set	sitesurvey_cmd_hdl, __rtl8189f_stub
	.globl	stop_ap_hdl
	.set	stop_ap_hdl, __rtl8189f_stub
	.globl	tdls_hdl
	.set	tdls_hdl, __rtl8189f_stub
	.globl	tx_beacon_hdl
	.set	tx_beacon_hdl, __rtl8189f_stub
	.globl	update_mgnt_tx_rate
	.set	update_mgnt_tx_rate, __rtl8189f_stub
	.globl	update_mgntframe_attrib
	.set	update_mgntframe_attrib, __rtl8189f_stub
	.globl	update_mgntframe_attrib_addr
	.set	update_mgntframe_attrib_addr, __rtl8189f_stub
	.globl	update_monitor_frame_attrib
	.set	update_monitor_frame_attrib, __rtl8189f_stub
