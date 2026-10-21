/*
 * @file
 * @brief The single CMSIS-RTOS2 osal of the net80211 port.
 *
 * One adapter implements the kernel-service face the imported stack expects
 * (the compat headers under ports/net80211/compat/sys/) on the CMSIS-RTOS2 API,
 * and serves every backend alike: ThreadX, FreeRTOS and the host. This file
 * names no kernel header - the backend is selected at link time.
 *
 * Service list: intrusive mutexes with owner/depth bookkeeping, condition
 * variables, the port serializer (the splnet() replacement, with
 * suspend/resume), the tick base and delays, bounded identified sleeps,
 * run-once, a software random source, allocation wrappers over the seam and the
 * single log/panic exit.
 *
 * ISR rule: interrupts only defer (semaphore or work item). No blocking service
 * in this file may be called from interrupt context; the port is entered from
 * the driver's worker, never from the ISR itself.
 *
 * @design ports & adapters: the adapter boundary is CMSIS-RTOS2, so the port
 * does not multiply per-kernel implementations. Service list re-derived from
 * the component's own FreeRTOS osal (same face it implemented):
 * components/net80211/port/osal/freertos/osal_freertos.c:72-652, and the
 * serializer shape from :433-522; bytes are ours, the API surface is the one
 * the imported code was written against.
 */

#include <errno.h>
#include <stdarg.h>
#include <string.h>

#include <cmsis_os2.h>

#ifdef WLAN_PORT_TARGET
/*
 * Target build: no C library behind this port. The display functions format
 * into a bounded buffer (the image's printf component, bound to the C
 * library's names) and hand the line
 * to the board's own log sink - the same one-line idiom the USB glue's
 * console sink uses - and the panic exit prints through the lock-free fault
 * sink and parks: a failed assertion that speaks is findable, a silent one
 * is not. The host build keeps the C library's vfprintf and abort().
 */
#include <stdio.h>

#include "log.h"
#include "arch_asm.h"
#elif defined(WLAN_PORT_LINUX)
/*
 * Linux kernel build: no C library. The log seam formats through the kernel's
 * vprintk and panic() parks the task with BUG(); the rest of this file is the
 * same single CMSIS-RTOS2 adapter the target uses.
 */
#include <linux/printk.h>
#include <linux/bug.h>
#else
#include <stdio.h>
#include <stdlib.h>
#endif

#include <sys/mutex.h>
#include <sys/intr.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/once.h>
#include <sys/cpu.h>
#include <sys/proc.h>
#include <sys/kmem.h>
#include <sys/malloc.h>

#include "../wlan_osal.h"

/* ------------------------------------------------------------------ */
/* Tick base                                                           */
/* ------------------------------------------------------------------ */

int hz = 1000;

ticks_t getticks(void)
{
	return (ticks_t) osKernelGetTickCount();
}

void delay(unsigned int us)
{
	uint32_t ms = (us + 999u) / 1000u;

	if (us != 0 && ms == 0) {
		ms = 1; /* sub-tick delays round up */
	}
	if (ms != 0) {
		osDelay(ms);
	}
}

/* ------------------------------------------------------------------ */
/* Mutexes                                                             */
/* ------------------------------------------------------------------ */

/* the port's own level-1 outlet (defined with the lifecycle below): the mutex
 * paths report a lock they could not take or did not hold through it */
static void osal_emergency(const char *fmt, ...);

#define WLAN_MUTEX_MAGIC 0x6d757821ul
/* hm_priv[] layout: [0] osMutexId_t, [1] owner thread id, [2] depth,
 * [3] magic. The RTOS mutex is created recursive as well so a re-entrant
 * enter is legal on both backends; the depth field is what the serializer
 * needs to release and restore a lock across a sleep. */
#define MP_ID(m)    ((m)->hm_priv[0])
#define MP_OWNER(m) ((m)->hm_priv[1])
#define MP_DEPTH(m) ((m)->hm_priv[2])
#define MP_MAGIC(m) ((m)->hm_priv[3])

