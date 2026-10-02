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
#include <net/ieee80211_radiotap.h>

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

int
lkpi_atomic_sub_return(int i, atomic_t *v)
{

	return (int)__atomic_sub_fetch(&v->counter, i, __ATOMIC_RELAXED);
}

int
lkpi_atomic_add_return(int i, atomic_t *v)
{

	return (int)__atomic_add_fetch(&v->counter, i, __ATOMIC_RELAXED);
}

unsigned short
eth_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	struct ethhdr {
		unsigned char	h_dest[ETH_ALEN];
		unsigned char	h_source[ETH_ALEN];
		unsigned short	h_proto;
	} __packed;
	const struct ethhdr *eth;
	unsigned short proto;

	(void)dev;
	if (skb == NULL || skb->len < sizeof(*eth))
		return 0;
	eth = (const struct ethhdr *)skb->data;
	proto = eth->h_proto;
	skb->data += sizeof(*eth);
	skb->len -= sizeof(*eth);
	skb->tail += sizeof(*eth);
	skb->protocol = proto;
	if (eth->h_dest[0] & 0x01)
		skb->pkt_type = (eth->h_dest[0] & 0x80) != 0 ?
		    PACKET_MULTICAST : PACKET_BROADCAST;
	return proto;
}

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

void *
lkpi_skb_alloc(unsigned int size, gfp_t gfp)
{
	struct sk_buff *skb;

	/* one allocation for the header and the data area */
	(void)gfp;
	skb = pvPortMalloc(sizeof(*skb) + size);
	if (skb == NULL)
		return NULL;
	memset(skb, 0, sizeof(*skb));
	skb->head = (unsigned char *)(skb + 1);
	skb->data = skb->head;
	skb->tail = 0;
	skb->end = size;
	skb->len = 0;
	skb->truesize = sizeof(*skb) + size;
	return skb;
}

