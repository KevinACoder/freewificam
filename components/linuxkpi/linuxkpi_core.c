/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * linuxkpi core implementation - the stateful half of linuxkpi_core.h,
 * compiled in the port's OSAL world.
 *
 * Ported from the prior experiment line ([EXPL], NetBSD compat OSAL) onto
 * the freewebcam base (SPEC S6/linuxkpi §3.9): kmem = tlsf heap, mutex =
 * CMSIS-RTOS2 osMutex (cmsis2_threadx.c, the [S7a]-precedent face),
 * completion/wait-queue wake channel = embedded ThreadX counting
 * semaphores, callouts = ThreadX timers, worker = osThreadNew.
 *
 * Concurrency model (unchanged, board-proven on the prior line):
 *   - driver code only ever runs at passive level (the lkpi worker
 *     thread, this file's drain/callout contexts); spinlocks are passive
 *     mutexes.
 *   - the generic pending-work list is touched only from passive
 *     contexts, so a mutex guards it.  IRQ-context producers (the USB
 *     completions) never come through here: linuxkpi_usb.c defers them
 *     behind its own interrupt-safe queue first.
 *   - the queued flag is cleared with the pending list still locked -
 *     the 2026-09-24 board lesson (clearing it after the unlock lost
 *     completions until the next unrelated event re-queued the work).
 */

#include "linuxkpi_impl.h"

#include <linux/firmware.h>

/* ------------------------------------------------------------------ */
/* allocation (tlsf - the one-allocator discipline, [S7b] FR-10 family) */

void *
lkpi_kmem_alloc(size_t size, int flags, bool zero)
{
	void *p;

	/* GFP_* only selects wait-or-fail; the tlsf heap never sleeps */
	(void)flags;
	p = pvPortMalloc(size);
	if (p != NULL && zero)
		memset(p, 0, size);
	return p;
}

void
lkpi_kmem_free(const void *ptr)
{

	if (ptr == NULL)
		return;
	vPortFree((void *)(uintptr_t)ptr);
}

/* ------------------------------------------------------------------ */
/* logging / fatal                                                     */
/* ------------------------------------------------------------------ */

int
lkpi_vprintf(const char *fmt, va_list ap)
{
	static char buf[256];
	int n;

	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	board_log("%s", buf);
	return n;
}

int
lkpi_printf(const char *fmt, ...)
{
	va_list ap;
	int n;

	/* the stamped line assembler (board_log) owns the wire format:
	 * one line, one stamp, exactly one CRLF (print discipline, S1
	 * §3.4.2) */
	va_start(ap, fmt);
	n = lkpi_vprintf(fmt, ap);
	va_end(ap);
	return n;
}

void
lkpi_dev_printk(struct device *dev, const char *fmt, ...)
{
	static char buf[256];
	const char *prefix = dev != NULL && dev->name != NULL ?
	    dev->name : "lkpi";
	va_list ap;

	/*
	 * The dist print helpers funnel everything through dev_*(); the
	 * debug paths use the Linux "%pV" wrapped-format convention, which
	 * the port printf does not understand - without the special case
	 * it prints the pointer instead of the message.
	 */
	if (strstr(fmt, "%pV") != NULL) {
		const struct va_format *vaf;

		va_start(ap, fmt);
		vaf = va_arg(ap, const struct va_format *);
		va_end(ap);
		lkpi_printf("%s: ", prefix);
		lkpi_vprintf(vaf->fmt, *vaf->va);
		return;
	}
	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	lkpi_printf("%s: %s", prefix, buf);
}

void
lkpi_fatal(const char *what)
{

	/* no panic path in this image: leave the evidence on the console
	 * and park (the gdb attach or the reboot command recovers) */
	lkpi_printf("lkpi: FATAL %s - parked\n", what);
	for (;;)
		__asm__ __volatile__("wfe");
}

/* ------------------------------------------------------------------ */
/* atomics                                                             */
/* ------------------------------------------------------------------ */

void
lkpi_atomic_inc(atomic_t *v)
{

	(void)__atomic_fetch_add(&v->counter, 1, __ATOMIC_RELAXED);
}

void
lkpi_atomic_dec(atomic_t *v)
{

	(void)__atomic_fetch_add(&v->counter, -1, __ATOMIC_RELAXED);
}

int
lkpi_atomic_inc_return(atomic_t *v)
{

	return (int)__atomic_add_fetch(&v->counter, 1, __ATOMIC_RELAXED);
}

int
lkpi_atomic_dec_return(atomic_t *v)
{

	return (int)__atomic_sub_fetch(&v->counter, 1, __ATOMIC_RELAXED);
}

int
lkpi_atomic_dec_and_test(atomic_t *v)
{

	return lkpi_atomic_dec_return(v) == 0;
}

void
lkpi_atomic_add(int i, atomic_t *v)
{

	(void)__atomic_fetch_add(&v->counter, i, __ATOMIC_RELAXED);
}

void
lkpi_atomic_sub(int i, atomic_t *v)
{

	(void)__atomic_fetch_add(&v->counter, -i, __ATOMIC_RELAXED);
}

/* ------------------------------------------------------------------ */
/* random                                                              */
/* ------------------------------------------------------------------ */

static uint32_t lkpi_rand_state;

static uint32_t
lkpi_rand(void)
{

	if (lkpi_rand_state == 0)
		lkpi_rand_state = (uint32_t)tx_time_get() | 1;
	lkpi_rand_state ^= lkpi_rand_state << 13;
	lkpi_rand_state ^= lkpi_rand_state >> 17;
	lkpi_rand_state ^= lkpi_rand_state << 5;
	return lkpi_rand_state;
}

long
lkpi_get_random_long(void)
{
	return (long)(lkpi_rand() & 0x7fffffff);
}

unsigned int
lkpi_get_random_int(void)
{
	return lkpi_rand();
}

unsigned char
lkpi_get_random_u8(void)
{
	return (unsigned char)(lkpi_rand() >> 24);
}

/* real function, not a macro: some call sites never include random.h */
unsigned int
get_random_int(void)
{
	return lkpi_rand();
}

void
lkpi_get_random_mask_addr(u8 *buf, const u8 *addr, const u8 *mask)
{
	int i;

	for (i = 0; i < 6; i++)
		buf[i] = (addr[i] & mask[i]) |
		    ((u8)lkpi_rand() & ~mask[i]);
}

void
lkpi_prandom_bytes(void *buf, size_t n)
{
	u8 *p = buf;
	size_t i;

	for (i = 0; i < n; i++)
		p[i] = (u8)(lkpi_rand() >> 16);
}

/* ------------------------------------------------------------------ */
/* string helpers                                                      */
/* ------------------------------------------------------------------ */

int
lkpi_strcasecmp(const char *a, const char *b)
{

	while (*a != '\0' && (*a | 0x20) == (*b | 0x20)) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)(*b | 0x20);
}

