/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/mutex.h
 * @brief   The blocking-mutex operations the interface declares that this component's
 *          mutex section does not already state.
 *
 * @design declaration-only compatibility face. The interface (include/linux/mutex.h)
 *         owns the mutex type and its whole operation set; in this component the type
 *         and the primary operations are already stated once in the contract header
 *         (components/linuxkpi/linuxkpi_core.h, its locking section: the opaque struct
 *         mutex plus mutex_init/lock/unlock/trylock/is_locked), and those are macros
 *         there, so restating them here would be both a redefinition and a second
 *         mechanism. What remains - the interruptible and killable takes - is declared
 *         with the interface's exact signatures and no body: making a take interruptible
 *         means the port's mutex wait has to be abortable, which this port's mutex does
 *         not model yet, and a caller that needs it must be given the real
 *         implementation rather than a plain blocking take wearing the interruptible
 *         name. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_MUTEX_H_
#define _LINUXKPI_LINUX_MUTEX_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/mutex.h:215-216 */
extern int __must_check mutex_lock_interruptible(struct mutex *lock);
extern int __must_check mutex_lock_killable(struct mutex *lock);

#endif /* _LINUXKPI_LINUX_MUTEX_H_ */
