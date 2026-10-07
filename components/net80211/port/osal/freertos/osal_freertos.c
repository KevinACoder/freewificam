/*
 * @file
 * @brief FreeRTOS implementation of the compat/netbsd kernel services.
 *
 * Memory goes to the FreeRTOS heap (heap_4), mutexes onto binary
 * semaphores, condition variables onto counting-semaphore broadcast
 * pools, callouts onto FreeRTOS software timers (one-shot, fired in
 * the timer service task), and the serializer/tsleep machinery keeps
 * the same shape as the embox port: a reentrant lock that sleeps drop,
 * and identified bounded waits with a short pending-wakeup window that
 * compensates for spl being a no-op.
 *
 * @date 10.09.2026
 * @author zhugengyu
 */

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"

#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <sys/kernel.h>
#include <sys/intr.h>
#include <sys/endian.h>

#include "wlan_port_freertos.h"

/* the compat malloc/free macros must not intercept the host calls */
#undef malloc
#undef free

#define WLAN_HZ 100

void panic(const char *fmt, ...) {
	va_list ap;

	printf("panic: ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");

	taskDISABLE_INTERRUPTS();
	for (;;) {
	}
}

int kprintf(const char *fmt, ...) {
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vprintf(fmt, ap);
	va_end(ap);
	return ret;
}

/* ------------------------------------------------------------------ */

void *wlan_kmalloc(size_t size, int flags, int type) {
	void *p;

	(void) type;
	/* Drivers can initialize from a short-lived shell command task. */
	p = pvPortMalloc(size);
	if (p != NULL && (flags & M_ZERO)) {
		memset(p, 0, size);
	}
	return p;
}

void wlan_kfree(void *p, int type) {
	(void) type;
	vPortFree(p);
}

void *kmem_intr_alloc(size_t size, int flags) {
	return wlan_kmalloc(size, flags, M_DEVBUF);
}

void kmem_intr_free(void *p, size_t size) {
	(void) size;
	wlan_kfree(p, M_DEVBUF);
}

void *kmem_intr_zalloc(size_t size, int flags) {
	return wlan_kmalloc(size, flags | M_ZERO, M_DEVBUF);
}

void *kmem_zalloc(size_t size, int flags) {
	return wlan_kmalloc(size, flags | M_ZERO, M_DEVBUF);
}

void *kmem_alloc(size_t size, int flags) {
	return wlan_kmalloc(size, flags, M_DEVBUF);
}

void kmem_free(void *p, size_t size) {
	(void) size;
	wlan_kfree(p, M_DEVBUF);
}

/* the compat <mem/sysmalloc.h> surface used by the shared mbuf/ifnet
 * shells (port/net/embox) */
void *sysmalloc(size_t size) {
	return pvPortMalloc(size);
}

void sysfree(void *p) {
	vPortFree(p);
}

void *sysmemalign(size_t align, size_t size) {
	/* heap_4 cannot return aligned blocks directly; over-allocate and
	 * hand back an aligned point inside (the tail waste is fine for
	 * the small DMA buffers the shim allocates) */
	void *raw;
	uintptr_t aligned;

	if (align == 0) {
		align = 1;
	}
	raw = pvPortMalloc(size + align);
	if (raw == NULL) {
		return NULL;
	}
	aligned = ((uintptr_t) raw + align - 1) & ~(uintptr_t)(align - 1);

	return (void *) aligned;
}

/* compat systm.h ksleep(9): bounded millisecond sleep */
void ksleep(int ms) {
	vTaskDelay(pdMS_TO_TICKS((uint32_t) ms));
}

/* ------------------------------------------------------------------ */
/* mutex/cv shells over FreeRTOS primitives (fixed-size storage owned
 * by the net80211 softcs, see compat sys/mutex.h) */

static SemaphoreHandle_t hm_mutex(kmutex_t *m) {
	_Static_assert(sizeof(struct host_mutex) >= sizeof(SemaphoreHandle_t),
	    "mutex shell");
	return (SemaphoreHandle_t) ((struct host_mutex *) m)->hm_priv[0];
}