char *
lkpi_strnstr(const char *haystack, const char *needle, size_t len)
{
	size_t nlen;

	if (needle == NULL)
		return (char *)(uintptr_t)haystack;
	nlen = strlen(needle);
	if (nlen == 0)
		return (char *)(uintptr_t)haystack;
	for (; *haystack != '\0' && len >= nlen; haystack++, len--) {
		if (*haystack == *needle &&
		    strncmp(haystack, needle, nlen) == 0)
			return (char *)(uintptr_t)haystack;
	}
	return NULL;
}

unsigned long
simple_strtoul(const char *cp, char **endp, unsigned int base)
{
	unsigned long result = 0, value;

	if (base == 0) {
		if (*cp == '0' && (cp[1] == 'x' || cp[1] == 'X')) {
			base = 16;
			cp += 2;
		} else if (*cp == '0') {
			base = 8;
			cp++;
		} else {
			base = 10;
		}
	} else if (base == 16 && *cp == '0' && (cp[1] == 'x' || cp[1] == 'X')) {
		cp += 2;
	}
	while (*cp != '\0') {
		if (*cp >= '0' && *cp <= '9')
			value = (unsigned long)(*cp - '0');
		else if (*cp >= 'a' && *cp <= 'f')
			value = (unsigned long)(*cp - 'a' + 10);
		else if (*cp >= 'A' && *cp <= 'F')
			value = (unsigned long)(*cp - 'A' + 10);
		else
			break;
		if (value >= base)
			break;
		result = result * base + value;
		cp++;
	}
	if (endp != NULL)
		*endp = (char *)(uintptr_t)cp;
	return result;
}

static int
lkpi_kstrto(const char *s, unsigned int base, unsigned long max,
    unsigned long *res)
{
	unsigned long v;
	char *end;

	if (s == NULL || *s == '\0')
		return -EINVAL;
	v = simple_strtoul(s, &end, (unsigned int)base);
	if (end == s || v > max)
		return -EINVAL;
	*res = v;
	return 0;
}

int
kstrtou8(const char *s, unsigned int base, u8 *res)
{
	unsigned long v;
	int error = lkpi_kstrto(s, base, 0xff, &v);

	if (error == 0)
		*res = (u8)v;
	return error;
}

int
kstrtou16(const char *s, unsigned int base, u16 *res)
{
	unsigned long v;
	int error = lkpi_kstrto(s, base, 0xffff, &v);

	if (error == 0)
		*res = (u16)v;
	return error;
}

int
kstrtouint(const char *s, unsigned int base, unsigned int *res)
{
	unsigned long v;
	int error = lkpi_kstrto(s, base, 0xffffffffUL, &v);

	if (error == 0)
		*res = (unsigned int)v;
	return error;
}

int
kstrtobool(const char *s, bool *res)
{

	if (s == NULL)
		return -EINVAL;
	if (s[0] == '1' || s[0] == 'y' || s[0] == 'Y') {
		*res = true;
		return 0;
	}
	if (s[0] == '0' || s[0] == 'n' || s[0] == 'N') {
		*res = false;
		return 0;
	}
	return -EINVAL;
}

/* ------------------------------------------------------------------ */
/* time                                                                */
/* ------------------------------------------------------------------ */

