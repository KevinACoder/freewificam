/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/unaligned.h
 * @brief   The unaligned little-endian load and store macros a driver's firmware
 *          record accessors are built with.
 *
 * @design declaration-only compatibility face. The get_unaligned_le16/32/64 and
 *         put_unaligned_le32/64 macros are part of the core contract every file of
 *         this face includes, so a consumer that reaches them through this header
 *         names the same declarations the core publishes. This header exists
 *         because the kernel's own spelling is what its sources include.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_UNALIGNED_H_
#define _LINUXKPI_LINUX_UNALIGNED_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_UNALIGNED_H_ */