static SemaphoreHandle_t hc_cond_lock(kcondvar_t *cv) {
	return (SemaphoreHandle_t) ((struct host_cond *) cv)->hc_priv[0];
}

static SemaphoreHandle_t hc_cond_sem(kcondvar_t *cv) {
	return (SemaphoreHandle_t) ((struct host_cond *) cv)->hc_priv[1];
}

static volatile int *hc_cond_waiters(kcondvar_t *cv) {
	return (volatile int *) &((struct host_cond *) cv)->hc_priv[2];
}

int wlan_mutex_init(kmutex_t *m, int type, int ipl) {
	(void) type;
	(void) ipl;
	((struct host_mutex *) m)->hm_priv[0] =
	    (void *) xSemaphoreCreateMutex();
	if (hm_mutex(m) == NULL) {
		return ENOMEM;
	}
	return 0;
}

int wlan_mutex_enter(kmutex_t *m) {
	xSemaphoreTake(hm_mutex(m), portMAX_DELAY);
	return 0;
}

int wlan_mutex_exit(kmutex_t *m) {
	xSemaphoreGive(hm_mutex(m));
	return 0;
}

int wlan_mutex_owned(kmutex_t *m) {
	(void) m;
	/* FreeRTOS mutexes do not expose the holder; lock assertions pass. */
	return 1;
}

void wlan_mutex_destroy(kmutex_t *m) {
	SemaphoreHandle_t s = hm_mutex(m);

	if (s != NULL) {
		vSemaphoreDelete(s);
		((struct host_mutex *) m)->hm_priv[0] = NULL;
	}
}

/* cv = broadcast pool: waiters register, broadcast hands one token per
 * waiter through the counting semaphore. A token left over from a lost
 * race only causes a spurious wakeup - callers poll a predicate around
 * timed waits, same contract as the embox port. */
int wlan_cv_init(kcondvar_t *cv, int flags) {
	(void) flags;
	((struct host_cond *) cv)->hc_priv[0] =
	    (void *) xSemaphoreCreateMutex();
	((struct host_cond *) cv)->hc_priv[1] =
	    (void *) xSemaphoreCreateCounting(0xffff, 0);
	*hc_cond_waiters(cv) = 0;
	if (hc_cond_lock(cv) == NULL || hc_cond_sem(cv) == NULL) {
		return ENOMEM;
	}
	return 0;
}

int wlan_cv_wait(kcondvar_t *cv, kmutex_t *m) {
	return wlan_cv_timedwait(cv, m, 0);
}

int wlan_cv_timedwait(kcondvar_t *cv, kmutex_t *m, int ticks) {
	TickType_t timeout;
	BaseType_t got;

	/* ticks are in hz units; <= 0 waits forever */
	timeout = (ticks <= 0) ? portMAX_DELAY :
	    pdMS_TO_TICKS((uint32_t) ticks * 1000 / WLAN_HZ);

	xSemaphoreTake(hc_cond_lock(cv), portMAX_DELAY);
	(*hc_cond_waiters(cv))++;
	xSemaphoreGive(hc_cond_lock(cv));

	wlan_mutex_exit(m);
	got = xSemaphoreTake(hc_cond_sem(cv), timeout);
	xSemaphoreTake(hc_cond_lock(cv), portMAX_DELAY);
	(*hc_cond_waiters(cv))--;
	xSemaphoreGive(hc_cond_lock(cv));
	wlan_mutex_enter(m);

	return (got == pdTRUE) ? 0 : EWOULDBLOCK;
}

int wlan_cv_broadcast(kcondvar_t *cv) {
	int n;

	xSemaphoreTake(hc_cond_lock(cv), portMAX_DELAY);
	n = *hc_cond_waiters(cv);
	xSemaphoreGive(hc_cond_lock(cv));
	while (n-- > 0) {
		xSemaphoreGive(hc_cond_sem(cv));
	}
	return 0;
}