unsigned long
lkpi_jiffies(void)
{
	/* the board tick is 1 kHz (CONFIG_TX_TICKS_PER_SECOND) and core.h's
	 * HZ rides that value: jiffies ARE milliseconds here */
	return (unsigned long)tx_time_get();
}

ktime_t
lkpi_ktime_get(void)
{
	return (ktime_t)tx_time_get() * (1000000000LL / (ktime_t)HZ);
}

/* busy-wait off the generic timer; used for the sub-tick register
 * settle delays the chip code scatters around its init sequence */
static void
lkpi_delay_us(unsigned int us)
{
	uint64_t start, ticks;
	uint64_t freq;

	__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
	__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(start));
	ticks = (us * freq) / 1000000ULL;
	if (ticks == 0)
		ticks = 1;
	for (;;) {
		uint64_t now;

		__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(now));
		if (now - start >= ticks)
			break;
	}
}

void
lkpi_udelay(unsigned int us)
{
	lkpi_delay_us(us);
}

void
lkpi_mdelay(unsigned int ms)
{
	lkpi_delay_us(ms * 1000U);
}

void
lkpi_usleep_range(unsigned int min_us, unsigned int max_us)
{
	lkpi_delay_us(max_us);
}

void
lkpi_msleep(unsigned int ms)
{

	(void)tx_thread_sleep(MAX(1, (ULONG)ms));
}

/* ------------------------------------------------------------------ */
/* mutex / spinlock (CMSIS osMutex over ThreadX)                       */
/* ------------------------------------------------------------------ */

void
lkpi_mutex_init(struct mutex *m)
{
	osMutexAttr_t attr = { .name = "lkpi_mutex",
			       .attr_bits = osMutexRecursive,
			       .cb_mem = NULL,
			       .cb_size = 0U };

	*lk_mutex(m) = osMutexNew(&attr);
	if (*lk_mutex(m) == NULL)
		lkpi_fatal("mutex init failed");
}

void
lkpi_mutex_destroy(struct mutex *m)
{

	if (*lk_mutex(m) != NULL)
		(void)osMutexDelete(*lk_mutex(m));
	*lk_mutex(m) = NULL;
}

void
lkpi_mutex_lock(struct mutex *m)
{

	(void)osMutexAcquire(*lk_mutex(m), osWaitForever);
}

void
lkpi_mutex_unlock(struct mutex *m)
{

	(void)osMutexRelease(*lk_mutex(m));
}

int
lkpi_mutex_trylock(struct mutex *m)
{

	/* a zero-timeout acquire is the trylock; on success the lock IS
	 * held, matching the Linux contract */
	return osMutexAcquire(*lk_mutex(m), 0) == osOK;
}

int
lkpi_mutex_is_locked(struct mutex *m)
{

	return osMutexAcquire(*lk_mutex(m), 0) == osErrorResource;
}

/* ------------------------------------------------------------------ */
/* sk_buff                                                             */
/* ------------------------------------------------------------------ */

/* TLSF-assert forensics (issues/2026-10-04-rtw88-tlsf-heap-assert):
 * a write past skb->end smashes the NEXT tlsf block header - exactly
 * the `!block_is_last` trail.  A 4-byte canary after the data area
 * turns that into a named report at free time; the counters also
 * settle whether the "OOM 32904 bytes" flood is the rtw88 USB RX
 * alloc_skb(32K) resubmit churn. */
#define LKPI_SKB_CANARY	0xd3bada7aU

static u32 lkpi_skb_n_alloc, lkpi_skb_n_free, lkpi_skb_inflight;
static u32 lkpi_skb_big_alloc, lkpi_skb_alloc_fail, lkpi_skb_canary_bad;

void
lkpi_skb_diag_dump(void)
{

	printf("lkpi: skb alloc=%u free=%u inflight=%u big16k=%u "
	    "alloc_fail=%u canary_bad=%u\n",
	    lkpi_skb_n_alloc, lkpi_skb_n_free, lkpi_skb_inflight,
	    lkpi_skb_big_alloc, lkpi_skb_alloc_fail, lkpi_skb_canary_bad);
}

void *
lkpi_skb_alloc(unsigned int size, gfp_t gfp)
{
	struct sk_buff *skb;

	/* one allocation for the header and the data area */
	(void)gfp;
	skb = pvPortMalloc(sizeof(*skb) + size + sizeof(u32));
	if (skb == NULL) {
		lkpi_skb_alloc_fail++;
		return NULL;
	}
	memset(skb, 0, sizeof(*skb));
	skb->head = (unsigned char *)(skb + 1);
	skb->data = skb->head;
	skb->tail = 0;
	skb->end = size;
	skb->len = 0;
	skb->truesize = sizeof(*skb) + size;
	*(u32 *)(skb->head + size) = LKPI_SKB_CANARY;

	lkpi_skb_n_alloc++;
	lkpi_skb_inflight++;
	if (size >= 16384)
		lkpi_skb_big_alloc++;
	return skb;
}