static const osMutexAttr_t wlan_mutex_attr = {
	.name = "wlan_mutex",
	.attr_bits = osMutexRecursive | osMutexPrioInherit,
};

int wlan_mutex_init(kmutex_t *m, int type, int ipl)
{
	osMutexId_t id;

	(void) type;
	(void) ipl;
	if (m == NULL) {
		return EINVAL;
	}
	id = osMutexNew(&wlan_mutex_attr);
	if (id == NULL) {
		return ENOMEM;
	}
	memset(m->hm_priv, 0, sizeof(m->hm_priv));
	MP_ID(m) = (void *) id;
	MP_MAGIC(m) = (void *) WLAN_MUTEX_MAGIC;
	return 0;
}

int wlan_mutex_enter(kmutex_t *m)
{
	osStatus_t st;

	if (m == NULL || MP_MAGIC(m) != (void *) WLAN_MUTEX_MAGIC) {
		/*
		 * A lock the port never initialised is a silent no-op today, and
		 * a no-op enter is invisible until something checks ownership:
		 * the imported code's own assert (ieee80211_node.c's
		 * _ieee80211_find_node) then parks the board on a lock that was
		 * never taken. Name each distinct object once, with the caller
		 * and the word that is in its magic slot, so the site and the
		 * object both come off the serial line.
		 */
		static void *seen[4];
		static unsigned int n_seen;
		unsigned int i;

		for (i = 0; i < n_seen; i++) {
			if (seen[i] == (void *)m) {
				return EINVAL;
			}
		}
		if (n_seen < 4) {
			seen[n_seen++] = (void *)m;
			osal_emergency("net80211: mutex_enter refused on "
			    "uninitialised lock %p (magic %p ra %p)",
			    (void *)m, (void *) MP_MAGIC(m),
			    __builtin_return_address(0));
		}
		return EINVAL;
	}
	st = osMutexAcquire((osMutexId_t) MP_ID(m), osWaitForever);
	if (st != osOK) {
		static unsigned int said;

		if (said < 4) {
			said++;
			osal_emergency("net80211: mutex_acquire failed (%d) "
			    "lock %p ra %p", (int)st, (void *)m,
			    __builtin_return_address(0));
		}
		return EINVAL;
	}
	MP_OWNER(m) = (void *) osThreadGetId();
	MP_DEPTH(m) = (void *) ((uintptr_t) MP_DEPTH(m) + 1);
	return 0;
}

int wlan_mutex_exit(kmutex_t *m)
{
	osStatus_t st;

	if (m == NULL || MP_MAGIC(m) != (void *) WLAN_MUTEX_MAGIC) {
		return EINVAL;
	}
	if ((uintptr_t) MP_DEPTH(m) > 0) {
		MP_DEPTH(m) = (void *) ((uintptr_t) MP_DEPTH(m) - 1);
	}
	if ((uintptr_t) MP_DEPTH(m) == 0) {
		/*
		 * Clearing the owner is the one write that can turn a held lock
		 * into a false ownership verdict; name the first few, with the
		 * caller, so a clear that should not have happened shows.
		 */
		static unsigned int said;

		if (said < 4) {
			said++;
			osal_emergency("net80211: mutex_exit clears owner "
			    "of lock %p (depth now 0) ra %p", (void *)m,
			    __builtin_return_address(0));
		}
		MP_OWNER(m) = NULL;
	}
	st = osMutexRelease((osMutexId_t) MP_ID(m));
	return st == osOK ? 0 : EINVAL;
}

