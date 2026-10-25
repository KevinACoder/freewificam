// SPDX-License-Identifier: GPL-2.0-only
/*
 * @file   obs.c
 * @brief  Read-only snapshot of the vendor driver's queue gauges for the RTOS
 *         observation image.
 *
 * The vendor driver's SDIO probe stores its dvobj on the function device; the
 * engine keeps that object reachable through lkpi_sdio_func(). No vendor line
 * is edited: this file is compiled in the vendor world and only loads scalars.
 *
 * Only the WLAN_OBS image links this unit. The delivery image carries no
 * observation code.
 */

#include <drv_types.h>
#include <hal_data.h>

#include "diag.h"

/* the engine's function accessor; declared here so this unit does not pull the
 * linuxkpi header world into the vendor translation unit */
struct sdio_func *lkpi_sdio_func(unsigned int num);

void
rtl8189f_diag_get(struct rtl8189f_diag *out)
{
	struct sdio_func *func;
	struct dvobj_priv *dvobj;
	_adapter *adapter;
	HAL_DATA_TYPE *hal;

	if (out == NULL)
		return;
	out->valid = 0u;

	func = lkpi_sdio_func(1u);
	if (func == NULL)
		return;
	dvobj = (struct dvobj_priv *)sdio_get_drvdata(func);
	if (dvobj == NULL)
		return;
	adapter = dvobj_get_primary_adapter(dvobj);
	if (adapter == NULL)
		return;
	hal = GET_HAL_DATA(adapter);
	if (hal == NULL)
		return;

	out->recv_free = (unsigned int)adapter->recvpriv.free_recvframe_cnt;
	out->recvbuf_free = (unsigned int)adapter->recvpriv.free_recv_buf_queue_cnt;
	out->rx_fifo = (unsigned int)hal->SdioRxFIFOSize;
	out->txbuf_free = (unsigned int)adapter->xmitpriv.free_xmitbuf_cnt;
	out->txframe_free = (unsigned int)adapter->xmitpriv.free_xmitframe_cnt;
	out->tx_oqt_free = (unsigned int)hal->SdioTxOQTFreeSpace;
	out->tx_pg_hi = (unsigned int)hal->SdioTxFIFOFreePage[0];
	out->tx_pg_mid = (unsigned int)hal->SdioTxFIFOFreePage[1];
	out->tx_pg_lo = (unsigned int)hal->SdioTxFIFOFreePage[2];
	out->tx_pg_pub = (unsigned int)hal->SdioTxFIFOFreePage[3];
	out->cmd_issued = (unsigned int)adapter->cmdpriv.cmd_issued_cnt;
	out->cmd_done = (unsigned int)adapter->cmdpriv.cmd_done_cnt;
	out->valid = 1u;
}
