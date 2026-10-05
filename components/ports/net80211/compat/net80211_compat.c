/*
 * net80211_compat.c - kernel-services shell for the vendored net80211
 * world: print, panic, spl/mutex serialization, allocation, randomness,
 * time and the small kauth/proc stubs the imported core references.
 *
 * S8 net80211-adapt spec FR-9/FR-11: splnet()/splx() is a REAL recursive
 * serializer (irqsave + nesting bookkeeping, one lock for the whole net
 * ipl on this single-core target) and every kmutex_t funnels into the
 * same primitive, so explicit splnet sections and IEEE80211_LOCK nest
 * freely. The binding table (print -> board_log, kmem/malloc -> heap,
 * cprng -> cntvct-seeded xorshift) is the spec's FR-11 face.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "log.h"
#include "minilibc.h"
#include "generic_timer.h"

#include "sys/systm.h"
#include "sys/mutex.h"
#include "sys/kmem.h"
#include "sys/malloc.h"
#include "sys/kauth.h"
#include "sys/cprng.h"
#include "sys/time.h"
#include "net/if_ether.h"

/* --- globals the imported core reads -------------------------------------- */

int hz = 100;
int cold = 1;
int max_linkhdr = 0;
const char ostype[] = "freewebcam";
const char osrelease[] = "0";
const char version[] = "freewebcam net80211 compat";

/* --- print/panic (FR-11: board_log double exit, one line one stamp) ------- */

int
vprintf(const char *fmt, va_list ap)
{
	char buf[256];

	vsnprintf(buf, sizeof(buf), fmt, ap);
	board_log("%s", buf);
	return 0;
}

int
kprintf(const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	board_log("%s", buf);
	return 0;
}

void
panic(const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	board_log("net80211 panic: %s\n", buf);
	for (;;) {
		__asm__ __volatile__("wfe");
	}
}

/* --- spl serializer + mutex funnel (FR-9) ---------------------------------
 *
 * One recursive irqsave lock serves splnet/splx, splraiseipl and every
 * kmutex_t: on this single-core target an interrupt context cannot run
 * while the lock is held (interrupts are masked), so the only re-entrancy
 * is same-context nesting, which the depth counter tracks. Open question
 * S8-Q4 (lock model under long input processing) rides the S8-2+ board
 * rounds; this is the conservative real-lock form the spec prescribes.
 */

static uint32_t net_nest;
static uintptr_t net_saved_daif;

static inline uintptr_t daif_save_clr(void)
{
	uintptr_t d;

	__asm__ __volatile__(
	    "mrs %0, daif\n"
	    "msr daifset, #0xf"
	    : "=r"(d));
	return d;
}

static inline void daif_restore(uintptr_t d)
{
	__asm__ __volatile__("msr daif, %0" ::"r"(d));
}

static void net_serializer_enter(void)
{
	uintptr_t d = daif_save_clr();

	if (net_nest == 0) {
		net_saved_daif = d;
	}
	net_nest++;
}

static void net_serializer_leave(void)
{
	if (net_nest == 0) {
		board_log("net80211: serializer underflow\n");
		return;
	}
	if (--net_nest == 0) {
		daif_restore(net_saved_daif);
	}
}

ipl_t
splnet(void)
{
	net_serializer_enter();
	return (ipl_t)0;
}

ipl_t
splraiseipl(ipl_t ipl)
{
	(void)ipl;
	return splnet();
}

ipl_t
splsoftserial(void)
{
	return splnet();
}

void
splx(ipl_t ipl)
{
	(void)ipl;
	net_serializer_leave();
}

/* IFQ macros in net/if.h serialize through these two (same primitive as
 * splnet, so queue mutation inside a splnet section nests). */
void
net80211_serializer_lock(void)
{
	net_serializer_enter();
}

void
net80211_serializer_unlock(void)
{
	net_serializer_leave();
}

int
wlan_mutex_init(kmutex_t *m, int type, int ipl)
{
	(void)type;
	(void)ipl;
	m->hm_priv[0] = (void *)0;
	m->hm_priv[1] = (void *)0;
	return 0;
}

int
wlan_mutex_enter(kmutex_t *m)
{
	net_serializer_enter();
	m->hm_priv[1] = (void *)((uintptr_t)m->hm_priv[1] + 1);
	return 1;
}

int
wlan_mutex_exit(kmutex_t *m)
{
	m->hm_priv[1] = (void *)((uintptr_t)m->hm_priv[1] - 1);
	net_serializer_leave();
	return 1;
}

int
wlan_mutex_owned(kmutex_t *m)
{
	return (uintptr_t)m->hm_priv[1] != 0;
}

void
wlan_mutex_destroy(kmutex_t *m)
{
	m->hm_priv[0] = (void *)0;
	m->hm_priv[1] = (void *)0;
}

/* --- allocation (FR-11: the heap the OS config provides) ------------------
 *
 * threadx configs route to the tlsf-backed pvPortMalloc/vPortFree
 * (os/threadx/heap.c); a baremetal build without a heap falls back to a
 * small bump arena so the compile face stays linkable everywhere.
 */

#ifdef NET80211_COMPAT_THREADX_HEAP
extern void *pvPortMalloc(size_t size);
extern void vPortFree(void *ptr);
#else
#define NET80211_STATIC_ARENA 1
#endif

#ifdef NET80211_STATIC_ARENA
static char kmem_arena[64 * 1024] __attribute__((aligned(16)));
static size_t kmem_arena_used;
#endif

