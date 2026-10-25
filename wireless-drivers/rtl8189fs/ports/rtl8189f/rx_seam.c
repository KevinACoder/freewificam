/*
 * @file
 * @brief Vendor-world RX seam: mgt_dispatcher -> mac80211.
 *
 * The vendor recv core (core/rtw_recv.c) calls mgt_dispatcher() for every
 * management frame after validate_recv_mgnt_frame(). This file replaces the
 * vendor MLME dispatcher with a copy into the mac80211 world; the kernel side
 * only sees bytes and a void *hw, so the vendor 802.11 headers and the kernel
 * mac80211 headers stay in separate translation units.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <drv_types.h>
#include <rtw_recv.h>

extern void *rtl8189f_mac80211_hw(void);
extern void rtl8189f_rx_mgmt(void *hw, const u8 *data, int len);

static unsigned long rtl8189f_mgt_count;

unsigned long rtl8189f_mgt_count_get(void)
{
	return rtl8189f_mgt_count;
}

/*
 * The receive filter the vendor's dispatcher applies (rtw_mlme_ext.c:1302):
 * only frames addressed to our MAC or to the broadcast address are delivered.
 * mac80211 makes its own decisions, but the filter keeps the seam's counter and
 * the stack's stats honest about what reached us.
 *
 * The frame is NOT freed here: validate_recv_frame() returns _FAIL for every
 * management frame, so recv_func_prehandle() frees it right after this returns.
 * The vendor's dispatcher does not free either; freeing here was a double free.
 */
void mgt_dispatcher(_adapter *padapter, union recv_frame *precv_frame)
{
	static const u8 bc_addr[ETH_ALEN] =
	    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	const u8 *a1;
	void *hw;

	if (precv_frame == NULL)
		return;
	a1 = GetAddr1Ptr(precv_frame->u.hdr.rx_data);
	if (!_rtw_memcmp(a1, adapter_mac_addr(padapter), ETH_ALEN) &&
	    !_rtw_memcmp(a1, bc_addr, ETH_ALEN))
		return;
	rtl8189f_mgt_count++;
	hw = rtl8189f_mac80211_hw();
	if (hw != NULL)
		rtl8189f_rx_mgmt(hw, precv_frame->u.hdr.rx_data,
		    precv_frame->u.hdr.len);
}
