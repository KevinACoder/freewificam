// SPDX-License-Identifier: GPL-2.0
/*
 * The one MLME command handler the front end needs so far.
 *
 * The vendor's rtw_mlme_ext.c is not part of this build; every command handler
 * it exports is stubbed. rtw_hal_init() ends with rtw_hal_init_opmode(), which
 * sends the opmode command directly (RTW_CMDF_DIRECTLY), so the stub meant the
 * firmware was never told it is a station. This implementation is the vendor's
 * setopmode_hdl() reduced to that effect: translate the network type to the
 * hardware state and hand it to the HAL variable that programs the port.
 */
#include <drv_types.h>

u8 setopmode_hdl(_adapter *padapter, u8 *pbuf)
{
	struct setopmode_parm *psetop = (struct setopmode_parm *)pbuf;
	u8 type;

	if (psetop->mode == Ndis802_11APMode || psetop->mode == Ndis802_11_mesh)
		type = _HW_STATE_AP_;
	else if (psetop->mode == Ndis802_11Infrastructure)
		type = _HW_STATE_STATION_;
	else if (psetop->mode == Ndis802_11IBSS)
		type = _HW_STATE_ADHOC_;
	else if (psetop->mode == Ndis802_11Monitor)
		type = _HW_STATE_MONITOR_;
	else
		type = _HW_STATE_NOLINK_;

	rtw_hal_set_hwreg(padapter, HW_VAR_SET_OPMODE, &type);
	return H2C_SUCCESS;
}
