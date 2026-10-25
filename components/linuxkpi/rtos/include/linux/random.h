/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/random.h
 * @brief   The include a driver names for the random-value vocabulary; the source
 *          is the component's.
 *
 * @design declaration-only compatibility face. The interface (include/linux/
 *         random.h) owns the random-value surface: the byte/word draws, the bounded
 *         and inclusive helpers, and the device-MAC helper. In this component the
 *         source (lkpi_random_u32, a xorshift stream seeded from the tick) and the
 *         draws this port serves (get_random_bytes, get_random_u32 and the bounded
 *         forms get_random_u32_below/get_random_u32_inclusive) already live in the
 *         core contract (components/linuxkpi/linuxkpi_core.h), which every file of
 *         this face includes; get_random_int and the mask-address helper are the
 *         core's too. Restating them here would be the second spelling of one
 *         mechanism the project forbids, so this header carries no declarations of
 *         its own: it exists because the vendored bytes include it by name, and it is
 *         the contract header that answers. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_RANDOM_H_
#define _LINUXKPI_LINUX_RANDOM_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_RANDOM_H_ */