void
lkpi_skb_free(struct sk_buff *skb)
{

	if (skb == NULL)
		return;
	if (skb->head != NULL && skb->end != 0 &&
	    *(u32 *)(skb->head + skb->end) != LKPI_SKB_CANARY) {
		lkpi_skb_canary_bad++;
		printf("lkpi: skb canary TRASHED size=%u val=%08x\n",
		    skb->end, *(u32 *)(skb->head + skb->end));
	}
	vPortFree(skb);
	lkpi_skb_n_free++;
	if (lkpi_skb_inflight != 0)
		lkpi_skb_inflight--;
}

void
lkpi_skb_queue_init(struct sk_buff_head *q)
{

	q->next = (struct sk_buff *)q;
	q->prev = (struct sk_buff *)q;
	q->qlen = 0;
	/* the drivers take the queue lock around list surgery */
	lkpi_mutex_init(&q->lock);
}

void
lkpi_skb_queue_head_init_lockable(struct sk_buff_head *q)
{
	lkpi_skb_queue_init(q);
}

void
lkpi_skb_queue_tail(struct sk_buff_head *q, struct sk_buff *skb)
{
	struct sk_buff *head = (struct sk_buff *)q;
	struct sk_buff *tail = head->prev;

	skb->next = head;
	skb->prev = tail;
	skb->list = q;
	tail->next = skb;
	head->prev = skb;
	q->qlen++;
}

void
lkpi_skb_queue_head(struct sk_buff_head *q, struct sk_buff *skb)
{
	struct sk_buff *head = (struct sk_buff *)q;
	struct sk_buff *first = head->next;

	skb->next = first;
	skb->prev = head;
	skb->list = q;
	first->prev = skb;
	head->next = skb;
	q->qlen++;
}

struct sk_buff *
lkpi_skb_dequeue(struct sk_buff_head *q)
{
	struct sk_buff *head = (struct sk_buff *)q;
	struct sk_buff *skb = head->next;

	if (skb == head)
		return NULL;
	lkpi_skb_unlink(skb, q);
	return skb;
}

void
lkpi_skb_unlink(struct sk_buff *skb, struct sk_buff_head *q)
{

	skb->prev->next = skb->next;
	skb->next->prev = skb->prev;
	skb->next = NULL;
	skb->prev = NULL;
	skb->list = NULL;
	if (q != NULL && q->qlen > 0)
		q->qlen--;
}

void
lkpi_skb_queue_purge(struct sk_buff_head *q)
{
	struct sk_buff *skb;

	while ((skb = lkpi_skb_dequeue(q)) != NULL)
		lkpi_skb_free(skb);
}

unsigned char *
lkpi_skb_put(struct sk_buff *skb, unsigned int len)
{
	unsigned char *tmp = skb->head + skb->tail;

	skb->tail += len;
	skb->len += len;
	if (skb->tail > skb->end)
		lkpi_fatal("skb overrun");
	return tmp;
}

void *
lkpi_skb_push(struct sk_buff *skb, unsigned int len)
{
	skb->data -= len;
	skb->len += len;
	if (skb->data < skb->head)
		lkpi_fatal("skb underrun");
	return skb->data;
}

/* ------------------------------------------------------------------ */
/* completion                                                          */
/* ------------------------------------------------------------------ */

void
lkpi_completion_init(struct completion *c)
{
	struct lkpi_cv_shell *cv = lk_ccv(c);

	*lk_cmtx(c) = osMutexNew(NULL);
	if (*lk_cmtx(c) == NULL)
		lkpi_fatal("completion mutex init failed");
	memset(cv, 0, sizeof(*cv));
	{
		UINT txret = tx_semaphore_create(&cv->sem, "lkpi_cv", 0);

		if (txret != TX_SUCCESS) {
			extern void *_tx_thread_current_ptr;
			extern unsigned long _tx_thread_system_state;

			TX_THREAD *cur = _tx_thread_current_ptr;

			lkpi_printf("lkpi: completion sem create ret=%u "
			    "cv=%p mtx=%p done=%u sysst=%lu cur=%p(%s)\n",
			    (unsigned)txret, (void *)cv, (void *)*lk_cmtx(c),
			    (unsigned)c->done,
			    (unsigned long)_tx_thread_system_state,
			    (void *)cur,
			    cur != NULL && cur->tx_thread_name != NULL ?
			      cur->tx_thread_name : "-");
			lkpi_fatal("completion semaphore init failed");
		}
	}
	c->done = 0;
}

void
lkpi_completion_reinit(struct completion *c)
{

	c->done = 0;
}

/* The init created a raw ThreadX semaphore and a cmsis2 mutex; a freed
 * carrier (urb recycling) MUST take both down, or the semaphore stays
 * on the created list as a dangling entry and the next create on the
 * recycled address returns TX_SEMAPHORE_ERROR (the "ret=12" park on
 * rtw88's second reserve-page write, 2026-10-04 W2 round; the mutex is
 * a pool slot leaking per urb on top). */
void
lkpi_completion_destroy(struct completion *c)
{
	struct lkpi_cv_shell *cv = lk_ccv(c);

	(void)tx_semaphore_delete(&cv->sem);
	if (*lk_cmtx(c) != NULL) {
		(void)osMutexDelete(*lk_cmtx(c));
		*lk_cmtx(c) = NULL;
	}
}

