/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/rtnetlink.h
 * @brief   The global network-configuration lock a driver's registration and interface
 *          paths take.
 *
 * @design declaration-only compatibility face. The interface (include/linux/rtnetlink.h)
 *         is a large surface (link kinds, attributes, message builders); the only part
 *         the vendored bytes reach is the one global RTNL lock, which on a Linux kernel
 *         serialises every change to network configuration. The lock is declared with
 *         the interface's exact signatures. Its body (linuxkpi_compat.c) is a real
 *         global lock, not a silent no-op: the port takes the core's mutex, the same
 *         blocking lock the interface takes, so the configuration changes the driver
 *         brackets with it are actually serialised. The other queries (rtnl_trylock,
 *         rtnl_is_locked, rtnl_lock_interruptible) are declared for the interface's
 *         shape but not reached by the compiled set, so they have no body. The
 *         interface's own include of linux/mutex.h is kept. Reference for the face
 *         shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_RTNETLINK_H_
#define _LINUXKPI_LINUX_RTNETLINK_H_

#include "../../linuxkpi_core.h"

#include <linux/mutex.h>

/* [LNX] include/linux/rtnetlink.h:42-46 */
extern void rtnl_lock(void);
extern void rtnl_unlock(void);
extern int rtnl_trylock(void);
extern int rtnl_is_locked(void);
extern int rtnl_lock_interruptible(void);

#endif /* _LINUXKPI_LINUX_RTNETLINK_H_ */
