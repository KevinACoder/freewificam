/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_osdep.c
 * @brief   The osdep service bodies the vendored driver's closure names that the
 *          service core (linuxkpi_core.c) does not already carry: the kernel
 *          thread, the scheduler entry points, the counting semaphore, the file
 *          seam, the process/signal seams, the netlink attribute builders, the
 *          checksum primitives and the initial network namespace.
 *          (The deferred-callback object is not one of them: its seam is the
 *          service core's deferred work, so its body lives there.)
 *
 * @design adapter over one OS surface, the same shape as the service core
 *         (components/linuxkpi/linuxkpi_core.c:1-20). The contract header and the
 *         per-area face headers declare these Linux services; this file is the
 *         body behind the declarations the compiled set actually names, and it
 *         reaches the OS only through the CMSIS-RTOS2 facade, which is what lets
 *         one body serve the board's ThreadX and a host FreeRTOS build. Each body
 *         states the driver call site it serves and, where it is deliberately
 *         minimal, says so in one line: a thread is a CMSIS thread, a semaphore
 *         is the count plus a yielding wait, and the seams the port has no
 *         subsystem for (VFS, processes, signals, netlink sockets) refuse
 *         honestly rather than pretend.
 *
 * A kernel thread on this port: kthread_create_on_node creates the CMSIS thread
 * but the body waits for wake_up_process (the kthread_run contract), the thread
 * name becomes the CMSIS thread name and the task record, and kthread_stop sets
 * the stop flag and reaps the thread. The table is fixed and small; a create that
 * finds it full answers -ENOMEM, never a shared object.
 *
 * Reference for the shape: components/linuxkpi/linuxkpi_core.c:1-20.
 * @date    12.10.2026
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"

#ifdef CONFIG_LKPI_CENSUS
#include "lkpi_census.h"
#endif

#include "os/cmsis2/cmsis2.h"

/*
 * The compiler-attribute vocabulary first: the face headers below wear
 * __noreturn/__must_check/__force in their declarations, and the driver's own
 * include order pulls <linux/compiler.h> ahead of them for the same reason
 * (third-party/rtl8189fs/include/osdep_service_linux.h:20).
 */
#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/semaphore.h>
#include <linux/string.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>
#include <net/net_namespace.h>
#include <net/netlink.h>

/* ------------------------------------------------------------------ */
/* kernel threads (driver: osdep_service.h:411 thread_enter,            */
/* hal_init.c:5567 kthread_run, osdep_service.c:1298 exit)              */
/* ------------------------------------------------------------------ */

#ifndef CONFIG_LKPI_KTHREAD_MAX
#define CONFIG_LKPI_KTHREAD_MAX		8
#endif
#ifndef CONFIG_LKPI_KTHREAD_STACK
#define CONFIG_LKPI_KTHREAD_STACK	8192
#endif

/*
 * One kernel thread. The public task record is first, so the pointer a caller
 * holds and the table entry are the same address; the CMSIS thread, the
 * start gate and the stop flag are this file's bookkeeping. A thread created
 * here but not yet woken (kthread_create without kthread_run) waits on the gate,
 * exactly as the interface's create/run split requires.
 */
struct lkpi_kthread {
	struct task_struct	task;
	osThreadId_t		id;
	osSemaphoreId_t		gate;		/* released by wake_up_process */
	int			(*threadfn)(void *);
	void			*data;
	volatile int		should_stop;
	volatile int		exited;
	volatile int		used;
};

static struct lkpi_kthread lkpi_kthreads[CONFIG_LKPI_KTHREAD_MAX];

static struct lkpi_kthread *
lkpi_kthread_of(struct task_struct *task)
{
	unsigned int i;

	if (task == NULL)
		return NULL;
	for (i = 0; i < CONFIG_LKPI_KTHREAD_MAX; i++)
		if (lkpi_kthreads[i].used && &lkpi_kthreads[i].task == task)
			return &lkpi_kthreads[i];
	return NULL;
}

static struct lkpi_kthread *
lkpi_kthread_by_thread(osThreadId_t id)
{
	unsigned int i;

	if (id == NULL)
		return NULL;
	for (i = 0; i < CONFIG_LKPI_KTHREAD_MAX; i++)
		if (lkpi_kthreads[i].used && lkpi_kthreads[i].id == id)
			return &lkpi_kthreads[i];
	return NULL;
}

/*
 * The thread body. The gate is the create/run split: kthread_create leaves it
 * closed, kthread_run's wake_up_process opens it. When the function returns the
 * thread is over and the flag lets kthread_stop reap it.
 */
static void
lkpi_kthread_entry(void *arg)
{
	struct lkpi_kthread *k = arg;

	(void)osSemaphoreAcquire(k->gate, osWaitForever);
	(void)k->threadfn(k->data);
	k->exited = 1;
	/* the body returning ends the thread; kthread_stop reaps the slot */
}

struct task_struct *
kthread_create_on_node(int (*threadfn)(void *data), void *data, int node,
    const char namefmt[], ...)
{
	struct lkpi_kthread *k = NULL;
	osThreadAttr_t attr;
	va_list ap;
	unsigned int i;

	(void)node;	/* one memory node on this port (NUMA_NO_NODE) */
	if (threadfn == NULL)
		return ERR_PTR(-EINVAL);
	for (i = 0; i < CONFIG_LKPI_KTHREAD_MAX; i++)
		if (!lkpi_kthreads[i].used) {
			k = &lkpi_kthreads[i];
			break;
		}
	if (k == NULL) {
		lkpi_printf(ULOG_ERROR_LEVEL, "lkpi kthread: table full (%u)\n",
		    (unsigned)CONFIG_LKPI_KTHREAD_MAX);
		return ERR_PTR(-ENOMEM);
	}
	memset(k, 0, sizeof(*k));
	k->used = 1;
	k->threadfn = threadfn;
	k->data = data;
	k->task.pid = (pid_t)(i + 1u);
	va_start(ap, namefmt);
	(void)vsnprintf(k->task.comm, sizeof(k->task.comm), namefmt, ap);
	va_end(ap);
	k->gate = osSemaphoreNew(1u, 0u, NULL);
	if (k->gate == NULL) {
		k->used = 0;
		return ERR_PTR(-ENOMEM);
	}
	memset(&attr, 0, sizeof(attr));
	attr.name = k->task.comm;
	attr.stack_size = CONFIG_LKPI_KTHREAD_STACK;
	attr.priority = osPriorityNormal;
	k->id = osThreadNew(lkpi_kthread_entry, k, &attr);
	if (k->id == NULL) {
		(void)osSemaphoreDelete(k->gate);
		k->used = 0;
		return ERR_PTR(-ENOMEM);
	}
	return &k->task;
}

int
wake_up_process(struct task_struct *tsk)
{
	struct lkpi_kthread *k = lkpi_kthread_of(tsk);

	if (k == NULL)
		return 0;
	(void)osSemaphoreRelease(k->gate);
	return 1;
}

bool
kthread_should_stop(void)
{
	struct lkpi_kthread *k = lkpi_kthread_by_thread(osThreadGetId());

	/* a thread this file did not create has no stop flag: never true */
	return (k != NULL) ? (k->should_stop != 0) : false;
}

int
kthread_stop(struct task_struct *k)
{
	struct lkpi_kthread *kt = lkpi_kthread_of(k);

	if (kt == NULL)
		return -EINVAL;
	kt->should_stop = 1;
	/*
	 * A stop before the thread ever ran still releases the gate, so the body
	 * runs its stop check and returns instead of waiting forever.
	 */
	(void)osSemaphoreRelease(kt->gate);
	while (!kt->exited)
		(void)osDelay(1);
	(void)osSemaphoreDelete(kt->gate);
	kt->gate = NULL;
	kt->id = NULL;
	kt->used = 0;
	return 0;
}

void
kthread_complete_and_exit(struct completion *comp, long code)
{

	(void)code;
	if (comp != NULL)
		complete(comp);
	/* the calling thread ends here; the return is unreachable by contract */
	(void)osThreadTerminate(osThreadGetId());
	for (;;)
		(void)osDelay(1000);
}

/* ------------------------------------------------------------------ */
/* the scheduler entry points (driver: osdep_service.c:1632 sleep,      */
/* osdep_service.h:432 stop wait)                                       */
/* ------------------------------------------------------------------ */

void
schedule(void)
{

	/*
	 * The interface's schedule() blocks the caller until it is woken. This
	 * port has no runqueue wakeup, so the honest approximation is to sleep
	 * one tick: the only caller (the thread stop-wait loop, osdep_service.h:
	 * 432) re-checks its condition each time round, and a plain yield would
	 * busy-spin the single core against every other thread.
	 */
	(void)osDelay(1);
}

long
schedule_timeout(long timeout)
{

	if (timeout > 0)
		(void)osDelay((uint32_t)timeout);
	else
		(void)osThreadYield();
	return 0;	/* the timeout expired; this port models no early wake */
}

void
yield(void)
{

	(void)osThreadYield();
}

/* ------------------------------------------------------------------ */
/* the counting semaphore (driver: osdep_service.c:1241 up, :1268 down)  */
/* ------------------------------------------------------------------ */

/*
 * The census this file contributes: the waiting takes of the counting
 * semaphore and the ticks their empty polls spin on. This port has no wait
 * queue, so an empty take sleeps one tick and re-checks - the iteration count
 * is how long a woken thread can lag behind the release that woke it.
 */
#ifdef CONFIG_LKPI_CENSUS
struct lkpi_osdep_census {
	uint32_t	down_calls;
	uint64_t	down_iters;
	uint64_t	down_spent_ns;
	uint64_t	down_max_ns;
};

static struct lkpi_osdep_census lkpi_osdep_census_state;

void
lkpi_osdep_census_reset(void)
{

	(void)memset(&lkpi_osdep_census_state, 0, sizeof(lkpi_osdep_census_state));
}

void
lkpi_osdep_census_print(void)
{
	struct lkpi_osdep_census *c = &lkpi_osdep_census_state;

	lkpi_printf(ULOG_INFO_LEVEL,
	    "perf census: D sem down n=%u iters=%llu spent_us=%llu max_us=%llu\n",
	    c->down_calls, c->down_iters, c->down_spent_ns / 1000u,
	    c->down_max_ns / 1000u);
}
#endif	/* CONFIG_LKPI_CENSUS */

int
down_interruptible(struct semaphore *sem)
{
#ifdef CONFIG_LKPI_CENSUS
	uint64_t t0 = lkpi_census_now_ns();
	uint64_t iters = 0u;
#endif

	/*
	 * The face's semaphore is a count and a lock slot; this port keeps the
	 * count and waits by yielding a tick, because there is no wait-queue
	 * object to park on and no signal delivery to interrupt the wait. The
	 * take is the interface's: succeed when the count is positive, consume
	 * it atomically.
	 */
	if (sem == NULL)
		return -EINVAL;
	for (;;) {
		unsigned long long flags = lkpi_spin_lock_irqsave(&sem->lock);

		if (sem->count > 0) {
			sem->count--;
			lkpi_spin_unlock_irqrestore(&sem->lock, flags);
#ifdef CONFIG_LKPI_CENSUS
			{
				uint64_t spent = lkpi_census_now_ns() - t0;

				lkpi_osdep_census_state.down_calls++;
				lkpi_osdep_census_state.down_iters += iters;
				lkpi_osdep_census_state.down_spent_ns += spent;
				if (spent > lkpi_osdep_census_state.down_max_ns)
					lkpi_osdep_census_state.down_max_ns = spent;
			}
#endif
			return 0;
		}
		lkpi_spin_unlock_irqrestore(&sem->lock, flags);
		(void)osDelay(1);
#ifdef CONFIG_LKPI_CENSUS
		iters++;
#endif
	}
}

void
up(struct semaphore *sem)
{
	unsigned long long flags;

	if (sem == NULL)
		return;
	flags = lkpi_spin_lock_irqsave(&sem->lock);
	sem->count++;
	lkpi_spin_unlock_irqrestore(&sem->lock, flags);
}

int
mutex_lock_interruptible(struct mutex *lock)
{

	/*
	 * This port's mutex is a plain CMSIS mutex with no abortable wait, so
	 * the interruptible take is the blocking take; it never reports -EINTR
	 * because no signal delivery exists to interrupt it.
	 */
	if (lock == NULL)
		return -EINVAL;
	lkpi_mutex_lock(lock);
	return 0;
}

/* ------------------------------------------------------------------ */
/* the file seam (driver: osdep_service.c:2117 filp_open, :2151 read)   */
/* ------------------------------------------------------------------ */

struct file *
filp_open(const char *name, int flags, umode_t mode)
{

	/*
	 * This image has no virtual filesystem: the driver's firmware/efuse
	 * file reader gets -ENOENT (the shape it already handles by falling
	 * back), rather than a fake handle that would fail later.
	 */
	(void)name;
	(void)flags;
	(void)mode;
	return ERR_PTR(-ENOENT);
}

int
filp_close(struct file *filp, fl_owner_t id)
{

	(void)filp;
	(void)id;
	return 0;
}

ssize_t
kernel_read(struct file *file, void *buf, size_t count, loff_t *pos)
{

	/* unreachable behind a filp_open that always fails; the honest answer
	 * for a file that cannot be opened is "not found" */
	(void)file;
	(void)buf;
	(void)count;
	(void)pos;
	return -ENOENT;
}

/* ------------------------------------------------------------------ */
/* the process and signal seams (driver: osdep_service_linux.h:524      */
/* rtw_signal_process, signal.h:allow_signal)                           */
/* ------------------------------------------------------------------ */

struct pid *
find_vpid(int nr)
{

	/* no process table on this port: no pid resolves */
	(void)nr;
	return NULL;
}

int
kill_pid(struct pid *pid, int sig, int priv)
{

	/* no pid and no signal delivery: the interface's "no such process" */
	(void)pid;
	(void)sig;
	(void)priv;
	return -ESRCH;
}

void
kernel_sigaction(int sig, __sighandler_t handler)
{

	/*
	 * Installing a signal action needs a per-task action table, which this
	 * port does not carry. allow_signal()/disallow_signal() call this to
	 * mark a kernel thread's own signals; with no delivery, the call has
	 * nothing to install and is inert.
	 */
	(void)sig;
	(void)handler;
}

/* ------------------------------------------------------------------ */
/* the netlink attribute builders (driver: rtw_cfgvendor.c:379, :187)   */
/* ------------------------------------------------------------------ */

int
nla_put_nohdr(struct sk_buff *skb, int attrlen, const void *data)
{
	int len;
	unsigned char *p;

	if (skb == NULL || attrlen < 0)
		return -EINVAL;
	len = (int)NLA_ALIGN((unsigned int)attrlen);
	if (skb_tailroom(skb) < len)
		return -EINVAL; /* no room: the port's errno set has no EMSGSIZE */
	p = skb_put(skb, (unsigned int)len);
	if (attrlen > 0)
		memcpy(p, data, (size_t)attrlen);
	if (len > attrlen)
		memset(p + attrlen, 0, (size_t)(len - attrlen));
	return 0;
}

int
nla_put_u32(struct sk_buff *skb, int attrtype, u32 value)
{
	struct nlattr *nla;

	if (skb == NULL)
		return -EINVAL;
	if (skb_tailroom(skb) < NLA_HDRLEN + (int)sizeof(u32))
		return -EINVAL; /* no room: the port's errno set has no EMSGSIZE */
	nla = (struct nlattr *)skb_put(skb,
	    (unsigned int)(NLA_HDRLEN + (int)sizeof(u32)));
	nla->nla_type = (__u16)attrtype;
	nla->nla_len = (__u16)(NLA_HDRLEN + (int)sizeof(u32));
	memcpy((char *)nla + NLA_HDRLEN, &value, sizeof(value));
	return 0;
}

/* ------------------------------------------------------------------ */
/* the checksum primitives (driver: rtw_br_ext.c:1351, :1354)           */
/* ------------------------------------------------------------------ */

__wsum
csum_partial(const void *buff, int len, __wsum sum)
{
	const u8 *p = buff;
	u32 acc = (u32)sum;

	/* the interface's running sum: 16-bit words in network order, unfolded
	 * (the caller folds it through csum_ipv6_magic) */
	while (len > 1) {
		acc += (u32)(((u32)p[0] << 8) | p[1]);
		p += 2;
		len -= 2;
	}
	if (len > 0)
		acc += (u32)((u32)p[0] << 8);
	return (__wsum)acc;
}

__sum16
csum_ipv6_magic(const struct in6_addr *saddr, const struct in6_addr *daddr,
    __u32 len, __u8 proto, __wsum csum)
{
	const u8 *s = saddr->s6_addr;
	const u8 *d = daddr->s6_addr;
	u32 sum = (u32)csum;
	int i;

	/*
	 * [LNX] net/ipv6/checksum.c:csum_ipv6_magic: fold the IPv6 pseudo-header
	 * (both addresses, the upper-layer length and the next-header value)
	 * into the running sum, then complement. The sum is kept in network
	 * order throughout so the result is the wire checksum.
	 */
	for (i = 0; i < 16; i += 2) {
		sum += (u32)(((u32)s[i] << 8) | s[i + 1]);
		sum += (u32)(((u32)d[i] << 8) | d[i + 1]);
	}
	sum += (len >> 16) & 0xffffu;
	sum += len & 0xffffu;
	sum += (u32)proto & 0xffffu;
	while (sum >> 16)
		sum = (sum & 0xffffu) + (sum >> 16);
	return (__sum16)(~sum & 0xffffu);
}

/* ------------------------------------------------------------------ */
/* the padded copy (driver: hal_sdio.c:41)                              */
/* ------------------------------------------------------------------ */

void
memcpy_and_pad(void *dest, size_t dest_len, const void *src, size_t count,
    int pad)
{
	size_t n = count < dest_len ? count : dest_len;

	memcpy(dest, src, n);
	if (n < dest_len)
		memset((char *)dest + n, pad, dest_len - n);
}

/* ------------------------------------------------------------------ */
/* the initial network namespace (driver: custom_gpio_linux.c:312)      */
/* ------------------------------------------------------------------ */

/*
 * [LNX] net/core/net_namespace.c: `struct net init_net` is the initial
 * namespace every device lookup defaults to. The face declares `struct net`
 * opaque; this port has exactly one namespace, so the record is an empty tag and
 * init_net its single instance, which dev_get_by_name accepts and ignores.
 */
struct net {
	int	_unused;
};

struct net init_net;