int wlan_mutex_owned(kmutex_t *m)
{
	if (m == NULL || MP_MAGIC(m) != (void *) WLAN_MUTEX_MAGIC) {
		return 0;
	}
	if (!(MP_OWNER(m) == (void *) osThreadGetId() && MP_OWNER(m) != NULL)) {
		/*
		 * Who asks and who holds: a false verdict from the imported
		 * code's asserts parks the board, and the object, the owner it
		 * saw and the caller are what say whether the lock was never
		 * taken, taken by another thread, or clobbered.
		 */
		static unsigned int said;

		if (said < 4) {
			said++;
			osal_emergency("net80211: mutex_owned false lock %p "
			    "owner %p current %p magic %p ra %p", (void *)m,
			    MP_OWNER(m) != NULL ? MP_OWNER(m) : (void *)0,
			    (void *) osThreadGetId(), (void *) MP_MAGIC(m),
			    __builtin_return_address(0));
		}
		return 0;
	}
	return 1;
}

void wlan_mutex_destroy(kmutex_t *m)
{
	if (m == NULL || MP_MAGIC(m) != (void *) WLAN_MUTEX_MAGIC) {
		return;
	}
	osMutexDelete((osMutexId_t) MP_ID(m));
	memset(m->hm_priv, 0, sizeof(m->hm_priv));
}

/* Internal helpers: release every recursion level of a held mutex and restore
 * the same depth afterwards. Used by the condition variables and the sleeps. */
static void mutex_release_all(kmutex_t *m)
{
	while ((uintptr_t) MP_DEPTH(m) > 0) {
		wlan_mutex_exit(m);
	}
}

static void mutex_acquire_depth(kmutex_t *m, unsigned int depth)
{
	unsigned int i;

	for (i = 0; i < depth; i++) {
		wlan_mutex_enter(m);
	}
}

/* ------------------------------------------------------------------ */
/* Condition variables                                                 */
/* ------------------------------------------------------------------ */

/* hc_priv[] layout: [0] osSemaphoreId_t, [1] waiter count. */
#define CP_SEM(c)        ((osSemaphoreId_t) (c)->hc_priv[0])
#define CP_SEM_SET(c, s) ((c)->hc_priv[0] = (void *) (s))
#define CP_WAITERS(c)    ((uintptr_t) (c)->hc_priv[1])
#define CP_WAITERS_SET(c, n) ((c)->hc_priv[1] = (void *) (uintptr_t) (n))

int wlan_cv_init(kcondvar_t *cv, int flags)
{
	osSemaphoreId_t sem;

	(void) flags;
	sem = osSemaphoreNew(0xffffffffu, 0, NULL);
	if (sem == NULL) {
		return ENOMEM;
	}
	memset(cv->hc_priv, 0, sizeof(cv->hc_priv));
	CP_SEM_SET(cv, sem);
	return 0;
}

static int cv_wait_common(kcondvar_t *cv, kmutex_t *m, uint32_t timeout_ms)
{
	unsigned int depth = (unsigned int) (uintptr_t) MP_DEPTH(m);
	osStatus_t st;

	CP_WAITERS_SET(cv, CP_WAITERS(cv) + 1);
	mutex_release_all(m);
	st = osSemaphoreAcquire(CP_SEM(cv), timeout_ms);
	CP_WAITERS_SET(cv, CP_WAITERS(cv) - 1);
	mutex_acquire_depth(m, depth);
	return st == osOK ? 0 : ETIMEDOUT;
}

int wlan_cv_wait(kcondvar_t *cv, kmutex_t *m)
{
	return cv_wait_common(cv, m, osWaitForever);
}

int wlan_cv_timedwait(kcondvar_t *cv, kmutex_t *m, int ticks)
{
	uint32_t ms;

	if (ticks <= 0) {
		return cv_wait_common(cv, m, osWaitForever);
	}
	ms = (uint32_t) (((uint64_t) ticks * 1000u) / (uint64_t) hz);
	if (ms == 0) {
		ms = 1;
	}
	return cv_wait_common(cv, m, ms);
}

int wlan_cv_broadcast(kcondvar_t *cv)
{
	uintptr_t waiters = CP_WAITERS(cv);

	while (waiters-- > 0) {
		osSemaphoreRelease(CP_SEM(cv));
	}
	return 0;
}

