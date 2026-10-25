/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/lockdep.h
 * @brief   The runtime lock-validator vocabulary a driver's locking assertions and
 *          lock-class tags are written with.
 *
 * @design declaration-only compatibility face. The interface (include/linux/
 *         lockdep.h) selects one of two arms on CONFIG_LOCKDEP; this port does not
 *         select the option, so the off arm is the reachable one: the map and key
 *         records take no space, every acquire/release annotation is empty, and an
 *         assertion is a no-op. The one shape worth naming is lockdep_is_held(),
 *         which the interface keeps as an extern query even with the validator
 *         off (it is used inside an expression), so it is declared here rather than
 *         made a constant. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_LOCKDEP_H_
#define _LINUXKPI_LINUX_LOCKDEP_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/lockdep_types.h:270,275: the CONFIG_LOCKDEP-off arm, in
 * which neither record takes space. */
struct lockdep_map { };
struct lock_class_key { };

/* [LNX] include/linux/lockdep.h:389 */
extern int lockdep_is_held(const void *);

/* [LNX] include/linux/lockdep.h:396. The held-assertion (include/linux/lockdep.h:
 * 395) is already carried by the core contract, so it is not repeated here. */
#define	lockdep_assert_not_held(l)	do { (void)(l); } while (0)

/* [LNX] include/linux/lockdep.h:354,346 */
#define	lockdep_set_class(lock, key)	do { (void)(key); } while (0)
#define	lockdep_init_map(lock, name, key, sub) \
	do { (void)(name); (void)(key); } while (0)

/* [LNX] include/linux/lockdep.h:544-548: the map acquire/release annotations,
 * which expand to nothing when the validator is off - the argument is not even
 * evaluated, which is what lets a call site name a map member that only exists in
 * the validator-on build. */
#define	lock_map_acquire(l)		do { } while (0)
#define	lock_map_acquire_read(l)	do { } while (0)
#define	lock_map_release(l)		do { } while (0)

#endif /* _LINUXKPI_LINUX_LOCKDEP_H_ */
