/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/semaphore.h
 * @brief   The counting semaphore a driver's thread-signalling paths use.
 *
 * @design declaration-only compatibility face. The interface (include/linux/
 *         semaphore.h) states the semaphore as a lock plus a count plus a waiter list,
 *         and declares the take/release operations; the vendored bytes embed the object
 *         by value in their own control blocks, so the shape has to be complete here.
 *         The fields are transcribed with the interface's names and order (the lock is
 *         this component's spinlock type - the same critical section the interface's
 *         raw spinlock spells). The one body is sema_init: initialising the declared
 *         fields is exactly what the interface's own static inline does, and it is the
 *         only part of the API that can be stated truthfully without the port. The
 *         operations that actually block or wake a task are declared with the
 *         interface's signatures and deliberately left undefined - waiting on a count
 *         needs the port's sleeping primitive, and an inert body would silently report
 *         a permit that was never available. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_SEMAPHORE_H_
#define _LINUXKPI_LINUX_SEMAPHORE_H_

#include "../../linuxkpi_core.h"

struct semaphore_waiter;

/* [LNX] include/linux/semaphore.h:15-23: the interface's fields, in its order. The lock
 * is `raw_spinlock_t` there; the project's spinlock type is the same critical section
 * (see the locking section of the contract header). */
struct semaphore {
	spinlock_t		lock;
	unsigned int		count;
	struct semaphore_waiter	*first_waiter;
};

/* [LNX] include/linux/semaphore.h:43-48: sema_init() is a static inline there that
 * stamps the initial count into the object. The names and the count are the whole
 * state a take/release pair will use, so this body is real rather than inert. */
static __always_inline __unused void
sema_init(struct semaphore *sem, int val)
{
	sem->count = (unsigned int)val;
	sem->first_waiter = NULL;
	spin_lock_init(&sem->lock);
}

/* [LNX] include/linux/semaphore.h:56-61, with the interface's exact signatures. No
 * bodies: blocking on a count, and waking the waiters, is the port's primitive. */
extern void down(struct semaphore *sem);
extern int __must_check down_interruptible(struct semaphore *sem);
extern int __must_check down_killable(struct semaphore *sem);
extern int __must_check down_trylock(struct semaphore *sem);
extern int __must_check down_timeout(struct semaphore *sem, long jiffies);
extern void up(struct semaphore *sem);

#endif /* _LINUXKPI_LINUX_SEMAPHORE_H_ */