void
lkpi_complete(struct completion *c)
{
	struct lkpi_cv_shell *cv = lk_ccv(c);

	(void)osMutexAcquire(*lk_cmtx(c), osWaitForever);
	/* complete() after complete_all() must not disturb the sticky mark
	 * (FreeBSD linux_compat.c:2299): a plain ++ here would let the next
	 * waiter consume the sticky value itself. */
	if (c->done != LKPI_COMPLETION_STICKY)
		c->done++;
	/* wake ONE waiter: Linux complete() semantics (the semaphore token
	 * is the wake; a spurious wake just rechecks done) */
	if (cv->waiters > 0)
		(void)tx_semaphore_put(&cv->sem);
	(void)osMutexRelease(*lk_cmtx(c));
}

/*
 * complete_all is STICKY in Linux: every current and future waiter passes
 * immediately.  A plain done++ here let the probe-time firmware wait
 * consume the only token, so ops->start's second wait_for_completion
 * (rtw_wait_firmware_completion) parked forever - the R-LK3 ops->start
 * wedge, five boots, always between the power-on register tail and the
 * first DLFW op ([FWCI] §13, fix 5f43157).
 */
void
lkpi_complete_all(struct completion *c)
{
	struct lkpi_cv_shell *cv = lk_ccv(c);
	int n, i;

	(void)osMutexAcquire(*lk_cmtx(c), osWaitForever);
	c->done = LKPI_COMPLETION_STICKY;
	/* wake EVERY waiter: one token each, none consumes the sticky */
	n = cv->waiters;
	for (i = 0; i < n; i++)
		(void)tx_semaphore_put(&cv->sem);
	(void)osMutexRelease(*lk_cmtx(c));
}

void
lkpi_wait_for_completion(struct completion *c)
{
	struct lkpi_cv_shell *cv = lk_ccv(c);

	(void)osMutexAcquire(*lk_cmtx(c), osWaitForever);
	while (c->done == 0) {
		cv->waiters++;
		(void)osMutexRelease(*lk_cmtx(c));
		(void)tx_semaphore_get(&cv->sem, TX_WAIT_FOREVER);
		(void)osMutexAcquire(*lk_cmtx(c), osWaitForever);
		cv->waiters--;
	}
	/* a sticky complete_all pass does not consume (it is not a count) */
	if (c->done != LKPI_COMPLETION_STICKY)
		c->done--;
	(void)osMutexRelease(*lk_cmtx(c));
}

unsigned long
lkpi_wait_for_completion_timeout(struct completion *c, unsigned long jiff)
{
	struct lkpi_cv_shell *cv = lk_ccv(c);

	(void)osMutexAcquire(*lk_cmtx(c), osWaitForever);
	while (c->done == 0) {
		cv->waiters++;
		(void)osMutexRelease(*lk_cmtx(c));
		if (tx_semaphore_get(&cv->sem, MAX(1, (UINT)jiff)) !=
		    TX_SUCCESS) {
			(void)osMutexAcquire(*lk_cmtx(c), osWaitForever);
			cv->waiters--;
			(void)osMutexRelease(*lk_cmtx(c));
			/* timed out: the count is untouched (Linux returns
			 * 0 and the caller owns the race window) */
			return 0;
		}
		(void)osMutexAcquire(*lk_cmtx(c), osWaitForever);
		cv->waiters--;
	}
	/* same sticky rule as lkpi_wait_for_completion (5f43157): the
	 * timeout variant was missed in that fix and rtw88 runs
	 * wait_for_completion_timeout on completions that complete_all
	 * armed (fw->completion, lps_leave_check) */
	if (c->done != LKPI_COMPLETION_STICKY)
		c->done--;
	(void)osMutexRelease(*lk_cmtx(c));
	return 1;
}

int
lkpi_try_wait_for_completion(struct completion *c)
{
	int done;

	(void)osMutexAcquire(*lk_cmtx(c), osWaitForever);
	if (c->done > 0) {
		/* sticky complete_all passes without consuming */
		if (c->done != LKPI_COMPLETION_STICKY)
			c->done--;
		done = 1;
	} else {
		done = 0;
	}
	(void)osMutexRelease(*lk_cmtx(c));
	return done;
}

int
lkpi_completion_done(const struct completion *c)
{
	return c->done != 0;
}

/* plain functions, not macros: urb->complete is called through the same
 * identifier (see the note in linuxkpi_core.h) */
void
complete(struct completion *c)
{
	lkpi_complete(c);
}

void
complete_all(struct completion *c)
{
	lkpi_complete_all(c);
}

/* ------------------------------------------------------------------ */
/* workqueue                                                           */
/* ------------------------------------------------------------------ */

struct lkpi_work_item {
	struct lkpi_work_item	*next;
	struct work_struct	*work;
};

static struct lkpi_work_item *lkpi_pending_head;
static struct lkpi_work_item *lkpi_pending_tail;
static osMutexId_t lkpi_pending_mtx;
static TX_SEMAPHORE lkpi_wake_sem;
static bool lkpi_worker_started;
static bool lkpi_worker_ready;

struct workqueue_struct *lkpi_system_wq;

