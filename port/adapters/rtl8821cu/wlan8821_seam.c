/*
 * @file   wlan8821_seam.c
 * @brief  The indication seam for the rtl8821cu vendor line (S5a W1, spec
 *         §3.5 冒烟级 v2 裁决).
 *
 * ioctl_linux.c would define these three on a Linux build; that unit is
 * not in the RTOS compile set, and mlme_linux.c's externs (:59-60/:87)
 * need a first-party provider.  W1 semantics = console stamps; the W4
 * wpa wrapper turns them into supplicant event sources, and the W5 lwip
 * bridge mounts its netif_rx sink in the same tier (the default
 * counted-and-drop sink inside linuxkpi_core.c needs no install here).
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <drv_types.h>

#include "rtl8821cu_port.h"

/* W4: the indications are the supplicant's event sources now - the pump
 * (rtl8821cu_wpa_event) deep-copies into the port's queue, so printing
 * stays as the console trace of the same transitions.  Driver/worker
 * contexts: queue only, never supplicant calls ([S7a] FR-7). */

void
rtw_indicate_wx_assoc_event(_adapter *padapter)
{

	printf("rtl8821cu: associated\n");
#ifdef CONFIG_NIC_WPA_BACKEND
	rtl8821cu_wpa_event(2 /* WPA_VENDOR_EVENT_ASSOC */,
			    padapter->mlmepriv.cur_network.network.MacAddress,
			    NULL, 0);
#endif
}

void
rtw_indicate_wx_disassoc_event(_adapter *padapter)
{
	(void)padapter;

	printf("rtl8821cu: disassociated\n");
#ifdef CONFIG_NIC_WPA_BACKEND
	rtl8821cu_wpa_event(3 /* WPA_VENDOR_EVENT_DISASSOC */, NULL, NULL, 0);
#endif
}

void
indicate_wx_scan_complete_event(_adapter *padapter)
{
	(void)padapter;

	printf("rtl8821cu: scan complete\n");
#ifdef CONFIG_NIC_WPA_BACKEND
	rtl8821cu_wpa_event(1 /* WPA_VENDOR_EVENT_SCAN_DONE */, NULL, NULL, 0);
#endif
}

/* ------------------------------------------------------------------ */
/* the data-plane counters (the seam tier owns the rx accounting; the   */
/* frame sink itself is netif_rx's default counted-and-drop)            */

static unsigned long rtl8821cu_rx_frames;
static unsigned long rtl8821cu_rx_bytes;

unsigned long
rtl8821cu_rx_stats(unsigned long *bytes)
{

	if (bytes != NULL)
		*bytes = rtl8821cu_rx_bytes;
	return rtl8821cu_rx_frames;
}

/* W5 mounts the lwip bridge here: the registered sink replaces the
 * default, counters move to the bridge.  Kept as the seam's extension
 * point so the W1 judgement can nm it. */
void
rtl8821cu_rx_account(unsigned long frames_delta, unsigned long bytes_delta)
{

	rtl8821cu_rx_frames += frames_delta;
	rtl8821cu_rx_bytes += bytes_delta;
}

/* ------------------------------------------------------------------ */
/* the PROC_DEBUG-off link face ([W1-0001] accounting): the debug       */
/* helpers below are defined in core/rtw_debug.c behind                 */
/* CONFIG_PROC_DEBUG, but their callers in rtw_mlme_ext.c are           */
/* unguarded - the stubs preserve the default behavior (no STA-linking  */
/* test armed: wait_done true, force_fail false).  W0-L kept PROC_DEBUG */
/* ON instead; the RTOS build goes without procfs, so the stubs come    */
/* from this first-party tier like the indication seam.                 */
/* ------------------------------------------------------------------ */

void rtw_sta_linking_test_set_start(void)
{
}

bool rtw_sta_linking_test_wait_done(void)
{
	return true;
}

bool rtw_sta_linking_test_force_fail(void)
{
	return false;
}

u32 rtw_get_wait_hiq_empty_ms(void)
{
	return 0;
}

bool rtw_del_rx_ampdu_test_trigger_no_tx_fail(void)
{
	return false;
}
