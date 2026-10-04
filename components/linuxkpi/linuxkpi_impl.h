/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * linuxkpi impl - the bridge between the driver-facing linuxkpi_core.h
 * and this port's OSAL world.  Included ONLY by the linuxkpi implementation
 * translation units; the drivers never see this file (and through it,
 * never see a port header).
 *
 * Ported from the prior experiment line ([EXPL] linuxkpi_impl.h, which mapped
 * onto the NetBSD compat world) onto the freewebcam base (SPEC S6/linuxkpi
 * §3.9): kmem = the tlsf-backed heap (heap.h, one allocator discipline),
 * mutex = CMSIS-RTOS2 osMutex over ThreadX (cmsis2_threadx.c - the first
 * consumer brings the osMutex face, the [S7a] §3.3.3 precedent), completion
 * and wait queues = osMutex + an embedded ThreadX counting semaphore as the
 * wake channel, callouts = ThreadX timers.
 *
 * The opaque storage sizes in linuxkpi_core.h are pinned to the shells below
 * (mutex = one osMutexId_t, completion/waitqueue = mutex + the semaphore
 * shell, callout = one TX_TIMER + its state); the static asserts make drift
 * a compile error, not a boot corruption.
 */

#ifndef LINUXKPI_IMPL_H
#define LINUXKPI_IMPL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <tx_api.h>
/* [S5b-W3] TX_TIMER_ID for the idempotent lkpi_timer_setup re-init check:
 * tx_api.h carries the TYPE definitions only; the component id macros live
 * in the per-component headers. */
#include <tx_timer.h>

#include "cmsis_os2.h"
#include "heap.h"
#include "log.h"

#include "linuxkpi_core.h"

/* the driver face's shells are sized for these; see the asserts below */
struct lkpi_cv_shell {
	TX_SEMAPHORE	sem;	/* the wake channel */
	int		waiters;
};

static inline osMutexId_t *
lk_mutex(struct mutex *m)
{
	return (osMutexId_t *)&m->_storage[0];
}

static inline osMutexId_t *
lk_wqmutex(struct wait_queue_head *wq)
{
	return (osMutexId_t *)&wq->_mtx[0];
}

static inline struct lkpi_cv_shell *
lk_wqcv(struct wait_queue_head *wq)
{
	return (struct lkpi_cv_shell *)&wq->_cv[0];
}

static inline osMutexId_t *
lk_cmtx(struct completion *c)
{
	return (osMutexId_t *)&c->_mtx[0];
}

static inline struct lkpi_cv_shell *
lk_ccv(struct completion *c)
{
	return (struct lkpi_cv_shell *)&c->_cv[0];
}

/* callout shell: one TX_TIMER plus the state the expiry callback needs
 * (the timer expiry function receives only the timer pointer) */
struct lkpi_callout_shell {
	TX_TIMER	timer;
	void		(*fn)(void *);
	void		*arg;
};

static inline struct lkpi_callout_shell *
lk_callout_dw(struct delayed_work *dw)
{
	return (struct lkpi_callout_shell *)&dw->_callout[0];
}

static inline struct lkpi_callout_shell *
lk_callout_tl(struct timer_list *t)
{
	return (struct lkpi_callout_shell *)&t->_callout[0];
}

/* [S6] B1/B2 shells: semaphore = one TX_SEMAPHORE, kthread = one
 * TX_THREAD plus the stop/exit flags that ride the task_struct */
static inline TX_SEMAPHORE *
lk_sem(struct semaphore *s)
{
	return (TX_SEMAPHORE *)&s->_sem[0];
}

static inline TX_THREAD *
lk_thread(struct task_struct *t)
{
	return (TX_THREAD *)&t->_tx[0];
}

_Static_assert(sizeof(struct semaphore) >= sizeof(TX_SEMAPHORE),
    "struct semaphore storage must hold the TX_SEMAPHORE shell");
_Static_assert(sizeof(struct task_struct) >= sizeof(TX_THREAD),
    "struct task_struct storage must hold the TX_THREAD shell");

_Static_assert(sizeof(struct mutex) >= sizeof(osMutexId_t),
    "struct mutex storage must hold the osMutex id");
_Static_assert(sizeof(struct completion) >=
    sizeof(osMutexId_t) + sizeof(struct lkpi_cv_shell),
    "struct completion storage must hold mutex + semaphore shells");
_Static_assert(sizeof(struct wait_queue_head) >=
    sizeof(osMutexId_t) + sizeof(struct lkpi_cv_shell),
    "struct wait_queue_head storage must hold mutex + semaphore shells");
_Static_assert(sizeof(((struct delayed_work *)0)->_callout) >=
    sizeof(struct lkpi_callout_shell),
    "delayed_work callout storage must hold the TX_TIMER shell");
_Static_assert(sizeof(((struct timer_list *)0)->_callout) >=
    sizeof(struct lkpi_callout_shell),
    "timer_list callout storage must hold the TX_TIMER shell");

#endif /* LINUXKPI_IMPL_H */