static void
lkpi_workqueue_worker(void *arg)
{
	struct lkpi_work_item *item;
	struct work_struct *w;

	(void)arg;
	for (;;) {
		item = NULL;
		(void)osMutexAcquire(lkpi_pending_mtx, osWaitForever);
		if (lkpi_pending_head != NULL) {
			item = lkpi_pending_head;
			lkpi_pending_head = item->next;
			if (lkpi_pending_head == NULL)
				lkpi_pending_tail = NULL;
			/* clear the queued flag with the list still locked */
			item->work->wk_queued = 0;
		}
		(void)osMutexRelease(lkpi_pending_mtx);

		if (item == NULL) {
			/* the producer's semaphore put cuts this short */
			(void)tx_semaphore_get(&lkpi_wake_sem, 1);
			continue;
		}

		w = item->work;
		vPortFree(item);

		/*
		 * Forensics carried from the 2026-09-24 board fatal (jump
		 * to 0 from this call): a corrupted func pointer means the
		 * work_struct memory was stomped or freed.  Dump the
		 * suspect instead of dying so the run keeps evidence.
		 */
		if (w == NULL || w->func == NULL) {
			const volatile uint32_t *raw =
			    (const volatile uint32_t *)(w != NULL ? w : &w);

			lkpi_printf("lkpi: work with NULL func! w=%p "
			    "raw=%08x %08x %08x %08x %08x %08x %08x %08x\n",
			    (void *)w, raw[0], raw[1], raw[2], raw[3],
			    raw[4], raw[5], raw[6], raw[7]);
			continue;
		}
		w->wk_running = 1;
		w->func(w);
		w->wk_running = 0;
	}
}

static void
lkpi_workqueue_ready(void)
{
	osThreadAttr_t attr = { .name = "lkpi_wq",
				.attr_bits = 0,
				.cb_mem = NULL,
				.cb_size = 0,
				.stack_mem = NULL,
				/* 16 KiB: rtw88's ops->start chain (power-on + reinit) runs on this
			     * worker; the prior project's shell-task stack
			     * overflowed on the same chain (M4) and 8 KiB is
			     * one deep call away from it */
				.stack_size = 16384,
				.priority = osPriorityNormal };

	if (lkpi_worker_ready)
		return;
	lkpi_worker_ready = true;
	if (tx_semaphore_create(&lkpi_wake_sem, "lkpi_wq", 0) != TX_SUCCESS)
		lkpi_fatal("wq semaphore init failed");

	lkpi_worker_started =
	    osThreadNew(lkpi_workqueue_worker, NULL, &attr) != NULL;
	if (!lkpi_worker_started) {
		lkpi_printf("lkpi: cannot start the work thread\n");
		return;
	}

	lkpi_system_wq = lkpi_alloc_workqueue("lkpi_sys", 0, 1);
}

void
lkpi_work_init(struct work_struct *w, void (*fn)(struct work_struct *))
{

	w->_wq = NULL;
	w->func = fn;
	w->wk_queued = 0;
	w->wk_running = 0;
}

static void
lkpi_work_enqueue(struct work_struct *w)
{
	struct lkpi_work_item *item;
	int zero = 0;

	if (!lkpi_worker_started)
		return;
	if (__atomic_compare_exchange_n(&w->wk_queued, &zero, 1, false,
	    __ATOMIC_RELAXED, __ATOMIC_RELAXED) == false)
		return;

	item = pvPortMalloc(sizeof(*item));
	if (item == NULL) {
		w->wk_queued = 0;
		return;
	}
	item->work = w;
	item->next = NULL;
	(void)osMutexAcquire(lkpi_pending_mtx, osWaitForever);
	if (lkpi_pending_tail != NULL)
		lkpi_pending_tail->next = item;
	else
		lkpi_pending_head = item;
	lkpi_pending_tail = item;
	(void)osMutexRelease(lkpi_pending_mtx);
	(void)tx_semaphore_put(&lkpi_wake_sem);
}

bool
lkpi_queue_work(struct workqueue_struct *wq, struct work_struct *w)
{

	(void)wq;
	lkpi_work_enqueue(w);
	return true;
}

static void
lkpi_delayed_work_timer(ULONG arg)
{
	struct delayed_work *dw = (struct delayed_work *)(uintptr_t)arg;

	dw->dw_scheduled = false;
	if (dw->dw_cancel)
		return;
	lkpi_work_enqueue(&dw->work);
}

void
lkpi_delayed_work_init(struct delayed_work *dw,
    void (*fn)(struct work_struct *))
{
	struct lkpi_callout_shell *co = lk_callout_dw(dw);

	lkpi_workqueue_ready();
	lkpi_work_init(&dw->work, fn);
	memset(co, 0, sizeof(*co));
	co->fn = NULL;
	co->arg = dw;
	if (tx_timer_create(&co->timer, "lkpi_dwork",
	    lkpi_delayed_work_timer, (ULONG)(uintptr_t)dw, 1, 0,
	    TX_NO_ACTIVATE) != TX_SUCCESS)
		lkpi_fatal("delayed_work timer init failed");
	dw->dw_scheduled = false;
	dw->dw_cancel = false;
}

