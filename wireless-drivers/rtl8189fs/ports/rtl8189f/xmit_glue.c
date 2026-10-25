// SPDX-License-Identifier: GPL-2.0-only
/*
 * First-party OS transmit glue for the RTL8189FTV mac80211 front-end.
 *
 * The vendor's os_dep/linux/xmit_linux.c is not linked (it is part of the
 * netdev face). The chip transmit path still calls four OS hooks; this file
 * implements the SDIO subset with the same semantics: one contiguous buffer
 * per transmit buffer, freed again on the way back, and the frame's packet
 * released when the HAL reports the transfer complete.
 */

#include <drv_types.h>
#include <rtw_xmit.h>
#include <xmit_osdep.h>

int rtw_os_xmit_resource_alloc(_adapter *padapter, struct xmit_buf *pxmitbuf,
    u32 alloc_sz, u8 flag)
{
	if (alloc_sz > 0) {
		pxmitbuf->pallocated_buf = rtw_zmalloc(alloc_sz);
		if (pxmitbuf->pallocated_buf == NULL)
			return _FAIL;
		pxmitbuf->pbuf = (u8 *)N_BYTE_ALIGMENT(
		    (SIZE_PTR)pxmitbuf->pallocated_buf, XMITBUF_ALIGN_SZ);
	}
	return _SUCCESS;
}

void rtw_os_xmit_resource_free(_adapter *padapter, struct xmit_buf *pxmitbuf,
    u32 free_sz, u8 flag)
{
	if (free_sz > 0 && pxmitbuf->pallocated_buf != NULL) {
		rtw_mfree(pxmitbuf->pallocated_buf, free_sz);
		pxmitbuf->pallocated_buf = NULL;
		pxmitbuf->pbuf = NULL;
	}
}

void rtw_os_pkt_complete(_adapter *padapter, _pkt *pkt)
{
	rtw_skb_free(pkt);
}

void rtw_os_xmit_complete(_adapter *padapter, struct xmit_frame *pxframe)
{
	if (pxframe->pkt != NULL)
		rtw_os_pkt_complete(padapter, pxframe->pkt);
	pxframe->pkt = NULL;
}
