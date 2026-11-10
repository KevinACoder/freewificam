/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/time.h
 * @brief   The wall-clock surface a driver's header reaches for.
 *
 * @design declaration-only compatibility face. The interface's header carries the
 *         time-of-day records and their accessors; this port runs no wall clock -
 *         its time source is the monotonic tick the core contract exposes
 *         (linuxkpi_core.h: ktime_get and friends) - and no compiled translation
 *         unit reaches a symbol of this header, only its name. The declarations
 *         are therefore left out rather than transcribed into a surface with no
 *         implementation and no caller; the first caller that needs one arrives
 *         with its declaration and the interface's exact shape. Reference for the
 *         face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_TIME_H_
#define _LINUXKPI_LINUX_TIME_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_TIME_H_ */