void wlan_cv_destroy(kcondvar_t *cv)
{
	osSemaphoreDelete(CP_SEM(cv));
	memset(cv->hc_priv, 0, sizeof(cv->hc_priv));
}

/* ------------------------------------------------------------------ */
/* Port serializer (the splnet() replacement)                          */
/* ------------------------------------------------------------------ */

static kmutex_t serializer;
static unsigned int serializer_suspended_depth;

void wlan_port_serializer_lock(void)
{
	wlan_mutex_enter(&serializer);
}

void wlan_port_serializer_unlock(void)
{
	wlan_mutex_exit(&serializer);
}

/* Release the serializer completely around a blocking wait and take it back
 * afterwards (the imported code assumes splnet() stays "held" across its
 * sleeps; a recursive lock that is fully released is the portable equivalent). */
void wlan_port_serializer_suspend(void)
{
	serializer_suspended_depth =
	    (unsigned int) (uintptr_t) MP_DEPTH(&serializer);
	while ((uintptr_t) MP_DEPTH(&serializer) > 0) {
		wlan_mutex_exit(&serializer);
	}
}

void wlan_port_serializer_resume(void)
{
	unsigned int i;

	for (i = 0; i < serializer_suspended_depth; i++) {
		wlan_mutex_enter(&serializer);
	}
	serializer_suspended_depth = 0;
}

/* ------------------------------------------------------------------ */
/* Interrupt-priority shells: scheduling barriers in name only         */
/* ------------------------------------------------------------------ */

ipl_t splraiseipl(ipl_t ipl)
{
	return ipl;
}

ipl_t splnet(void)
{
	return 0;
}

ipl_t splsoftserial(void)
{
	return 0;
}

void splx(ipl_t ipl)
{
	(void) ipl;
}

bool cpu_intr_p(void)
{
	return false;
}

bool cpu_softintr_p(void)
{
	return false;
}

void spl_raise_ipi(void)
{
}

/* Deferred work hook: no consumer in the current compile set; the driver round
 * wires this to the port worker (recorded in the port DIVERGENCES ledger). */
void *softint_establish(int flags, void (*func)(void *), void *arg)
{
	(void) flags;
	(void) func;
	(void) arg;
	return NULL;
}

void softint_schedule(void *sih)
{
	(void) sih;
}

/* ------------------------------------------------------------------ */
/* Bounded identified sleeps (tsleep/wakeup)                           */
/* ------------------------------------------------------------------ */

#define WLAN_SLEEP_SLOTS 8

struct sleep_slot {
	void *ident;
	osSemaphoreId_t sem;
};

static struct sleep_slot sleep_slots[WLAN_SLEEP_SLOTS];

int tsleep(void *ident, int pri, const char *wmesg, int timo)
{
	struct sleep_slot *slot = NULL;
	uint32_t timeout_ms = osWaitForever;
	osStatus_t st;
	int i;

	(void) pri;
	(void) wmesg;

	for (i = 0; i < WLAN_SLEEP_SLOTS; i++) {
		if (sleep_slots[i].ident == NULL ||
		    sleep_slots[i].ident == ident) {
			slot = &sleep_slots[i];
			break;
		}
	}
	if (slot == NULL) {
		return ENOMEM;
	}
	slot->ident = ident;
	if (timo > 0) {
		timeout_ms = (uint32_t) (((uint64_t) timo * 1000u) /
		    (uint64_t) hz);
		if (timeout_ms == 0) {
			timeout_ms = 1;
		}
	}

	wlan_port_serializer_suspend();
	st = osSemaphoreAcquire(slot->sem, timeout_ms);
	wlan_port_serializer_resume();

	slot->ident = NULL;
	return st == osOK ? 0 : EWOULDBLOCK;
}

