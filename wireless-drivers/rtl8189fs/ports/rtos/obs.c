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

#include "rtl8189fs_diag.h"

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

/* --- the AP role ---------------------------------------------------------
 *
 * The AP's MLME is the vendor's, so the station table, the BSS state and the
 * beacon interval are read from the vendor's own records.  The walk is a
 * read-only snapshot under the asoc_list lock, the same lock the AP's assoc
 * handler takes when it inserts a station.
 */
void
rtl8189f_ap_diag_get(struct rtl8189f_ap_diag *out)
{
	struct sdio_func *func;
	struct dvobj_priv *dvobj;
	_adapter *adapter;
	struct mlme_priv *pmlmepriv;
	struct mlme_ext_priv *pmlmeext;
	struct sta_priv *pstapriv;
	_irqL irqL;

	if (out == NULL)
		return;
	_rtw_memset(out, 0, sizeof(*out));

	func = lkpi_sdio_func(1u);
	if (func == NULL)
		return;
	dvobj = (struct dvobj_priv *)sdio_get_drvdata(func);
	if (dvobj == NULL)
		return;
	adapter = dvobj_get_primary_adapter(dvobj);
	if (adapter == NULL)
		return;

	pmlmepriv = &adapter->mlmepriv;
	pmlmeext = &adapter->mlmeextpriv;
	pstapriv = &adapter->stapriv;

	out->fwstate = (unsigned int)pmlmepriv->fw_state;
	out->ap_state = check_fwstate(pmlmepriv, WIFI_AP_STATE) ? 1u : 0u;
	out->bstart_bss = (unsigned int)pmlmeext->bstart_bss;
	out->cur_channel = (unsigned int)pmlmeext->cur_channel;
	/* The AP's own BSS record is where the beacon period lives: mlmext_info's
	 * copy is the station-side field and stays 0 in the AP role. */
	out->bcn_interval =
	    (unsigned int)pmlmepriv->cur_network.network.Configuration.BeaconPeriod;
#ifdef CONFIG_SUPPORT_MULTI_BCN
	out->nr_ap_if = dvobj->nr_ap_if;
#else
	out->nr_ap_if = out->bstart_bss ? 1u : 0u;
#endif
	out->tx_pkts = (unsigned int)dvobj->traffic_stat.tx_pkts;
	out->tx_bytes = (unsigned int)dvobj->traffic_stat.tx_bytes;
	out->rx_pkts = (unsigned int)dvobj->traffic_stat.rx_pkts;
	out->rx_bytes = (unsigned int)dvobj->traffic_stat.rx_bytes;

	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	{
		_list *phead = &pstapriv->asoc_list;
		_list *plist = get_next(phead);
		unsigned int n = 0u;

		while (rtw_end_of_queue_search(phead, plist) == _FALSE) {
			struct sta_info *psta =
			    LIST_CONTAINOR(plist, struct sta_info, asoc_list);

			if (n < RTL8189F_AP_STA_MAX) {
				struct rtl8189f_ap_sta *s = &out->sta[n];

				_rtw_memcpy(s->mac, psta->cmn.mac_addr, 6);
				s->aid = (unsigned int)psta->cmn.aid;
				s->mac_id = (unsigned int)psta->cmn.mac_id;
				s->state = (unsigned int)psta->state;
				s->flags = (unsigned int)psta->flags;
				s->rate_id = (unsigned int)psta->cmn.ra_info.rate_id;
				s->cur_rate =
				    (unsigned int)psta->cmn.ra_info.curr_tx_rate;
				s->cur_bw =
				    (unsigned int)psta->cmn.ra_info.curr_tx_bw;
				s->bw_mode = (unsigned int)psta->cmn.bw_mode;
				s->sgi = (unsigned int)
				    psta->cmn.ra_info.is_support_sgi;
				s->rx_data_pkts =
				    (unsigned int)psta->sta_stats.rx_data_pkts;
				s->tx_pkts =
				    (unsigned int)psta->sta_stats.tx_pkts;
				s->tx_bytes =
				    (unsigned int)psta->sta_stats.tx_bytes;
			}
			n++;
			plist = get_next(plist);
		}
		out->nr_sta = n;
	}
	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	out->valid = 1u;
}
