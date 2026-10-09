/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/linkage.h
 * @brief   The linkage marker a kernel function declaration wears, as the interface
 *          defines it.
 *
 * @design declaration-only compatibility face: one macro pair, transcribed as the
 *         interface has it (include/linux/linkage.h: `asmlinkage` is `CPP_ASMLINKAGE`,
 *         which is nothing outside C++). On the interface's aarch64 port the marker is
 *         equally empty in effect, so the empty expansion here is the interface's own
 *         answer and not a simplification. Carried because the scheduling entry points
 *         this face declares (components/linuxkpi/include/linux/sched.h) wear it in the
 *         interface's declaration, and a face that dropped it would not be transcribing
 *         that declaration's shape. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_LINKAGE_H_
#define _LINUXKPI_LINUX_LINKAGE_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/linkage.h:16-23 */
#ifdef __cplusplus
#define	CPP_ASMLINKAGE	extern "C"
#else
#define	CPP_ASMLINKAGE
#endif

#ifndef asmlinkage
#define	asmlinkage	CPP_ASMLINKAGE
#endif

#endif /* _LINUXKPI_LINUX_LINKAGE_H_ */