bool
lkpi_queue_delayed_work(struct workqueue_struct *wq,
    struct delayed_work *dw, unsigned long delay)
{
	struct lkpi_callout_shell *co = lk_callout_dw(dw);

	/* The third parameter must NOT be named "jiffies": the shim macro
	 * (#define jiffies lkpi_jiffies()) would turn the prototype into a
	 * function-pointer parameter and every body reference compiled as
	 * an indirect call through it - the prior line's watchdog-arm
	 * wild-call lesson. */
	unsigned long ticks = delay > 0 ? delay : 1;

	(void)wq;
	(void)tx_timer_deactivate(&co->timer);
	dw->dw_scheduled = true;
	dw->dw_cancel = false;
	(void)tx_timer_change(&co->timer, (ULONG)ticks, 0);
	(void)tx_timer_activate(&co->timer);
	return true;
}

bool
lkpi_cancel_delayed_work(struct delayed_work *dw)
{
	struct lkpi_callout_shell *co = lk_callout_dw(dw);
	bool was;

	was = tx_timer_deactivate(&co->timer) == TX_SUCCESS &&
	    dw->dw_scheduled;
	dw->dw_scheduled = false;
	dw->dw_cancel = true;
	return was;
}

bool
lkpi_cancel_delayed_work_sync(struct delayed_work *dw)
{
	bool was;

	was = lkpi_cancel_delayed_work(dw);
	lkpi_work_flush(&dw->work);
	return was;
}

void
lkpi_flush_delayed_work(struct delayed_work *dw)
{
	struct lkpi_callout_shell *co = lk_callout_dw(dw);

	if (tx_timer_deactivate(&co->timer) == TX_SUCCESS)
		lkpi_work_enqueue(&dw->work);
}

void
lkpi_work_flush(struct work_struct *w)
{

	while (w->wk_queued != 0 || w->wk_running)
		(void)tx_thread_sleep(1);
}

struct workqueue_struct *
lkpi_alloc_workqueue(const char *fmt, int flags, int max_active)
{

	lkpi_workqueue_ready();
	/* the queue is a singleton; the handle is only a token */
	(void)fmt;
	(void)flags;
	(void)max_active;
	return lkpi_worker_started ?
	    (struct workqueue_struct *)&lkpi_pending_head : NULL;
}

void
lkpi_destroy_workqueue(struct workqueue_struct *wq)
{

	/* the chip code calls this from failure paths that may run on the
	 * worker itself; the thread is kept for the lifetime of the image */
	(void)wq;
}

/* ------------------------------------------------------------------ */
/* timers                                                              */
/* ------------------------------------------------------------------ */

static void
lkpi_timer_expiry(ULONG arg)
{
	struct timer_list *tl = (struct timer_list *)(uintptr_t)arg;

	tl->tl_pending = false;
	tl->tl_func(tl);
}

void
lkpi_timer_setup(struct timer_list *t, void (*fn)(struct timer_list *))
{
	struct lkpi_callout_shell *co = lk_callout_tl(t);

	lkpi_workqueue_ready();
	memset(co, 0, sizeof(*co));
	co->arg = t;
	if (tx_timer_create(&co->timer, "lkpi_timer", lkpi_timer_expiry,
	    (ULONG)(uintptr_t)t, 1, 0, TX_NO_ACTIVATE) != TX_SUCCESS)
		lkpi_fatal("timer init failed");
	t->tl_func = fn;
	t->tl_pending = false;
}

int
lkpi_mod_timer(struct timer_list *t, unsigned long expires)
{
	struct lkpi_callout_shell *co = lk_callout_tl(t);
	unsigned long now = (unsigned long)tx_time_get();
	unsigned long ticks = expires > now ? expires - now : 1;

	(void)tx_timer_deactivate(&co->timer);
	t->tl_pending = true;
	(void)tx_timer_change(&co->timer, (ULONG)ticks, 0);
	(void)tx_timer_activate(&co->timer);
	return 1;
}

void
lkpi_del_timer(struct timer_list *t)
{
	struct lkpi_callout_shell *co = lk_callout_tl(t);

	t->tl_pending = false;
	(void)tx_timer_deactivate(&co->timer);
}

void
lkpi_del_timer_sync(struct timer_list *t)
{

	lkpi_del_timer(t);
}

int
lkpi_timer_pending(const struct timer_list *t)
{
	return t->tl_pending;
}

/* ------------------------------------------------------------------ */
/* wait queues                                                         */
/* ------------------------------------------------------------------ */

void
lkpi_waitqueue_init(struct wait_queue_head *wq)
{
	struct lkpi_cv_shell *cv = lk_wqcv(wq);

	*lk_wqmutex(wq) = osMutexNew(NULL);
	if (*lk_wqmutex(wq) == NULL)
		lkpi_fatal("waitqueue mutex init failed");
	memset(cv, 0, sizeof(*cv));
	if (tx_semaphore_create(&cv->sem, "lkpi_wq_cv", 0) != TX_SUCCESS)
		lkpi_fatal("waitqueue semaphore init failed");
}

