/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/stringify.h
 * @brief   The indirect stringification macros a driver's build-time names are
 *          built with.
 *
 * @design declaration-only compatibility face. The core header every file of this
 *         face includes already carries the two-level __stringify() indirection
 *         (components/linuxkpi/linuxkpi_core.h), because the device-model and
 *         sysfs faces reach for it; this header exists because the kernel's own
 *         spelling is what its sources include. The only shape it adds is
 *         FILE_LINE, the "file:line" spelling the interface builds from __FILE__
 *         and __stringify(__LINE__). Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_STRINGIFY_H_
#define _LINUXKPI_LINUX_STRINGIFY_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/stringify.h:13 */
#define	FILE_LINE	__FILE__ ":" __stringify(__LINE__)

#endif /* _LINUXKPI_LINUX_STRINGIFY_H_ */