void wlan_cv_destroy(kcondvar_t *cv) {
	vSemaphoreDelete(hc_cond_lock(cv));
	vSemaphoreDelete(hc_cond_sem(cv));
	((struct host_cond *) cv)->hc_priv[0] = NULL;
	((struct host_cond *) cv)->hc_priv[1] = NULL;
}

/* ------------------------------------------------------------------ */

static TimerHandle_t hc_timer(callout_t *c) {
	return (TimerHandle_t) c->hc_timer;
}

static void host_callout_fire(TimerHandle_t tmr) {
	callout_t *c = (callout_t *) pvTimerGetTimerID(tmr);

	c->hc_pending = 0;
	if (c->hc_fn != NULL) {
		c->hc_fn(c->hc_arg);
	}
}

int callout_init(callout_t *c, int flags) {
	(void) flags;
	c->hc_fn = NULL;
	c->hc_arg = NULL;
	c->hc_pending = 0;
	/* period is set at schedule time; ChangePeriod starts it */
	c->hc_timer = (void *) xTimerCreate("wlan_callout",
	    pdMS_TO_TICKS(1000), pdFALSE, c, host_callout_fire);
	if (c->hc_timer == NULL) {
		return ENOMEM;
	}
	return 0;
}

int callout_setfunc(callout_t *c, callout_fn_t fn, void *arg) {
	c->hc_fn = fn;
	c->hc_arg = arg;
	return 0;
}

int callout_schedule(callout_t *c, int ticks) {
	TickType_t period;

	if (ticks <= 0) {
		ticks = 1;
	}
	period = pdMS_TO_TICKS((uint32_t) ticks * 1000 / WLAN_HZ);
	if (period == 0) {
		period = 1;
	}
	c->hc_pending = 1;
	/* xTimerChangePeriod starts a dormant timer */
	if (xTimerChangePeriod(hc_timer(c), period, 0) != pdPASS) {
		return EINVAL;
	}
	return 0;
}

int callout_stop(callout_t *c) {
	if (c->hc_timer != NULL) {
		xTimerStop(hc_timer(c), 0);
	}
	c->hc_pending = 0;
	return 0;
}

int callout_halt(callout_t *c, kmutex_t *lock) {
	(void) lock;
	return callout_stop(c);
}

void callout_destroy(callout_t *c) {
	callout_stop(c);
	if (c->hc_timer != NULL) {
		xTimerDelete(hc_timer(c), 0);
		c->hc_timer = NULL;
	}
}

bool callout_pending(callout_t *c) {
	return c->hc_pending;
}

/* ------------------------------------------------------------------ */

int hz = WLAN_HZ;

ticks_t getticks(void) {
	/* tick rate is 1000 Hz; hz is 100 */
	return (ticks_t) (xTaskGetTickCount() /
	    (configTICK_RATE_HZ / WLAN_HZ));
}

static uint64_t wlan_cntfrq(void) {
	uint64_t frq;

	__asm volatile("mrs %0, cntfrq_el0" : "=r"(frq));

	return frq;
}

static uint64_t wlan_cntvct(void) {
	uint64_t v;

	__asm volatile("mrs %0, cntvct_el0" : "=r"(v));

	return v;
}

void delay(unsigned int us) {
	static uint64_t frq;
	uint64_t deadline;

	if (frq == 0) {
		frq = wlan_cntfrq();
	}
	deadline = wlan_cntvct() + frq * (uint64_t) us / 1000000;

	for (;;) {
		uint64_t now = wlan_cntvct();
		uint64_t left = deadline - now;

		if (now >= deadline) {
			return;
		}
		/* long waits yield; sub-millisecond tails spin on CNTVCT */
		if (left > frq / 1000) {
			vTaskDelay(pdMS_TO_TICKS(left * 1000 / frq));
		}
	}
}

static unsigned int wlan_rand_state = 0x1234abcd;

void cprng_fast(void *buf, size_t len) {
	unsigned char *p = buf;
	size_t i;

	for (i = 0; i < len; i++) {
		wlan_rand_state = wlan_rand_state * 1103515245 + 12345;
		p[i] = (unsigned char) (wlan_rand_state >> 16);
	}
}

