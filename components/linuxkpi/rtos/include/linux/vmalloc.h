/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/vmalloc.h
 * @brief   The include a driver names for the virtually-mapped allocator; the
 *          allocation is the component's.
 *
 * @design declaration-only compatibility face. The interface (include/linux/vmalloc.h)
 *         separates vmalloc from kmalloc because a Linux kernel can hand back
 *         non-physically-contiguous pages; this project's heap has one linear address
 *         space, so the contract header (components/linuxkpi/linuxkpi_core.h, its
 *         allocation section) already serves vmalloc/vzalloc/vfree from the same
 *         allocator and says why. A second statement here would be the duplicate
 *         mechanism the project forbids, so the header satisfies the include by name
 *         only. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_VMALLOC_H_
#define _LINUXKPI_LINUX_VMALLOC_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_VMALLOC_H_ */
