/*
 * @file
 * @brief First-party OS recv glue for the RTL8189FTV mac80211 front-end.
 *
 * The vendor's os_dep/linux/recv_linux.c is not linked (it is part of the
 * netdev/cfg80211 face). The chip recv path and core/rtw_recv.c still call
 * seven OS hooks; this file implements the SDIO subset with the same
 * semantics as the vendor: preallocate/copy an skb per recv frame, clone for
 * fragments, and free it on the way back.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <drv_types.h>
#include <rtw_recv.h>

int rtw_os_recv_resource_init(struct recv_priv *precvpriv, _adapter *padapter)
{
	return _SUCCESS;
}

int rtw_os_recv_resource_alloc(_adapter *padapter, union recv_frame *precvframe)
{
	precvframe->u.hdr.pkt = NULL;
	return _SUCCESS;
}

void rtw_os_recv_resource_free(struct recv_priv *precvpriv)
{
	union recv_frame *precvframe = (union recv_frame *)precvpriv->precv_frame_buf;
	sint i;

	for (i = 0; i < NR_RECVFRAME; i++) {
		rtw_os_free_recvframe(precvframe);
		precvframe++;
	}
}

int rtw_os_recvbuf_resource_alloc(_adapter *padapter, struct recv_buf *precvbuf)
{
	/* SDIO: the chip's init_recv_priv allocates the skb under
	 * CONFIG_SDIO_RX_COPY; the vendor hook is a no-op for this bus. */
	return _SUCCESS;
}

int rtw_os_recvbuf_resource_free(_adapter *padapter, struct recv_buf *precvbuf)
{
	if (precvbuf->pskb) {
		rtw_skb_free(precvbuf->pskb);
		precvbuf->pskb = NULL;
	}
	return _SUCCESS;
}

int rtw_os_alloc_recvframe(_adapter *padapter, union recv_frame *precvframe,
    u8 *pdata, _pkt *pskb)
{
	int res = _SUCCESS;
	u8 shift_sz = 0;
	u32 skb_len, alloc_sz;
	_pkt *pkt_copy = NULL;
	struct rx_pkt_attrib *pattrib = &precvframe->u.hdr.attrib;

	if (pdata == NULL) {
		precvframe->u.hdr.pkt = NULL;
		return _FAIL;
	}

	shift_sz = pattrib->qos ? 6 : 0;
	skb_len = pattrib->pkt_len;
	if ((pattrib->mfrag == 1) && (pattrib->frag_num == 0))
		alloc_sz = (skb_len <= 1650) ? 1664 : (skb_len + 14);
	else
		alloc_sz = skb_len + 14;

	pkt_copy = rtw_skb_alloc(alloc_sz);
	if (pkt_copy) {
		pkt_copy->dev = padapter->pnetdev;
		pkt_copy->len = skb_len;
		precvframe->u.hdr.pkt = pkt_copy;
		precvframe->u.hdr.rx_head = pkt_copy->head;
		precvframe->u.hdr.rx_end = pkt_copy->data + alloc_sz;
		skb_reserve(pkt_copy, 8 - ((SIZE_PTR)(pkt_copy->data) & 7));
		skb_reserve(pkt_copy, shift_sz);
		_rtw_memcpy(pkt_copy->data, pdata, skb_len);
		precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail =
		    pkt_copy->data;
	} else {
		if ((pattrib->mfrag == 1) && (pattrib->frag_num == 0)) {
			res = _FAIL;
			goto exit;
		}
		if (pskb == NULL) {
			res = _FAIL;
			goto exit;
		}
		precvframe->u.hdr.pkt = rtw_skb_clone(pskb);
		if (precvframe->u.hdr.pkt) {
			precvframe->u.hdr.pkt->dev = padapter->pnetdev;
			precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data =
			    precvframe->u.hdr.rx_tail = pdata;
			precvframe->u.hdr.rx_end = pdata + alloc_sz;
		} else {
			res = _FAIL;
		}
	}

exit:
	return res;
}

void rtw_os_free_recvframe(union recv_frame *precvframe)
{
	if (precvframe->u.hdr.pkt) {
		rtw_os_pkt_free(precvframe->u.hdr.pkt);
		precvframe->u.hdr.pkt = NULL;
	}
}