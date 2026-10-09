/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/slab.h
 * @brief   The include a driver names for the kmalloc family; the allocator is the
 *          component's.
 *
 * @design declaration-only compatibility face. The interface (include/linux/slab.h)
 *         owns the kmalloc/kzalloc/kcalloc/kfree family and the devm variants; the
 *         allocator this component serves - including the size-header heap contract and
 *         the gfp flags reducing to wait-or-fail - is already stated in the contract
 *         header (components/linuxkpi/linuxkpi_core.h, its allocation section). This
 *         header therefore only satisfies the include by name rather than restating a
 *         second allocator API. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_SLAB_H_
#define _LINUXKPI_LINUX_SLAB_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_SLAB_H_ */
