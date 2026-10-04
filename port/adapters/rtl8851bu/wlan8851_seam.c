/*
 * @file   wlan8851_seam.c
 * @brief  The indication seam for the rtl8851bu vendor line (S5b W1).
 *
 * ioctl_linux.c would define these three on a Linux build; that unit is
 * not in the RTOS compile set, and mlme_linux.c's externs (:54-55/:88)
 * need a first-party provider - the sibling-line shape (wlan8821_seam.c).
 * W1 semantics = console stamps; the W4 wpa wrapper and the W5 lwip
 * bridge turn them into event sources in later rounds (they mount here
 * exactly where the 8821 seam carries the #ifdef'd hooks).
 *
 * The PROC_DEBUG-off link face is the [S5a] W1 accounting carried over:
 * the debug helpers are defined in core/rtw_debug.c behind
 * CONFIG_PROC_DEBUG (:806-:7210), but their callers in rtw_mlme_ext.c /
 * rtw_cmd.c are unguarded - the stubs preserve the default behavior (no
 * STA-linking test armed: wait_done true, force_fail false).  W0-B kept
 * PROC_DEBUG ON (kbuild default); the RTOS build goes without procfs and
 * without rtw_proc.c, so the stubs come from this first-party tier.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <drv_types.h>

#include "rtl8851bu_port.h"

#ifdef CONFIG_WLAN_NETIF
/* port/adapters/rtl8851bu/wlan8851_netif.c: lwip bridge lifecycle */
extern void wlan8851_wlan_netif_on_assoc(void);
extern void wlan8851_wlan_netif_on_disassoc(void);
#endif
#ifdef CONFIG_NIC_WPA_BACKEND
/* rtl8851bu_wpa_event: declared in rtl8851bu_port.h (event pump queues
 * only - the seam runs in driver/USB worker contexts, [S7a] FR-7) */
#endif

void
rtw_indicate_wx_assoc_event(_adapter *padapter)
{
	(void)padapter;

	printf("rtl8851bu: associated\n");
#ifdef CONFIG_NIC_WPA_BACKEND
	rtl8851bu_wpa_event(2 /* WPA_VENDOR_EVENT_ASSOC */,
			    padapter->mlmepriv.dev_cur_network.network.MacAddress,
			    NULL, 0);
#endif
#ifdef CONFIG_WLAN_NETIF
	/* [S5b W4] mount the lwip bridge: netif up + dhcp_start in the tcpip
	 * thread (the sibling line's W5 discipline).  The MAC is efuse-read
	 * by this point. */
	wlan8851_wlan_netif_on_assoc();
#endif
}

void
rtw_indicate_wx_disassoc_event(_adapter *padapter)
{
	(void)padapter;

	printf("rtl8851bu: disassociated\n");
#ifdef CONFIG_NIC_WPA_BACKEND
	rtl8851bu_wpa_event(3 /* WPA_VENDOR_EVENT_DISASSOC */, NULL, NULL, 0);
#endif
#ifdef CONFIG_WLAN_NETIF
	wlan8851_wlan_netif_on_disassoc();
#endif
}

void
indicate_wx_scan_complete_event(_adapter *padapter)
{
	(void)padapter;

	printf("rtl8851bu: scan complete\n");
#ifdef CONFIG_NIC_WPA_BACKEND
	rtl8851bu_wpa_event(1 /* WPA_VENDOR_EVENT_SCAN_DONE */, NULL, NULL, 0);
#endif
}

/* ------------------------------------------------------------------ */
/* V5 note: the 8821 seam's PROC_DEBUG-off stub face (rtw_sta_linking_ */
/* test_* family) is NOT needed here - core/rtw_debug.c defines the    */
/* five helpers unconditionally in the V5 tree (rtw_debug.c:529-537),  */
/* no gate to bridge.                                                  */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* the ioctl_cfg80211.c seam ([W1] accounting): the vendor wrappers     */
/* below are defined in ioctl_cfg80211.c (not in the RTOS compile set)  */
/* but called from ungated core paths.  W1 semantics = no-op/false:     */
/* FT/CSA/ROC/P2P-action are connection-class or P2P-class events that  */
/* a down-mode STA never raises; the W4 wpa round revisits the assoc/   */
/* connect family when the supplicant backend lands.                    */
/* ------------------------------------------------------------------ */

/* ioctl_cfg80211.h:457 maps rtw_cfg80211_ft_event(adapter, parm) here */
void cfg80211_ft_event(struct net_device *netdev,
		       struct cfg80211_ft_event_params *params)
{
	(void)netdev;
	(void)params;
	printf("rtl8851bu: cfg80211_ft_event (seam no-op)\n");
}

bool rtw_cfg80211_get_is_roch(_adapter *adapter)
{
	(void)adapter;
	return false;
}

void rtw_cfg80211_indicate_sta_assoc(_adapter *padapter, u8 *pmgmt_frame,
				     uint frame_len)
{
	(void)padapter;
	(void)pmgmt_frame;
	(void)frame_len;
}

void rtw_cfg80211_rx_p2p_action_public(_adapter *padapter,
				       union recv_frame *rframe)
{
	(void)padapter;
	(void)rframe;
}

void rtw_cfg80211_rx_rrm_action(_adapter *adapter, union recv_frame *rframe)
{
	(void)adapter;
	(void)rframe;
}

u8 rtw_cfg80211_ch_switch_notify(_adapter *adapter,
				 struct _ADAPTER_LINK *alink,
				 struct rtw_chan_def *rtw_chdef,
				 u8 ht, bool started)
{
	(void)adapter;
	(void)alink;
	(void)rtw_chdef;
	(void)ht;
	(void)started;
	return _SUCCESS;
}

/* the BR_EXT seam (patches/rtl8821cu README W0L-M3 lineage): V5's
 * core_tx_update_pkt calls core_br_client_tx with no CONFIG_BR_EXT gate
 * while the definition sits inside it - with the bridge cut, every frame
 * is a pass-through (the NAT25 world never runs) */
s32 core_br_client_tx(_adapter *padapter, struct xmit_frame *pxframe,
		      struct sk_buff **pskb)
{
	(void)padapter;
	(void)pxframe;
	(void)pskb;
	return _SUCCESS;
}
