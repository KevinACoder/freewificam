/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    asm/atomic.h
 * @brief   The architecture's atomic-operations entry point.
 *
 * @design declaration-only compatibility face. The interface's architecture header for
 *         this machine (arch/arm64/include/asm/atomic.h:13-19) includes <linux/types.h>
 *         and works on the `atomic_t` type, defining the arch_* operation set; the
 *         operations themselves come from <linux/atomic.h>. This port has exactly one
 *         atomic model - the contract header's plain arithmetic operations on a
 *         single-core image, extended by the face's <linux/atomic.h> - so this header
 *         routes to that model instead of stating a second one: it includes
 *         <linux/atomic.h>, which is where the type and the whole operation set live.
 *         Nothing here is arch-specific on this port, and nothing should be added here
 *         until the port gains real arch atomic primitives that the contract header
 *         cannot express. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_ASM_ATOMIC_H_
#define _LINUXKPI_ASM_ATOMIC_H_

#include "../../linuxkpi_core.h"
#include <linux/atomic.h>

#endif /* _LINUXKPI_ASM_ATOMIC_H_ */
