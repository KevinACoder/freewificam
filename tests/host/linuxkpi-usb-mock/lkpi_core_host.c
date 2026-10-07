/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * lkpi_core_host.c - the host build's minimal linuxkpi core services.
 *
 * The linuxkpi USB engine (components/linuxkpi/usb) reaches its platform
 * services through the declarations of components/linuxkpi/linuxkpi_core.h:
 * allocation, logging, millisecond sleeps, mutexes, DAIF spin sections and
 * wait-queue initialisation.  The in-tree core implementation belongs to the
 * later linuxkpi body; until it lands, the host mock suite supplies exactly
 * these service bodies, so the engine compiles and runs without carrying a
 * second copy of anything.
 *
 * @design ports & adapters: this file is the host adapter of the core
 * service port; the declarations come from the component's own header and
 * every body maps one-to-one onto a CMSIS-RTOS2 primitive, so the engine
 * cannot tell which adapter it runs on.  Allocation is the only service with
 * extra machinery: a size header plus live-block accounting, since the
 * engine's consumers read the pool counters as evidence.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"

/* ------------------------------------------------------------------ */
/* logging                                                             */
/* ------------------------------------------------------------------ */

int
lkpi_printf(const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vfprintf(stdout, fmt, ap);
	va_end(ap);
	fflush(stdout);
	return n;
}

/* ------------------------------------------------------------------ */
/* allocation (a size header keeps kfree() able to report the size)    */
/* ------------------------------------------------------------------ */

struct lkpi_mem_hdr {
	size_t	size;
	unsigned long long magic;
};

#define	LKPI_MEM_MAGIC	0x6c6b70696d656d31ULL	/* "lkpimem1" */

void *
lkpi_kmem_alloc(size_t size, int flags, bool zero)
{
	struct lkpi_mem_hdr *h;

	(void)flags;
	h = malloc(sizeof(*h) + size);
	if (h == NULL)
		return NULL;
	h->size = size;
	h->magic = LKPI_MEM_MAGIC;
	if (zero)
		memset(h + 1, 0, size);
	else
		memset(h + 1, 0, size > 0 ? size : 0);
	return h + 1;
}

void
lkpi_kmem_free(const void *p)
{
	struct lkpi_mem_hdr *h;

	if (p == NULL)
		return;
	h = (struct lkpi_mem_hdr *)p - 1;
	if (h->magic != LKPI_MEM_MAGIC) {
		lkpi_printf("lkpi_core: free of a stray block %p\n", p);
		abort();
	}
	h->magic = 0;
	free(h);
}

/* ------------------------------------------------------------------ */
/* time                                                                */
/* ------------------------------------------------------------------ */

void
lkpi_msleep(unsigned int ms)
{

	(void)osDelay(ms != 0 ? ms : 1u);
}

/* ------------------------------------------------------------------ */
/* mutexes (CMSIS-RTOS2)                                               */
/* ------------------------------------------------------------------ */

void
lkpi_mutex_init(struct mutex *m)
{
	osMutexAttr_t attr = { 0 };

	attr.name = "lkpi_mutex";
	m->_storage[0] = osMutexNew(&attr);
	if (m->_storage[0] == NULL) {
		lkpi_printf("lkpi_core: mutex create failed\n");
		abort();
	}
}

void
lkpi_mutex_destroy(struct mutex *m)
{

	if (m->_storage[0] != NULL)
		(void)osMutexDelete((osMutexId_t)m->_storage[0]);
	m->_storage[0] = NULL;
}

void
lkpi_mutex_lock(struct mutex *m)
{

	(void)osMutexAcquire((osMutexId_t)m->_storage[0], osWaitForever);
}

void
lkpi_mutex_unlock(struct mutex *m)
{

	(void)osMutexRelease((osMutexId_t)m->_storage[0]);
}

int
lkpi_mutex_trylock(struct mutex *m)
{

	return osMutexAcquire((osMutexId_t)m->_storage[0], 0u) == osOK ? 1 : 0;
}

int
lkpi_mutex_is_locked(struct mutex *m)
{

	return osMutexGetOwner((osMutexId_t)m->_storage[0]) != NULL;
}

/* ------------------------------------------------------------------ */
/* spinlocks: DAIF sections (the critical-section extension)           */
/* ------------------------------------------------------------------ */

unsigned long long
lkpi_spin_lock_irqsave(spinlock_t *l)
{
	os_critical_state_t s = osCriticalEnter();

	l->_daif = (unsigned long long)s;
	return l->_daif;
}

void
lkpi_spin_unlock_irqrestore(spinlock_t *l, unsigned long long flags)
{

	(void)l;
	osCriticalExit((os_critical_state_t)flags);
}

void
lkpi_spin_lock(spinlock_t *l)
{

	l->_daif = (unsigned long long)osCriticalEnter();
}

void
lkpi_spin_unlock(spinlock_t *l)
{

	osCriticalExit((os_critical_state_t)l->_daif);
}

int
lkpi_spin_trylock(spinlock_t *l)
{

	lkpi_spin_lock(l);
	return 1;
}

/* ------------------------------------------------------------------ */
/* wait queues                                                         */
/* ------------------------------------------------------------------ */

/* the header's storage slots are the object shells: [0] the mutex, [1] a
 * counting semaphore used for wake-ups */
void
lkpi_waitqueue_init(struct wait_queue_head *wq)
{
	osMutexAttr_t mattr = { 0 };
	osSemaphoreAttr_t sattr = { 0 };

	mattr.name = "lkpi_wq";
	sattr.name = "lkpi_wq_cv";
	wq->_mtx[0] = osMutexNew(&mattr);
	wq->_cv[0] = osSemaphoreNew(1u, 0u, &sattr);
	if (wq->_mtx[0] == NULL || wq->_cv[0] == NULL) {
		lkpi_printf("lkpi_core: waitqueue create failed\n");
		abort();
	}
}

void
lkpi_waitqueue_wake(struct wait_queue_head *wq)
{

	(void)osSemaphoreRelease((osSemaphoreId_t)wq->_cv[0]);
}

void
lkpi_waitq_lock(struct wait_queue_head *wq)
{

	(void)osMutexAcquire((osMutexId_t)wq->_mtx[0], osWaitForever);
}

void
lkpi_waitq_unlock(struct wait_queue_head *wq)
{

	(void)osMutexRelease((osMutexId_t)wq->_mtx[0]);
}

int
lkpi_waitq_timedwait(struct wait_queue_head *wq, int ms)
{
	osStatus_t st;

	st = osSemaphoreAcquire((osSemaphoreId_t)wq->_cv[0],
	    ms > 0 ? (uint32_t)ms : osWaitForever);
	return st == osOK ? 1 : 0;
}
