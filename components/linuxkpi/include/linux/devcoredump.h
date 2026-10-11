/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/devcoredump.h
 * @brief   The device-core-dump entry point under the name a driver includes it.
 *
 * @design declaration-only compatibility face, and a pure forward: the entry point is
 *         already stated in the contract header (components/linuxkpi/linuxkpi_core.h,
 *         whose device-coredump section is the single definition - a statement-form macro
 *         that names its arguments, which is what a caller that transfers the buffer's
 *         ownership needs to see). The interface keeps this header for the same name;
 *         this face keeps one mechanism, so the interface's name reaches the contract
 *         header and the entry point is not restated here (restating it would be shadowed
 *         by the macro anyway).
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_DEVCOREDUMP_H_
#define _LINUXKPI_LINUX_DEVCOREDUMP_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_DEVCOREDUMP_H_ */
