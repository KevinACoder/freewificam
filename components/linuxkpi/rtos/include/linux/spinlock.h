/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/spinlock.h
 * @brief   The include a driver's locking section names; the lock itself is the
 *          component's.
 *
 * @design declaration-only compatibility face. The interface (include/linux/spinlock.h)
 *         owns the lock type and its operations; in this component they already live in
 *         the contract header (components/linuxkpi/linuxkpi_core.h, its locking
 *         section), which every file of this face includes. Restating them here would
 *         be the second spelling of one mechanism the project forbids, so this header
 *         carries no declarations of its own: it exists because the vendored bytes
 *         include it by name, and it is the contract header that answers. Reference for
 *         Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_SPINLOCK_H_
#define _LINUXKPI_LINUX_SPINLOCK_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_SPINLOCK_H_ */