void
lkpi_skb_free(struct sk_buff *skb)
{

	if (skb == NULL)
		return;
	vPortFree(skb);
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

/* [W3] completion rework: the wait primitive is NO LONGER a TX object
 * embedded in the caller's struct.  The previous shape (per-completion
 * osMutex + TX_SEMAPHORE, created at init, deleted at deinit) turned
 * every completion into a globally-registered kernel object, and the
 * vendor's Linux-legal lifetimes (kfree of a submit_ctx, raw recycle of
 * a pool urb) then left zombie nodes on the ThreadX created-object
 * chain - the up-path data aborts.  The rework: `done` stays plain
 * memory; sleep/wake rides ONE global broadcast semaphore; waiters
 * recheck their own flag on every wake on a bounded 10-tick slice, so
 * a zeroed, freed or recycled completion cannot strand anything.
 * complete()/complete_all() both wake all current waiters (Linux's
 * wake-one is an optimization, not semantics); the sticky complete_all
 * rule (fix 5f43157, [FWCI] 13) is kept on the done field. */
static TX_SEMAPHORE lkpi_completion_bcast;
static unsigned int lkpi_completion_bcast_ok;

static void lkpi_completion_kick(void)
{
	TX_INTERRUPT_SAVE_AREA

	if (lkpi_completion_bcast_ok != 0U)
		return;
	TX_DISABLE;
	if (lkpi_completion_bcast_ok == 0U)
		lkpi_completion_bcast_ok =
		    (tx_semaphore_create(&lkpi_completion_bcast,
		     "lkpi_cvb", 0) == TX_SUCCESS) ? 1U : 0U;
	TX_RESTORE;
}

#define	LKPI_COMPLETION_SLICE	10U

void
lkpi_completion_init(struct completion *c)
{

	c->done = 0;
}

void
lkpi_completion_deinit(struct completion *c)
{

	/* nothing to release: the completion owns no kernel object */
	(void)c;
}

void
lkpi_completion_reinit(struct completion *c)
{

	c->done = 0;
}

void
lkpi_complete(struct completion *c)
{
	struct lkpi_cv_shell *cv = lk_ccv(c);
	int n, i;

	/* complete() after complete_all() must not disturb the sticky mark
	 * (FreeBSD linux_compat.c:2299) */
	if (c->done != LKPI_COMPLETION_STICKY)
		c->done++;
	lkpi_completion_kick();
	/* best-effort wake-all; a waiter that raced the count rechecks on
	 * its bounded slice anyway (no lost-wakeup hazard) */
	n = cv->waiters;
	for (i = 0; i < n; i++)
		(void)tx_semaphore_put(&lkpi_completion_bcast);
}

/*
 * complete_all is STICKY in Linux: every current and future waiter passes
 * immediately (the probe-time firmware wait consumed the only token once;
 * the R-LK3 ops->start wedge, five boots, fix 5f43157).
 */
void
lkpi_complete_all(struct completion *c)
{
	struct lkpi_cv_shell *cv = lk_ccv(c);
	int n, i;

	c->done = LKPI_COMPLETION_STICKY;
	lkpi_completion_kick();
	n = cv->waiters;
	for (i = 0; i < n; i++)
		(void)tx_semaphore_put(&lkpi_completion_bcast);
}

void
lkpi_wait_for_completion(struct completion *c)
{
	struct lkpi_cv_shell *cv = lk_ccv(c);

	lkpi_completion_kick();
	cv->waiters++;
	while (c->done == 0)
		(void)tx_semaphore_get(&lkpi_completion_bcast,
		    LKPI_COMPLETION_SLICE);
	cv->waiters--;
	/* a sticky complete_all pass does not consume (it is not a count) */
	if (c->done != LKPI_COMPLETION_STICKY)
		c->done--;
}

unsigned long
lkpi_wait_for_completion_timeout(struct completion *c, unsigned long jiff)
{
	struct lkpi_cv_shell *cv = lk_ccv(c);

	lkpi_completion_kick();
	cv->waiters++;
	while (c->done == 0) {
		if (tx_semaphore_get(&lkpi_completion_bcast,
		    MAX(1, (UINT)jiff)) != TX_SUCCESS) {
			cv->waiters--;
			/* timed out: the count is untouched (Linux returns
			 * 0 and the caller owns the race window) */
			return 0;
		}
	}
	cv->waiters--;
	/* same sticky rule as lkpi_wait_for_completion (5f43157): rtw88
	 * runs the timeout variant on complete_all-armed completions */
	if (c->done != LKPI_COMPLETION_STICKY)
		c->done--;
	return 1;
}

int
lkpi_try_wait_for_completion(struct completion *c)
{
	int done;

	if (c->done > 0) {
		/* sticky complete_all passes without consuming */
		if (c->done != LKPI_COMPLETION_STICKY)
			c->done--;
		done = 1;
	} else {
		done = 0;
	}
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
				.stack_size = 8192,
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

/* ------------------------------------------------------------------ */
/* semaphores ([S6] §3.4 B1): TX counting semaphore; down_interruptible */
/* never reports a signal because nothing in this image signals.        */
/* ------------------------------------------------------------------ */

void
lkpi_sema_init(struct semaphore *s, int val)
{

	memset(s, 0, sizeof(*s));
	if (tx_semaphore_create(lk_sem(s), "lkpi_sem", (UINT)(val < 0 ? 0 : val))
	    != TX_SUCCESS)
		lkpi_fatal("semaphore init failed");
}

void
lkpi_sema_destroy(struct semaphore *s)
{

	(void)tx_semaphore_delete(lk_sem(s));
}

void
lkpi_sema_down(struct semaphore *s)
{

	(void)tx_semaphore_get(lk_sem(s), TX_WAIT_FOREVER);
}

int
lkpi_sema_down_interruptible(struct semaphore *s)
{

	(void)tx_semaphore_get(lk_sem(s), TX_WAIT_FOREVER);
	return 0;
}

int
lkpi_sema_down_trylock(struct semaphore *s)
{

	return tx_semaphore_get(lk_sem(s), 0) == TX_SUCCESS ? 0 : -EAGAIN;
}

void
lkpi_sema_up(struct semaphore *s)
{

	(void)tx_semaphore_put(lk_sem(s));
}

/* ------------------------------------------------------------------ */
/* kthreads ([S6] §3.4 B2): TX thread + stop flag; the thread's entry   */
/* runs the vendor function once and completes when it returns          */
/* ([FWC]-style thread_exit callers go through complete_and_exit, which */
/* fires the completion and terminates in place instead).               */
/* ------------------------------------------------------------------ */

#define	LKPI_KTHREAD_STACK	8192
#define	LKPI_KTHREAD_PRIORITY	20	/* below the timer, above idle */

static void
lkpi_kthread_trampoline(ULONG arg)
{
	struct task_struct *t = (struct task_struct *)(uintptr_t)arg;

	t->fn(t->arg);
	/* ThreadX completes the thread when the entry function returns;
	 * kthread_stop polls the exited flag. */
	t->exited = 1;
}

/* the kthread registry: lkpi_current() resolves tx_thread_identify() by
 * walking it - the worker/timer contexts are not kthreads and their control
 * blocks never appear here */
static struct task_struct *lkpi_kthread_head;

struct task_struct *
lkpi_kthread_run(int (*threadfn)(void *), void *data, const char *name)
{
	struct task_struct *t;
	void *stack;

	t = lkpi_kmem_alloc(sizeof(*t), GFP_KERNEL, true);
	if (t == NULL)
		return ERR_PTR(-ENOMEM);
	stack = pvPortMalloc(LKPI_KTHREAD_STACK);
	if (stack == NULL) {
		lkpi_kmem_free(t);
		return ERR_PTR(-ENOMEM);
	}
	t->fn = (void (*)(void *))threadfn;
	t->arg = data;
	t->name = name;
	(void)strscpy(t->comm, name != NULL ? name : "lkpithread",
	    sizeof(t->comm));
	t->pid = 0;
	if (tx_thread_create(lk_thread(t), (CHAR *)name,
	    lkpi_kthread_trampoline, (ULONG)(uintptr_t)stack,
	    stack, LKPI_KTHREAD_STACK, LKPI_KTHREAD_PRIORITY,
	    LKPI_KTHREAD_PRIORITY, TX_NO_TIME_SLICE, TX_AUTO_ACTIVATE)
	    != TX_SUCCESS) {
		vPortFree(stack);
		lkpi_kmem_free(t);
		return ERR_PTR(-ENOMEM);
	}
	do {
		t->lk_next = lkpi_kthread_head;
	} while (__atomic_compare_exchange_n(&lkpi_kthread_head, &t->lk_next,
	    t, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED) == false);
	lkpi_printf("lkpi: kthread %s started\n", name);
	return t;
}

int
lkpi_kthread_stop(struct task_struct *t)
{
	struct task_struct **pp;
	void *stack;

	if (t == NULL || IS_ERR(t))
		return -EINVAL;
	t->should_stop = 1;
	/* the vendor threads poll kthread_should_stop at loop granularity;
	 * wait out at one tick so a mid-sleep thread notices */
	while (!t->exited)
		tx_thread_sleep(1);
	/* unlink from the registry */
	for (pp = &lkpi_kthread_head; *pp != NULL; pp = &(*pp)->lk_next) {
		if (*pp == t) {
			*pp = t->lk_next;
			break;
		}
	}
	stack = lk_thread(t)->tx_thread_stack_start;
	(void)tx_thread_delete(lk_thread(t));
	vPortFree(stack);
	lkpi_printf("lkpi: kthread %s stopped\n", t->name != NULL ? t->name : "?");
	lkpi_kmem_free(t);
	return 0;
}

bool
lkpi_kthread_should_stop(void)
{
	struct task_struct *t = lkpi_current();

	return t != NULL && t->should_stop != 0;
}

struct task_struct *
lkpi_current(void)
{
	TX_THREAD *tx = tx_thread_identify();
	struct task_struct *t;

	if (tx == NULL)
		return NULL;
	for (t = lkpi_kthread_head; t != NULL; t = t->lk_next) {
		if (lk_thread(t) == tx)
			return t;
	}
	return NULL;
}

void
complete_and_exit(struct completion *comp, long code)
{

	(void)code;
	if (comp != NULL)
		complete_all(comp);
	(void)tx_thread_terminate(tx_thread_identify());
	for (;;)
		__asm__ __volatile__("wfe");
}

/* ------------------------------------------------------------------ */
/* tasklets ([S6] §3.4 B3): softirq semantics ride the system workqueue  */
/* (single passive worker - the drain model shape), hi_schedule ==      */
/* schedule at this priority.                                           */
/* ------------------------------------------------------------------ */

static void
lkpi_tasklet_trampoline(struct work_struct *w)
{
	struct tasklet_struct *t = container_of(w, struct tasklet_struct, work);

	t->func(t->data);
	t->armed = false;
}

void
lkpi_tasklet_init(struct tasklet_struct *t, void (*fn)(unsigned long),
    unsigned long data)
{

	memset(t, 0, sizeof(*t));
	lkpi_work_init(&t->work, lkpi_tasklet_trampoline);
	t->func = fn;
	t->data = data;
}

void
lkpi_tasklet_schedule(struct tasklet_struct *t)
{

	lkpi_workqueue_ready();
	if (t->func == NULL)
		return;
	t->armed = true;
	(void)lkpi_queue_work(lkpi_system_wq, &t->work);
}

void
lkpi_tasklet_kill(struct tasklet_struct *t)
{

	lkpi_work_flush(&t->work);
	t->armed = false;
}

/* ------------------------------------------------------------------ */
/* net_device ([S6] §3.4 B4/B5)                                        */
/* ------------------------------------------------------------------ */

struct skb_shared_info lkpi_shinfo_zero;

static struct lkpi_tx_queue lkpi_tx_queue_singleton;

struct lkpi_tx_queue *
netdev_get_tx_queue(struct net_device *dev, unsigned int index)
{

	(void)dev;
	(void)index;
	return &lkpi_tx_queue_singleton;
}

void *
lkpi_netdev_priv(const struct net_device *dev)
{

	/* net_device is 32-byte aligned, so sizeof is the priv offset */
	return (void *)(uintptr_t)((const char *)dev + sizeof(*dev));
}

struct net_device *
alloc_etherdev_mq(int sizeof_priv, unsigned int txqs)
{
	struct net_device *dev;
	size_t size = sizeof(*dev) + (size_t)(sizeof_priv > 0 ? sizeof_priv : 0);

	(void)txqs;
	dev = lkpi_kmem_alloc(size, GFP_KERNEL, true);
	if (dev == NULL)
		return NULL;
	dev->addr_len = ETH_ALEN;
	dev->type = ARPHRD_ETHER;
	dev->mtu = 1500;
	dev->flags = IFF_BROADCAST | IFF_MULTICAST;
	(void)dev_alloc_name(dev, "eth%d");
	return dev;
}

void
free_netdev(struct net_device *dev)
{

	lkpi_kmem_free(dev);
}

int
register_netdev(struct net_device *dev)
{

	lkpi_printf("lkpi: netdev %s registered (no-op)\n", dev->name);
	return 0;
}

void
unregister_netdev(struct net_device *dev)
{

	lkpi_printf("lkpi: netdev %s unregistered (no-op)\n", dev->name);
}

int
dev_alloc_name(struct net_device *dev, const char *fmt)
{

	/* single interface per driver in this image; %d resolves to 0 */
	snprintf(dev->name, sizeof(dev->name), fmt, 0);
	return 0;
}

void
netif_carrier_on(struct net_device *dev)
{

	lkpi_printf("lkpi: %s carrier on\n", dev->name);
}

void
netif_carrier_off(struct net_device *dev)
{

	lkpi_printf("lkpi: %s carrier off\n", dev->name);
}

void
netif_device_attach(struct net_device *dev)
{
	(void)dev;
}

void
netif_device_detach(struct net_device *dev)
{
	(void)dev;
}

/* ------------------------------------------------------------------ */
/* netif_rx sink ([S6] §3.4 B5): counted-and-drop until W5 mounts the   */
/* lwip bridge                                                          */
/* ------------------------------------------------------------------ */

#define	NET_RX_SUCCESS	0
#define	NET_RX_DROP	1

static lkpi_netif_rx_sink_t lkpi_netif_sink;
static unsigned long lkpi_netif_drop_cnt;

void
lkpi_netif_rx_sink_register(lkpi_netif_rx_sink_t sink)
{

	lkpi_netif_sink = sink;
}

unsigned long
lkpi_netif_rx_dropped(void)
{

	return lkpi_netif_drop_cnt;
}

int
netif_rx(struct sk_buff *skb)
{
	int r;

	if (skb == NULL)
		return NET_RX_DROP;
	if (lkpi_netif_sink != NULL) {
		r = lkpi_netif_sink(skb);
		if (r == NET_RX_DROP)
			lkpi_netif_drop_cnt++;
		return r;
	}
	lkpi_netif_drop_cnt++;
	kfree_skb(skb);
	return NET_RX_DROP;
}

/* ------------------------------------------------------------------ */
/* rtnetlink / notifier / dev-lookup stamps                             */
/* ------------------------------------------------------------------ */

static unsigned int lkpi_rtnl_locked;

void
rtnl_lock(void)
{

	lkpi_rtnl_locked++;
}

void
rtnl_unlock(void)
{

	if (lkpi_rtnl_locked > 0)
		lkpi_rtnl_locked--;
}

int
rtnl_is_locked(void)
{

	return lkpi_rtnl_locked > 0;
}

static unsigned long lkpi_wext_event_cnt;

int
wireless_send_event(struct net_device *dev, unsigned int cmd,
    union iwreq_data *wrqu, const char *extra)
{

	(void)wrqu;
	(void)extra;
	lkpi_wext_event_cnt++;
	lkpi_printf("lkpi: %s wext event %u\n",
	    dev != NULL ? dev->name : "?", cmd);
	return 0;
}

struct net_device *
dev_get_by_name(void *net, const char *name)
{

	(void)net;
	(void)name;
	return NULL;
}

struct net_device *
dev_get_by_index(void *net, int ifindex)
{

	(void)net;
	(void)ifindex;
	return NULL;
}

/* ------------------------------------------------------------------ */
/* skb clone family ([S6] §3.4 B6): full-copy clone preserving geometry */
/* ------------------------------------------------------------------ */

static struct sk_buff *
lkpi_skb_dup_like(const struct sk_buff *skb, gfp_t gfp, bool whole)
{
	struct sk_buff *n;
	unsigned int copy;

	if (skb == NULL)
		return NULL;
	n = lkpi_skb_alloc(skb->end, gfp);
	if (n == NULL)
		return NULL;
	copy = whole ? skb->end
	    : (unsigned int)(skb->data - skb->head) + skb->len;
	memcpy(n->head, skb->head, copy);
	n->data = n->head + (skb->data - skb->head);
	n->tail = skb->tail;
	n->len = skb->len;
	n->data_len = skb->data_len;
	n->truesize = skb->truesize;
	n->protocol = skb->protocol;
	n->queue_mapping = skb->queue_mapping;
	n->priority = skb->priority;
	n->pkt_type = skb->pkt_type;
	n->ip_summed = skb->ip_summed;
	n->dev = skb->dev;
	n->sk = skb->sk;
	memcpy(n->cb, skb->cb, sizeof(n->cb));
	return n;
}

struct sk_buff *
lkpi_skb_clone(const struct sk_buff *skb, gfp_t gfp)
{

	/* the clone face is full-copy: nothing is shared, skb_cloned()==0 */
	return lkpi_skb_dup_like(skb, gfp, true);
}

struct sk_buff *
lkpi_skb_copy_header(const struct sk_buff *skb, gfp_t gfp)
{

	return lkpi_skb_dup_like(skb, gfp, false);
}

/* ------------------------------------------------------------------ */
/* file-I/O face ([S6] B8 / W0L-S1): dead-path stubs - every open fails */
/* and the callers keep their built-in fallbacks.                       */
/* ------------------------------------------------------------------ */

struct file *
filp_open(const char *path, int flags, int mode)
{

	(void)flags;
	(void)mode;
	lkpi_printf("lkpi: filp_open(%s): no filesystem\n", path != NULL ? path : "?");
	return ERR_PTR(-ENOSYS);
}

int
filp_close(struct file *fp, void *files)
{

	(void)fp;
	(void)files;
	return -ENOSYS;
}

long
kernel_read(struct file *fp, void *buf, size_t len, loff_t *pos)
{

	(void)fp;
	(void)buf;
	(void)len;
	(void)pos;
	return -ENOSYS;
}

long
kernel_write(struct file *fp, const void *buf, size_t len, loff_t *pos)
{

	(void)fp;
	(void)buf;
	(void)len;
	(void)pos;
	return -ENOSYS;
}

long
__vfs_read(struct file *fp, char *buf, size_t len, loff_t *pos)
{

	return kernel_read(fp, buf, len, pos);
}

long
__vfs_write(struct file *fp, const char *buf, size_t len, loff_t *pos)
{

	return kernel_write(fp, buf, len, pos);
}

int
vfs_fsync(struct file *fp, int datasync)
{

	(void)fp;
	(void)datasync;
	return -ENOSYS;
}

int
seq_printf(struct seq_file *m, const char *fmt, ...)
{
	static char buf[256];
	va_list ap;
	int n;

	(void)m;
	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	board_log("%s", buf);
	return n;
}

int
seq_puts(struct seq_file *m, const char *str)
{

	(void)m;
	board_log("%s", str != NULL ? str : "");
	return 0;
}

int
seq_putc(struct seq_file *m, char c)
{

	(void)m;
	board_log("%c", c);
	return 0;
}

/* ------------------------------------------------------------------ */
/* kernel-libc face ([S6] B8 calibration): the vendor osdep calls       */
/* printk/sprintf/sscanf/strsep/strpbrk; minilibc does not carry them.  */
/* ------------------------------------------------------------------ */

int
printk(const char *fmt, ...)
{
	static char buf[384];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	/* printk appends a newline in kernel semantics; the board_log sink
	 * stamps per line, strip ours to keep one line one stamp */
	board_log("%s", buf);
	return n;
}

int
sprintf(char *buf, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, (size_t)-1 / 2, fmt, ap);
	va_end(ap);
	return n;
}

char *
strsep(char **s, const char *delim)
{
	char *start, *end;

	if (s == NULL || *s == NULL)
		return NULL;
	start = *s;
	for (end = start; *end != '\0'; end++) {
		if (strchr(delim, *end) != NULL)
			break;
	}
	if (*end == '\0') {
		*s = NULL;
	} else {
		*end = '\0';
		*s = end + 1;
	}
	return start;
}

char *
strpbrk(const char *s, const char *accept)
{
	const char *sc;

	for (; *s != '\0'; s++) {
		for (sc = accept; *sc != '\0'; sc++) {
			if (*sc == *s)
				return (char *)(uintptr_t)s;
		}
	}
	return NULL;
}

/*
 * A compact sscanf over the subset the vendor debug/param paths use:
 * %i %d %u %x %X %s %c %n with h/hh/l modifiers and '*' suppression.
 * Returns the number of successful conversions (Linux semantics).
 */
int
sscanf(const char *buf, const char *fmt, ...)
{
	va_list ap;
	const char *bp = buf;
	int nconv = 0;
	int suppress;

	if (buf == NULL || fmt == NULL)
		return 0;
	va_start(ap, fmt);

	for (; *fmt != '\0'; fmt++) {
		if (*fmt != '%') {
			if (*fmt == ' ' || *fmt == '\t') {
				while (*bp == ' ' || *bp == '\t')
					bp++;
				continue;
			}
			if (*bp != *fmt)
				break;
			bp++;
			continue;
		}
		fmt++;
		suppress = (*fmt == '*');
		if (suppress)
			fmt++;

		int length = 0;	/* 0=int, 1=long, -1=short, -2=char */
		while (*fmt == 'h' || *fmt == 'l') {
			length += (*fmt == 'l') ? 1 : -1;
			fmt++;
		}

		char conv = *fmt;
		if (conv == '\0')
			break;

		while (*bp == ' ' || *bp == '\t')
			bp++;

		if (conv == '%') {
			if (*bp != '%')
				break;
			bp++;
			continue;
		}
		if (conv == 'n') {
			if (!suppress) {
				int *p = va_arg(ap, int *);
				*p = (int)(bp - buf);
			}
			continue;
		}

		unsigned long long uv = 0;
		long long sv = 0;
		int neg = 0;
		int got = 0;

		if (conv == 's') {
			char *dst = suppress ? NULL : va_arg(ap, char *);
			while (*bp != '\0' && *bp != ' ' && *bp != '\t' &&
			    *bp != '\n') {
				if (dst != NULL)
					*dst++ = *bp;
				bp++;
				got = 1;
			}
			if (dst != NULL)
				*dst = '\0';
		} else if (conv == 'c') {
			if (*bp == '\0')
				break;
			if (!suppress) {
				char *dst = va_arg(ap, char *);
				*dst = *bp;
			}
			bp++;
			got = 1;
		} else {
			if (*bp == '-' && (conv == 'd' || conv == 'i')) {
				neg = 1;
				bp++;
			}
			if (conv == 'x' || conv == 'X' ||
			    (conv == 'i' && bp[0] == '0' &&
			    (bp[1] == 'x' || bp[1] == 'X'))) {
				if (bp[0] == '0' &&
				    (bp[1] == 'x' || bp[1] == 'X'))
					bp += 2;
				for (; *bp != '\0'; bp++) {
					int d;
					if (*bp >= '0' && *bp <= '9')
						d = *bp - '0';
					else if (*bp >= 'a' && *bp <= 'f')
						d = *bp - 'a' + 10;
					else if (*bp >= 'A' && *bp <= 'F')
						d = *bp - 'A' + 10;
					else
						break;
					uv = uv * 16 + (unsigned)d;
					got = 1;
				}
				sv = (long long)uv;
			} else if (conv == 'd' || conv == 'u' || conv == 'i') {
				for (; *bp >= '0' && *bp <= '9'; bp++) {
					uv = uv * 10 +
					    (unsigned)(*bp - '0');
					got = 1;
				}
				sv = (long long)uv;
			} else {
				break;	/* unsupported conversion */
			}
			if (neg)
				sv = -sv;
		}

		if (!got)
			break;
		if (!suppress) {
			switch (length) {
			case -2: {
				char *p = va_arg(ap, char *);
				*p = (char)sv;
				break;
			}
			case -1: {
				short *p = va_arg(ap, short *);
				*p = (short)sv;
				break;
			}
			case 1: {
				long *p = va_arg(ap, long *);
				*p = (long)sv;
				break;
			}
			default: {
				int *p = va_arg(ap, int *);
				*p = (int)sv;
				break;
			}
			}
			nconv++;
		}
	}

	va_end(ap);
	return nconv;
}

long
lkpi_sched_timeout(unsigned int ms)
{

	tx_thread_sleep((UINT)(ms != 0 ? ms : 1));
	return 0;	/* the whole timeout elapsed (Linux semantics) */
}

unsigned int
prandom_u32(void)
{

	return get_random_int();
}

int
skb_copy_bits(const struct sk_buff *skb, unsigned int offset, void *to,
    unsigned int len)
{
	const unsigned char *from;
	unsigned int avail;

	if (skb == NULL || to == NULL)
		return -EINVAL;
	if (offset >= skb->len)
		return -EFAULT;
	avail = skb->len - offset;
	if (len > avail)
		len = avail;
	from = skb->data + offset;
	memcpy(to, from, len);
	return (int)len;
}

unsigned int
ieee80211_get_radiotap_len(const struct sk_buff *skb)
{
	const struct ieee80211_radiotap_header *rth;

	if (skb == NULL || skb->len < sizeof(*rth))
		return 0;
	rth = (const struct ieee80211_radiotap_header *)skb->data;
	return (unsigned int)(rth->it_len != 0 ? rth->it_len : sizeof(*rth));
}

void
get_random_bytes(void *buf, int len)
{

	lkpi_prandom_bytes(buf, (size_t)(len > 0 ? len : 0));
}
