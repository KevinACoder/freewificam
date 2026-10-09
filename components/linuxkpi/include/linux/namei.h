/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/namei.h
 * @brief   The include a driver's platform layer names for path lookups; this project
 *          has no filesystem, and nothing in the compiled set reaches it.
 *
 * @design declaration-only compatibility face, deliberately empty of declarations. The
 *         interface (include/linux/namei.h) describes path traversal and `struct
 *         nameidata`, the per-task path-walk state; there is no filesystem on the
 *         target and the vendored bytes call nothing from it, so carrying a shape here
 *         would be a guess about a mechanism that is not on either side of the
 *         interface. The header exists because the vendor's Linux platform layer
 *         includes it by name, and its absence would otherwise stop the parse before
 *         the code that actually matters. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_NAMEI_H_
#define _LINUXKPI_LINUX_NAMEI_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_NAMEI_H_ */