void wakeup(void *ident)
{
	int i;

	for (i = 0; i < WLAN_SLEEP_SLOTS; i++) {
		if (sleep_slots[i].ident == ident) {
			osSemaphoreRelease(sleep_slots[i].sem);
		}
	}
}

void wakeup_one(void *ident)
{
	int i;

	for (i = 0; i < WLAN_SLEEP_SLOTS; i++) {
		if (sleep_slots[i].ident == ident) {
			osSemaphoreRelease(sleep_slots[i].sem);
			return;
		}
	}
}

int uimin(int a, int b)
{
	return a < b ? a : b;
}

int uimax(int a, int b)
{
	return a > b ? a : b;
}

/* ------------------------------------------------------------------ */
/* Copies: no user/kernel split on a port                              */
/* ------------------------------------------------------------------ */

int copyin(const void *from, void *to, size_t len)
{
	memcpy(to, from, len);
	return 0;
}

int copyout(const void *from, void *to, size_t len)
{
	memcpy(to, from, len);
	return 0;
}

int copystr(const void *from, void *to, size_t maxlen, size_t *done)
{
	size_t i;
	const char *s = from;
	char *d = to;

	for (i = 0; i < maxlen; i++) {
		d[i] = s[i];
		if (s[i] == '\0') {
			if (done != NULL) {
				*done = i;
			}
			return 0;
		}
	}
	return ENAMETOOLONG;
}

/* ------------------------------------------------------------------ */
/* Randomness (software source; the memory subsystem may replace it)   */
/* ------------------------------------------------------------------ */

static uint64_t rng_state = 0x9e3779b97f4a7c15ull;

void cprng_fast(void *buf, size_t len)
{
	uint8_t *p = buf;

	while (len-- > 0) {
		rng_state ^= rng_state << 13;
		rng_state ^= rng_state >> 7;
		rng_state ^= rng_state << 17;
		*p++ = (uint8_t) (rng_state >> 24);
	}
}

/* ------------------------------------------------------------------ */
/* Allocation wrappers: one seam behind every name                     */
/* ------------------------------------------------------------------ */

void *wlan_kmalloc(size_t size, int flags, int type)
{
	(void) flags;
	(void) type;
	return (flags & M_ZERO) != 0 ? wlan_osal_zalloc(size)
				     : wlan_osal_alloc(size);
}

void wlan_kfree(void *p, int type)
{
	(void) type;
	wlan_osal_free(p);
}

void *kmem_alloc(size_t size, int flags)
{
	(void) flags;
	return wlan_osal_alloc(size);
}

void *kmem_zalloc(size_t size, int flags)
{
	(void) flags;
	return wlan_osal_zalloc(size);
}

void kmem_free(void *p, size_t size)
{
	(void) size;
	wlan_osal_free(p);
}

void *kmem_intr_alloc(size_t size, int flags)
{
	(void) flags;
	return wlan_osal_alloc(size);
}

void *kmem_intr_zalloc(size_t size, int flags)
{
	(void) flags;
	return wlan_osal_zalloc(size);
}

void kmem_intr_free(void *p, size_t size)
{
	(void) size;
	wlan_osal_free(p);
}

/* ------------------------------------------------------------------ */
/* Output and panic                                                    */
/* ------------------------------------------------------------------ */

#ifdef WLAN_PORT_TARGET

/* The target's line sink: format, then hand to the board log (which owns the
 * stamp and the CR LF discipline). `emergency` picks the fault-path outlet,
 * which takes no print lock. */
static int wlan_osal_vlog(int emergency, const char *fmt, va_list ap)
{
	char line[192];
	size_t n;

	n = (size_t)vsnprintf(line, sizeof(line), fmt, ap);
	/* The line is terminated here: not every caller's format string ends
	 * with a newline, and the console splits a line only at its length cap -
	 * an unterminated print glues onto whatever is written next, and the
	 * split then cuts a line (a judged anchor included) in half. */
	while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
		line[--n] = '\0';
	if (emergency)
		board_log_emergency("%s\n", line);
	else
		LOG_INFO("%s\n", line);
	return (int)n;
}

