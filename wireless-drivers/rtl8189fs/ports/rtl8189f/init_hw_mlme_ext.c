/*
 * @file
 * @brief First-party init_hw_mlme_ext() for the HAL-only bring-up.
 *
 * The vendor's rtw_hal_init() calls init_hw_mlme_ext() after the chip's
 * hal_init; the vendor body (core/rtw_mlme_ext.c:994) does three things and
 * nothing else: reset the HAL's current channel/band bookkeeping, enable the
 * RX BAR register, and program the initial channel. The vendor MLME state
 * machine is not needed for that, so this translation unit provides the same
 * symbol and the vendor MLME object stays out of the link.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <drv_types.h>
#include <hal_data.h>

int init_hw_mlme_ext(_adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	u8 rx_bar_enble = _TRUE;

	pHalData->current_channel = 0;
	pHalData->current_channel_bw = CHANNEL_WIDTH_MAX;
#ifdef CONFIG_IEEE80211_BAND_5GHZ
	pHalData->current_band_type = BAND_MAX;
#else
	pHalData->current_band_type = BAND_ON_2_4G;
#endif

	rtw_hal_set_hwreg(padapter, HW_VAR_ENABLE_RX_BAR, &rx_bar_enble);
	set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset,
	    pmlmeext->cur_bwmode);
	return _SUCCESS;
}