void
lkpi_waitqueue_wake(struct wait_queue_head *wq)
{
	struct lkpi_cv_shell *cv = lk_wqcv(wq);

	(void)osMutexAcquire(*lk_wqmutex(wq), osWaitForever);
	if (cv->waiters > 0)
		(void)tx_semaphore_put(&cv->sem);
	(void)osMutexRelease(*lk_wqmutex(wq));
}

void
lkpi_waitq_lock(struct wait_queue_head *wq)
{
	(void)osMutexAcquire(*lk_wqmutex(wq), osWaitForever);
}

void
lkpi_waitq_unlock(struct wait_queue_head *wq)
{
	(void)osMutexRelease(*lk_wqmutex(wq));
}

int
lkpi_waitq_timedwait(struct wait_queue_head *wq, int ms)
{
	struct lkpi_cv_shell *cv = lk_wqcv(wq);
	int woke;

	cv->waiters++;
	(void)osMutexRelease(*lk_wqmutex(wq));
	woke = tx_semaphore_get(&cv->sem, MAX(1, (UINT)ms)) == TX_SUCCESS;
	(void)osMutexAcquire(*lk_wqmutex(wq), osWaitForever);
	cv->waiters--;
	return woke ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* deferred calls (net80211 -> driver at serializer level)             */
/* ------------------------------------------------------------------ */

/*
 * net80211 calls into the driver at splnet, but the chip code sleeps, so
 * those paths are deferred here and run on the lkpi workqueue.  The work
 * item cannot be freed from inside its own callback, so deferred calls
 * come from a small recycled pool.
 */
#define	LKPI_ASYNC_CALLS	16

struct lkpi_async_call {
	struct work_struct	work;
	void			(*fn)(void *);
	void			*arg;
	volatile uint32_t	in_use;
	bool			initialised;
};

static struct lkpi_async_call lkpi_async_calls[LKPI_ASYNC_CALLS];
static unsigned int lkpi_async_call_next;

static void
lkpi_async_call_cb(struct work_struct *work)
{
	struct lkpi_async_call *call =
	    container_of(work, struct lkpi_async_call, work);

	call->fn(call->arg);
	call->in_use = 0;
}

int
lkpi_call_async(void (*fn)(void *), void *arg)
{
	struct lkpi_async_call *call;
	unsigned int i, slot;
	uint32_t zero;

	lkpi_workqueue_ready();
	for (i = 0; i < LKPI_ASYNC_CALLS; i++) {
		slot = (lkpi_async_call_next + i) % LKPI_ASYNC_CALLS;
		call = &lkpi_async_calls[slot];
		zero = 0;
		if (__atomic_compare_exchange_n(&call->in_use, &zero, 1,
		    false, __ATOMIC_RELAXED, __ATOMIC_RELAXED) == false)
			continue;
		call->fn = fn;
		call->arg = arg;
		if (!call->initialised) {
			call->initialised = true;
			lkpi_work_init(&call->work, lkpi_async_call_cb);
		}
		lkpi_async_call_next = slot + 1;
		lkpi_work_enqueue(&call->work);
		return 0;
	}

	return EAGAIN;
}

/* ------------------------------------------------------------------ */
/* firmware (the minimal registry, SPEC §3.6-6)                        */
/* ------------------------------------------------------------------ */

/*
 * The registry keys blobs by the bare image name; the Linux path form
 * "rtw88/rtw8821c_fw.bin" is accepted and reduced to the base name.  The
 * registry itself lives in fw_registry.c (compiled-in arrays with a
 * PROVENANCE header; empty by default - the U-disk line needs no
 * firmware).  The data is registry-resident, so release only drops the
 * descriptor.
 */
extern int lkpi_fw_registry_find(const char *base, const u8 **data,
    size_t *size);

int
lkpi_request_firmware(const struct firmware **fw_out, const char *name,
    struct device *dev)
{
	const char *base;
	struct firmware *fw;
	const u8 *data = NULL;
	size_t size = 0;

	base = strrchr(name, '/');
	base = base != NULL ? base + 1 : name;
	if (lkpi_fw_registry_find(base, &data, &size) != 0) {
		lkpi_printf("%s: firmware %s not registered\n",
		    dev_name(dev), base);
		if (fw_out != NULL)
			*fw_out = NULL;
		return -ENOENT;
	}
	fw = lkpi_kmem_alloc(sizeof(*fw), GFP_KERNEL, true);
	if (fw == NULL) {
		if (fw_out != NULL)
			*fw_out = NULL;
		return -ENOMEM;
	}
	fw->size = size;
	fw->data = data;
	fw->priv = NULL;	/* registry-resident: nothing to free */
	if (fw_out != NULL)
		*fw_out = fw;
	return 0;
}

void
lkpi_release_firmware(const struct firmware *fw)
{

	if (fw == NULL)
		return;
	lkpi_kmem_free(fw);
}

int
lkpi_request_firmware_nowait(const char *name, struct device *dev,
    void *context, lkpi_fw_cont cont)
{
	const struct firmware *fw;
	int error;

	/* the registry is in-memory; the async form is synchronous here */
	error = lkpi_request_firmware(&fw, name, dev);
	cont(error == 0 ? fw : NULL, context);
	return error;
}
