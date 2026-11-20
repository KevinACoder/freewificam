/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * wifi_base_config.h - compatibility shim.
 *
 * The vendor SDK's SDIO bus protocol carries `#include "wifi_base_config.h"`
 * (a sample-project header the vendor added to their copy of the SDK); nothing
 * in that file uses anything from it.  This port is not a sample project, so the
 * include resolves here, to a header that states why it exists.  It must stay
 * empty: a real definition would make the driver depend on an application.
 */

#ifndef AP6181_WIFI_BASE_CONFIG_SHIM_H
#define AP6181_WIFI_BASE_CONFIG_SHIM_H

#endif /* AP6181_WIFI_BASE_CONFIG_SHIM_H */
