/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/gfp.h
 * @brief   The allocation-flag bits a driver ORs into its allocation requests.
 *
 * @design declaration-only compatibility face. The two flags that select
 *         wait-or-fail (GFP_KERNEL, GFP_ATOMIC) are part of the core contract
 *         every file of this face includes, and on this port a flag only selects
 *         that behaviour; what this header adds are the modifier bits a compiled
 *         consumer ORs on top. Their numbering is the interface's
 *         (include/linux/gfp_types.h, the ___GFP_*_BIT enumeration), so a bit
 *         that a driver sets means what the interface means by it even though the
 *         port only reads the wait-or-fail pair. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_GFP_H_
#define _LINUXKPI_LINUX_GFP_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/gfp_types.h:40, 75: BIT(13) */
#define	__GFP_NOWARN		BIT(13)
/* [LNX] include/linux/gfp_types.h:43, 80: BIT(18) */
#define	__GFP_COMP		BIT(18)

#endif /* _LINUXKPI_LINUX_GFP_H_ */
