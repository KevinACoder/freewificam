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
	unsigned int valid;		/* driver/adapter present */
};

void rtl8189f_diag_get(struct rtl8189f_diag *out);

#endif /* RTL8189F_RTOS_DIAG_H */
