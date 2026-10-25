/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/bits.h
 * @brief   The bit and bit-field mask spellings a driver's register and command
 *          tables are built with.
 *
 * @design declaration-only compatibility face. The mask vocabulary (BIT, BIT_ULL,
 *         GENMASK, GENMASK_ULL), the word-size constants (BITS_PER_LONG,
 *         BITS_PER_BYTE, BITS_PER_TYPE) and the DIV_ROUND_UP helper are part of
 *         the core contract every file of this face includes, so a consumer that
 *         reaches them through this header names the same declarations the core
 *         publishes. This header exists because the kernel's own spelling is what
 *         its sources include. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_BITS_H_
#define _LINUXKPI_LINUX_BITS_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_BITS_H_ */
