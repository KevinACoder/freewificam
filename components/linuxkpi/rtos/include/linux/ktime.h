/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/ktime.h
 * @brief   The monotonic-time type and conversions a driver's timestamp path
 *          names.
 *
 * @design declaration-only compatibility face. The ktime record, the clock reads
 *         and the conversion and arithmetic helpers are part of the core contract
 *         every file of this face includes (linuxkpi_core.h: ktime_t, ktime_get,
 *         ktime_to_ms and the add/sub forms), so a consumer that reaches them
 *         through this header names the same declarations the core publishes.
 *         This header exists because the kernel's own spelling is what its sources
 *         include. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_KTIME_H_
#define _LINUXKPI_LINUX_KTIME_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_KTIME_H_ */
