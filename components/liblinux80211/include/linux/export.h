/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/export.h
 * @brief   The symbol-export spellings a driver's registration lines wear.
 *
 * @design declaration-only compatibility face. This project links every driver
 *         into one flat image, so a symbol export has no consumer and every
 *         spelling is inert. The core header every file of this face includes
 *         already carries the two forms that a built-in build actually reaches
 *         (EXPORT_SYMBOL, EXPORT_SYMBOL_GPL); the namespaced and
 *         KUNIT-conditional forms are named only inside preprocessor branches
 *         this port's configuration never selects, so no compiled translation
 *         unit reaches them and they are not stated here. This header exists
 *         because the kernel's own spelling is what its sources include.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_EXPORT_H_
#define _LINUXKPI_LINUX_EXPORT_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/export.h: the namespaced spellings. iwlwifi's two modules
 * (`iwlwifi` core and `iwlmvm`) export their cross-module entry points with
 * them when CONFIG_IWLWIFI_OPMODE_MODULAR is set (iwl-drv.h:94). This image
 * links both halves into one flat image, so the export is inert exactly like
 * EXPORT_SYMBOL above. */
#define	EXPORT_SYMBOL_NS(sym, ns)
#define	EXPORT_SYMBOL_NS_GPL(sym, ns)

#endif /* _LINUXKPI_LINUX_EXPORT_H_ */
