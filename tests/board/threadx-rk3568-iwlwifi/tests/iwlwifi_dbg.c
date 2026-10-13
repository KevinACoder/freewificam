/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    iwlwifi_dbg.c
 * @brief   The console's handle on the driver's debug mask.
 *
 * @design adapter, one function wide: iwlwifi's verbosity lives in its
 *         module-parameter block (iwlwifi_mod_params.debug_level, read by
 *         iwl_have_debug_level in iwl-debug.h) and Linux sets it through
 *         module_param_named - which this port has no control plane for (the
 *         macro is a stub).  The variable is the driver's own and the image is
 *         built with CONFIG_IWLWIFI_DEBUG, so the only thing that has to be this
 *         port's is the door.  It is a translation unit of its own because it
 *         includes the driver's headers (iwl-modparams.h and the linux/ set
 *         behind it); the console file stays on the bridge's includes, and the
 *         two sets do not mix.
 *
 * Why the board wants the door: a driver that refuses a command deep in its
 * transmit path says so at debug level - the family a bring-up does not enable.
 * `wlan dbg <hex>` before the failing step is how that line is read; the mask
 * is the driver's own IWL_DL_* set (iwl-debug.h: 0x04 HCMD, 0x80000000
 * TX_QUEUES, 0x40000000 TX_REPLY, 0x00002000 DROP, 0x00800000 TX).
 * @date    13.10.2026
 */

#include "iwl-modparams.h"

/*
 * Set the driver's debug mask: 0 turns the extra lines off; the families a
 * diagnosis wants are OR-ed together by the console's caller.
 */
void
iwlwifi_set_debug_level(unsigned int mask)
{

	iwlwifi_mod_params.debug_level = mask;
}