void wlan_osal_log(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)wlan_osal_vlog(0, fmt, ap);
	va_end(ap);
}

int kprintf(const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = wlan_osal_vlog(0, fmt, ap);
	va_end(ap);
	return n;
}

/* The imported sources' continuation-print family (if_printf and the debug
 * dump helpers call vprintf directly). On the target it is the same sink as
 * kprintf; the host build leaves the name to the C library. */
int vprintf(const char *fmt, va_list ap)
{

	return wlan_osal_vlog(0, fmt, ap);
}

/* the port's own level-1 outlet: the board's emergency sink where one exists,
 * the port's logger everywhere else - a diagnostic must not add a board-only
 * dependency to a path every link takes */
static void
osal_emergency(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)wlan_osal_vlog(1, fmt, ap);
	va_end(ap);
}

void panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)wlan_osal_vlog(1, fmt, ap);
	va_end(ap);
	/*
	 * The caller's chain, so a parked board still names where the check
	 * fired. The imported code's assertions (the node table's lock, the
	 * channel checks) reach here from among hundreds of call sites, and the
	 * chain exists only on the board; the raw window resolves offline
	 * against the image's own ELF, the same way the exception handler's
	 * dump does.
	 */
	{
		unsigned long *sp = (unsigned long *)__builtin_frame_address(0);
		unsigned int i;

		for (i = 0; i < 16; i++) {
			board_log_emergency("net80211 panic stack+%02x=%08lx",
			    i * 8u, (unsigned long)sp[i]);
		}
	}
	board_log_emergency("net80211 port panic: parked; power-cycle to recover");
	for (;;)
		ARCH_WFE();
}

#elif defined(WLAN_PORT_LINUX)

void wlan_osal_log(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintk(fmt, ap);
	va_end(ap);
}

int kprintf(const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vprintk(fmt, ap);
	va_end(ap);
	return n;
}

/* the imported sources' continuation-print family; the kernel has no vprintf */
int vprintf(const char *fmt, va_list ap)
{

	return vprintk(fmt, ap);
}

/* emergency outlet: same kernel log, one full line; no board fault sink here */
static void
osal_emergency(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintk(fmt, ap);
	va_end(ap);
	printk("\n");
}

void panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintk(fmt, ap);
	va_end(ap);
	printk("\nnet80211 port panic: parking the task\n");
	BUG();
}

#else /* host build */

void wlan_osal_log(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
}

int kprintf(const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vfprintf(stdout, fmt, ap);
	va_end(ap);
	return n;
}

/* the port's own level-1 outlet: the board's emergency sink where one exists,
 * the port's logger everywhere else - a diagnostic must not add a board-only
 * dependency to a path every link takes */
static void
osal_emergency(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fputc('\n', stdout);
}

void panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fputs("net80211 port panic: ", stdout);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fputc('\n', stdout);
	fflush(stdout);
	abort();
}

#endif /* WLAN_PORT_TARGET */

/* ------------------------------------------------------------------ */
/* Single-context placeholder for the process shell                    */
/* ------------------------------------------------------------------ */

struct proc {
	int placeholder;
};

struct proc proc0_holder;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static ONCE_DECL(osal_once);

static void osal_init_once(void)
{
	uint32_t freq = osKernelGetTickFreq();
	int i;

	if (freq != 0) {
		hz = (int) freq;
	}
	wlan_mutex_init(&serializer, MUTEX_DEFAULT, IPL_NET);
	for (i = 0; i < WLAN_SLEEP_SLOTS; i++) {
		sleep_slots[i].ident = NULL;
		sleep_slots[i].sem = osSemaphoreNew(1, 0, NULL);
	}
}

void wlan_osal_init(void)
{
	RUN_ONCE(&osal_once, osal_init_once);
}