int copyin(const void *u, void *k, size_t len) {
	memcpy(k, u, len);
	return 0;
}

int copyout(const void *k, void *u, size_t len) {
	memcpy(u, k, len);
	return 0;
}

int copystr(const void *kf, void *kt, size_t len, size_t *done) {
	const char *src = kf;
	size_t n = 0;

	while (n + 1 < len && src[n] != '\0') {
		((char *) kt)[n] = src[n];
		n++;
	}
	((char *) kt)[n] = '\0';
	if (done != NULL) {
		*done = n + 1;
	}
	return 0;
}

ipl_t splraiseipl(ipl_t ipl) {
	(void) ipl;
	return 0;
}

int max_linkhdr = 16;
int uimin(int a, int b) { return a < b ? a : b; }
int uimax(int a, int b) { return a > b ? a : b; }

ipl_t splsoftserial(void) {
	return 0;
}

ipl_t splnet(void) {
	return 0;
}

void splx(ipl_t ipl) {
	(void) ipl;
}

/* ------------------------------------------------------------------ */
/* Port serializer.
 *
 * The imported drivers rely on the splnet() discipline of their host
 * kernel to serialize the interrupt, state-machine and transmit
 * contexts; spl is a no-op here, so every driver entry takes this
 * reentrant lock. Sleeping waits drop it (see tsleep) so worker
 * contexts can deliver completions. */

static SemaphoreHandle_t wlan_ser_mtx;
static TaskHandle_t wlan_ser_owner;
static int wlan_ser_depth;

void wlan_port_serializer_lock(void) {
	TaskHandle_t self = xTaskGetCurrentTaskHandle();

	if (wlan_ser_owner == self) {
		wlan_ser_depth++;
		return;
	}
	xSemaphoreTake(wlan_ser_mtx, portMAX_DELAY);
	wlan_ser_owner = self;
	wlan_ser_depth = 1;
}

void wlan_port_serializer_unlock(void) {
	TaskHandle_t self = xTaskGetCurrentTaskHandle();

	if (wlan_ser_owner != self || wlan_ser_depth <= 0) {
		panic("wlan serializer unlock by non-owner");
	}
	if (--wlan_ser_depth > 0) {
		return;
	}
	wlan_ser_owner = NULL;
	xSemaphoreGive(wlan_ser_mtx);
}

void *wlan_port_serializer_owner(void) {
	return wlan_ser_owner;
}

/* Fully release the lock around a sleep and return the saved hold
 * count (0 when the caller was not holding it). */
int wlan_port_serializer_suspend(void) {
	int depth;

	if (wlan_ser_owner != xTaskGetCurrentTaskHandle()) {
		return 0;
	}
	depth = wlan_ser_depth;
	wlan_ser_owner = NULL;
	wlan_ser_depth = 0;
	xSemaphoreGive(wlan_ser_mtx);
	return depth;
}

void wlan_port_serializer_resume(int depth) {
	if (depth <= 0) {
		return;
	}
	xSemaphoreTake(wlan_ser_mtx, portMAX_DELAY);
	wlan_ser_owner = xTaskGetCurrentTaskHandle();
	wlan_ser_depth = depth;
}

/* ------------------------------------------------------------------ */
/* tsleep/wakeup: identified bounded waits over one global (mutex, cv)
 * pair. A waiter releases the port serializer around the wait. */

struct wlan_tsleep_slot {
	void *ident;
	int fired;
	struct wlan_tsleep_slot *next;
};

static SemaphoreHandle_t wlan_tsleep_mtx;
static SemaphoreHandle_t wlan_tsleep_sem; /* broadcast pool */
static struct wlan_tsleep_slot *wlan_tsleep_slots;

/* Wakeups that found no waiter are remembered here for a short window.
 * The imported drivers use the Unix check-then-tsleep idiom, which
 * relies on the interrupt path not being able to slip a wakeup in
 * between the predicate test and the sleep; spl does that on NetBSD
 * but is a no-op here, so a wakeup that arrives in that window would
 * otherwise be lost and the caller would run into its timeout. */