static void *
net80211_kmem_alloc(size_t size)
{
#ifdef NET80211_STATIC_ARENA
	size_t align = (16 - (kmem_arena_used & 15)) & 15;
	void *p;

	if (size == 0) {
		return NULL;
	}
	if (kmem_arena_used + align + size > sizeof(kmem_arena)) {
		return NULL;
	}
	kmem_arena_used += align;
	p = &kmem_arena[kmem_arena_used];
	kmem_arena_used += size;
	return p;
#else
	return pvPortMalloc(size);
#endif
}

static void
net80211_kmem_free(void *p)
{
#ifdef NET80211_STATIC_ARENA
	/* bump arena: no per-block free (compile-face fallback only) */
	(void)p;
#else
	vPortFree(p);
#endif
}

void *
kmem_alloc(size_t size, int flags)
{
	(void)flags;
	return net80211_kmem_alloc(size);
}

void *
kmem_zalloc(size_t size, int flags)
{
	void *p = net80211_kmem_alloc(size);

	if (p != NULL) {
		memset(p, 0, size);
	}
	return p;
}

void *
kmem_intr_alloc(size_t size, int flags)
{
	return kmem_alloc(size, flags);
}

void *
kmem_intr_zalloc(size_t size, int flags)
{
	return kmem_zalloc(size, flags);
}

void
kmem_free(void *p, size_t size)
{
	(void)size;
	net80211_kmem_free(p);
}

void
kmem_intr_free(void *p, size_t size)
{
	kmem_free(p, size);
}

void *
wlan_kmalloc(size_t size, int flags, int type)
{
	(void)flags;
	(void)type;
	return kmem_zalloc(size, 0);
}

void
wlan_kfree(void *p, int type)
{
	(void)type;
	net80211_kmem_free(p);
}

void *
kern_malloc(unsigned long size, int flags)
{
	return kmem_zalloc((size_t)size, flags);
}

void
kern_free(void *p)
{
	net80211_kmem_free(p);
}

/* --- time, delay, ticks ---------------------------------------------------- */

static inline uint64_t cntvct_read(void)
{
	return gtimer_count_get();
}

static inline uint32_t cntfrq_read(void)
{
	return gtimer_freq_get();
}

void
delay(unsigned int us)
{
	uint32_t freq = cntfrq_read();
	uint64_t deadline = cntvct_read() + (uint64_t)us * freq / 1000000u;

	while (cntvct_read() < deadline) {
		__asm__ __volatile__("nop");
	}
}

void
microtime(struct timeval *tv)
{
	uint64_t now = cntvct_read();
	uint32_t freq = cntfrq_read() ? cntfrq_read() : 1;

	tv->tv_sec = (long)(now / freq);
	tv->tv_usec = (long)((now % freq) * 1000000u / freq);
}

ticks_t
getticks(void)
{
	return (ticks_t)(cntvct_read() / (cntfrq_read() ? cntfrq_read() : 1));
}

int
ratecheck(struct timeval *last, const struct timeval *min)
{
	struct timeval now, delta;

	microtime(&now);
	timersub(&now, last, &delta);
	if (timercmp(&delta, min, >=)) {
		*last = now;
		return 1;
	}
	return 0;
}

/* --- randomness (compile-face entropy; S8-2 binds the os source) ---------- */

static uint32_t prng_state;

static uint32_t
prng_next(void)
{
	uint32_t x = prng_state;

	if (x == 0) {
		x = (uint32_t)(cntvct_read() >> 8) | 1u;
	}
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	prng_state = x;
	return x;
}

void
cprng_fast(void *buf, size_t len)
{
	uint8_t *p = buf;
	size_t i;

	for (i = 0; i < len; i++) {
		if ((i & 3) == 0) {
			prng_state ^= (uint32_t)(cntvct_read() >> 4);
			(void)prng_next();
		}
		p[i] = (uint8_t)(prng_next() >> 24);
	}
}

/* --- kauth: the single (root) privilege context ---------------------------- */

static struct kauth_cred net80211_cred = { 1, 0, 0, 0, 0, 0, 0, 0 };

kauth_cred_t
kauth_cred_get(void)
{
	return &net80211_cred;
}

kauth_cred_t
kauth_cred_hold(kauth_cred_t cred)
{
	return cred;
}

uid_t
kauth_cred_geteuid(kauth_cred_t cred)
{
	return cred->cr_euid;
}

gid_t
kauth_cred_getegid(kauth_cred_t cred)
{
	return cred->cr_egid;
}

/* --- misc small helpers ----------------------------------------------------- */

int
uimin(int a, int b)
{
	return a < b ? a : b;
}

int
uimax(int a, int b)
{
	return a > b ? a : b;
}

bool
cpu_intr_p(void)
{
	return false;
}

bool
cpu_softintr_p(void)
{
	return false;
}

int
copyin(const void *u, void *k, size_t len)
{
	memcpy(k, u, len);
	return 0;
}

int
copyout(const void *k, void *u, size_t len)
{
	memcpy(u, k, len);
	return 0;
}

int
copystr(const void *u, void *k, size_t len, size_t *done)
{
	size_t n = strnlen((const char *)u, len - 1) + 1;

	if (n > len) {
		return 1;
	}
	memcpy(k, u, n);
	if (done != NULL) {
		*done = n;
	}
	return 0;
}
