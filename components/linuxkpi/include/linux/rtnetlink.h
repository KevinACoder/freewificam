/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/rtnetlink.h
 * @brief   The global network-configuration lock a driver's registration and interface
 *          paths take.
 *
 * @design declaration-only compatibility face. The interface (include/linux/rtnetlink.h)
 *         is a large surface (link kinds, attributes, message builders); the only part
 *         the vendored bytes reach is the one global RTNL lock, which on a Linux kernel
 *         serialises every change to network configuration. The lock is declared with
 *         the interface's exact signatures and with no body: this project presents its
 *         interfaces through lwIP rather than through the Linux netdev registry, and
 *         whether that stack needs - or has - one global configuration lock is the port's
 *         decision, not the face's, so a caller that reaches these gets an unresolved
 *         symbol instead of a silent no-op that would pretend the configuration is
 *         serialised. The interface's own include of linux/mutex.h is kept. Reference for
 *         Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
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