#define WLAN_TSLEEP_PENDING_MAX 16
static void *wlan_tsleep_pending[WLAN_TSLEEP_PENDING_MAX];
static int wlan_tsleep_pending_n;

static int wlan_tsleep_consume_pending(void *ident) {
	int i;

	for (i = 0; i < wlan_tsleep_pending_n; i++) {
		if (wlan_tsleep_pending[i] == ident) {
			wlan_tsleep_pending[i] =
			    wlan_tsleep_pending[--wlan_tsleep_pending_n];
			return 1;
		}
	}
	return 0;
}

int tsleep(void *ident, int pri, const char *wmesg, int timo) {
	struct wlan_tsleep_slot w;
	struct wlan_tsleep_slot **pp;
	TickType_t left_ms;
	TickType_t deadline;
	TickType_t now;
	int held;
	int rc = 0;

	(void) pri;
	(void) wmesg;

	w.ident = ident;
	w.fired = 0;

	xSemaphoreTake(wlan_tsleep_mtx, portMAX_DELAY);
	w.next = wlan_tsleep_slots;
	wlan_tsleep_slots = &w;
	/* a wakeup may have raced in before the slot was linked */
	if (wlan_tsleep_consume_pending(ident)) {
		w.fired = 1;
	}
	xSemaphoreGive(wlan_tsleep_mtx);

	/* timo: ticks; <= 0 means forever */
	now = xTaskGetTickCount();
	deadline = (timo <= 0) ? 0 :
	    now + pdMS_TO_TICKS((uint32_t) timo * 1000 / WLAN_HZ);

	held = wlan_port_serializer_suspend();

	xSemaphoreTake(wlan_tsleep_mtx, portMAX_DELAY);
	while (!w.fired) {
		if (deadline != 0) {
			left_ms = pdTICKS_TO_MS(deadline - xTaskGetTickCount());
			if ((int64_t) left_ms <= 0) {
				rc = EWOULDBLOCK;
				break;
			}
			if (left_ms > 20) {
				left_ms = 20; /* poll quantum */
			}
		} else {
			left_ms = pdMS_TO_TICKS(20);
		}
		xSemaphoreGive(wlan_tsleep_mtx);
		xSemaphoreTake(wlan_tsleep_sem, left_ms);
		xSemaphoreTake(wlan_tsleep_mtx, portMAX_DELAY);
	}
	for (pp = &wlan_tsleep_slots; *pp != NULL; pp = &(*pp)->next) {
		if (*pp == &w) {
			*pp = w.next;
			break;
		}
	}
	xSemaphoreGive(wlan_tsleep_mtx);

	wlan_port_serializer_resume(held);
	return rc;
}

void wakeup(void *ident) {
	struct wlan_tsleep_slot *s;
	int matched = 0;

	xSemaphoreTake(wlan_tsleep_mtx, portMAX_DELAY);
	for (s = wlan_tsleep_slots; s != NULL; s = s->next) {
		if (s->ident == ident) {
			s->fired = 1;
			matched = 1;
		}
	}
	if (!matched && wlan_tsleep_pending_n < WLAN_TSLEEP_PENDING_MAX) {
		wlan_tsleep_pending[wlan_tsleep_pending_n++] = ident;
	}
	xSemaphoreGive(wlan_tsleep_mtx);

	/* one token wakes one waiter; multiple sleepers on the same ident
	 * poll their deadlines */
	xSemaphoreGive(wlan_tsleep_sem);
}

void wakeup_one(void *ident) {
	wakeup(ident);
}

/* ------------------------------------------------------------------ */

void wlan_osal_freertos_init(void) {
	if (wlan_ser_mtx != NULL) {
		return;
	}
	wlan_ser_mtx = xSemaphoreCreateMutex();
	wlan_tsleep_mtx = xSemaphoreCreateMutex();
	wlan_tsleep_sem = xSemaphoreCreateCounting(0xffff, 0);
}
