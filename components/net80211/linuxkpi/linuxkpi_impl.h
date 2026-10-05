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
 * and wait queues = osMutex + an embedded caller-storage counting semaphore
 * as the wake channel (cmsis_os2_ext.h osSemStatic*), callouts = the
 * caller-storage timer shell (osTimerStatic*).
 *
 * The opaque storage sizes in linuxkpi_core.h are pinned to the shells below
 * (mutex = one osMutexId_t, completion/waitqueue = mutex + the semaphore
 * shell, callout = one os_timer_static_t); the static asserts make drift
 * a compile error, not a boot corruption.
 */

#ifndef LINUXKPI_IMPL_H
#define LINUXKPI_IMPL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "cmsis_os2.h"
#include "cmsis_os2_ext.h"
#include "heap.h"
#include "log.h"

#include "linuxkpi_core.h"

/* the driver face's shells are sized for these; see the asserts below */
struct lkpi_cv_shell {
	os_sem_static_t	sem;	/* the wake channel */
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

/* callout shell: the ext static timer (OS timer storage + the expiry
 * state it runs - the OS timer callback receives only the timer) */
struct lkpi_callout_shell {
	os_timer_static_t	timer;
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
    "delayed_work callout storage must hold the os_timer_static_t shell");
_Static_assert(sizeof(((struct timer_list *)0)->_callout) >=
    sizeof(struct lkpi_callout_shell),
    "timer_list callout storage must hold the os_timer_static_t shell");

#endif /* LINUXKPI_IMPL_H */
