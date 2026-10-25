/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/kref.h
 * @brief   The include a driver names for the reference-count type; the type is the
 *          component's.
 *
 * @design declaration-only compatibility face. The interface (include/linux/kref.h)
 *         owns struct kref and its operations; the contract header
 *         (components/linuxkpi/linuxkpi_core.h, its kref/rcu section) already states
 *         the struct and kref_init/kref_get/kref_put with the bodies this port runs.
 *         Restating them here would be the second spelling of one mechanism, so this
 *         header satisfies the include by name only. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_KREF_H_
#define _LINUXKPI_LINUX_KREF_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_KREF_H_ */
