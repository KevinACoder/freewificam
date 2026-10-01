/*
 * @file   netutils_shim.c
 * @brief  RT-Thread API realizations for the vendored netutils sources.
 *
 * See shim/rtthread.h for the mapping table. Two facts this file is built
 * around:
 *
 *  - tick parity: the kernel tick is 1 ms (CONFIG_TX_TICKS_PER_SECOND=
 *    1000, spec S7b FR-7), which matches RT-Thread's
 *    RT_TICK_PER_SECOND=1000, so tick values cross the shim unchanged
 *    (delays, tick-difference timing).
 *  - output routing: rt_kprintf and the LOG_* macros must land on the shell
 *    session that ran the command, not always on the UART. A session binds
 *    its shell task's thread id (netutils_shim_bind_session); output from a
 *    bound thread goes to that session's csh_printf, everything else goes
 *    to the locked console sink. The map is written once at session setup
 *    (thread context) and read per output call under a short critical
 *    section - the telnet feat is what fills it.
 */

#include <rtthread.h>
#include <cmsis_os2.h>
#include "log.h"
#include "csh.h"

/* --- session-routed output ------------------------------------------------- */

#define NETUTILS_SESSION_MAX	4

struct netutils_session {
	void *thread;		/* osThreadId_t of the shell task */
	void *csh;		/* chry_shell_t of the session */
};

static struct netutils_session sessions[NETUTILS_SESSION_MAX];

void netutils_shim_bind_session(void *csh)
{
	rt_base_t level = rt_hw_interrupt_disable();
	void *self = osThreadGetId();
	uint32_t i;

	for (i = 0; i < NETUTILS_SESSION_MAX; i++) {
		if (sessions[i].thread == self) {
			sessions[i].csh = csh;
			break;
		}
	}
	if (i == NETUTILS_SESSION_MAX) {
		for (i = 0; i < NETUTILS_SESSION_MAX; i++) {
			if (sessions[i].thread == NULL) {
				sessions[i].thread = self;
				sessions[i].csh = csh;
				break;
			}
		}
	}
	rt_hw_interrupt_enable(level);
}

void netutils_shim_unbind_session(void)
{
	rt_base_t level = rt_hw_interrupt_disable();
	void *self = osThreadGetId();
	uint32_t i;

	for (i = 0; i < NETUTILS_SESSION_MAX; i++) {
		if (sessions[i].thread == self) {
			sessions[i].thread = NULL;
			sessions[i].csh = NULL;
		}
	}
	rt_hw_interrupt_enable(level);
}

static void netutils_output(const char *buf)
{
	rt_base_t level = rt_hw_interrupt_disable();
	void *self = osThreadGetId();
	void *csh = NULL;
	uint32_t i;

	for (i = 0; i < NETUTILS_SESSION_MAX; i++) {
		if (sessions[i].thread == self) {
			csh = sessions[i].csh;
			break;
		}
	}
	rt_hw_interrupt_enable(level);

	if (csh != NULL) {
		csh_printf(csh, "%s", buf);
	} else {
		board_console_write_raw(buf, (unsigned int)strlen(buf));
	}
}

int rt_kprintf(const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	netutils_output(buf);
	return n;
}

void netutils_shim_log(const char *level, const char *fmt, ...)
{
	char buf[280];
	size_t off;
	va_list ap;

	va_start(ap, fmt);
	off = (size_t)snprintf(buf, sizeof(buf), "[%s/netutils] ", level);
	(void)vsnprintf(buf + off, sizeof(buf) - off, fmt, ap);
	va_end(ap);
	netutils_output(buf);
}

/* --- heap ------------------------------------------------------------------ */

/* The libc names resolve to the image's TLSF-backed malloc family (the
 * libbsd adapter's host-world libc unit provides them for the whole
 * image); no second allocator is created here - cross-freeing between the
 * netutils world and the rest of the tree must stay safe. */

void *rt_malloc(rt_size_t size)
{
	return malloc(size);
}

void rt_free(void *ptr)
{
	free(ptr);
}

void *rt_calloc(rt_size_t count, rt_size_t size)
{
	return calloc(count, size);
}

void *rt_realloc(void *ptr, rt_size_t size)
{
	return realloc(ptr, size);
}

char *rt_strdup(const char *s)
{
	size_t len;
	char *copy;

	if (s == NULL) {
		return NULL;
	}
	len = strlen(s) + 1U;
	copy = malloc(len);
	if (copy != NULL) {
		memcpy(copy, s, len);
	}
	return copy;
}

/* --- str/mem realizations --------------------------------------------------
 *
 * The macros in rtthread.h serve TUs that include the shim header (ping.c);
 * the tftp core is deliberately RTOS-free and calls the rt_mem... and
 * rt_sprintf names bare, so they must also exist as functions. #undef first:
 * this file itself includes rtthread.h and the macros would otherwise
 * rewrite these definitions into libc-name collisions.
 */

#undef rt_memset
#undef rt_memcpy
#undef rt_memmove
#undef rt_memcmp
#undef rt_strcpy
#undef rt_strcat
#undef rt_strlen

void *rt_memset(void *s, int c, rt_size_t n)
{
	return memset(s, c, n);
}

void *rt_memcpy(void *dest, const void *src, rt_size_t n)
{
	return memcpy(dest, src, n);
}

void *rt_memmove(void *dest, const void *src, rt_size_t n)
{
	return memmove(dest, src, n);
}

int rt_memcmp(const void *a, const void *b, rt_size_t n)
{
	return memcmp(a, b, n);
}

char *rt_strcpy(char *dest, const char *src)
{
	return strcpy(dest, src);
}

char *rt_strcat(char *dest, const char *src)
{
	return strcat(dest, src);
}

rt_size_t rt_strlen(const char *s)
{
	return strlen(s);
}

int rt_sprintf(char *buf, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	/* unbounded by contract; the bound only stops a runaway format from
	 * running off the image - callers pass paths and short labels */
	n = vsnprintf(buf, 4096u, fmt, ap);
	va_end(ap);
	return n;
}

/* --- tick ------------------------------------------------------------------ */

rt_tick_t rt_tick_get(void)
{
	return (rt_tick_t)osKernelGetTickCount();
}

rt_tick_t rt_tick_from_millisecond(rt_int32_t ms)
{
	/* tick == 1 ms; negative means "forever" on RT-Thread and osWaitForever
	 * (-1 in CMSIS value space) means the same here. */
	return (ms < 0) ? (rt_tick_t)RT_WAITING_FOREVER : (rt_tick_t)ms;
}

/* --- threads (S7b 裁剪面：只留 delay；create/startup/self 不迁，FR-6) -------- */

rt_err_t rt_thread_delay(rt_tick_t tick)
{
	return (osDelay((uint32_t)tick) == osOK) ? RT_EOK : -RT_ERROR;
}

rt_err_t rt_thread_mdelay(rt_int32_t ms)
{
	return (osDelay((uint32_t)ms) == osOK) ? RT_EOK : -RT_ERROR;
}

/* --- interrupts (平移改写：tx_interrupt_control -> osKernelLock 深度令牌) ------ */

rt_base_t rt_hw_interrupt_disable(void)
{
	return (rt_base_t)osKernelLock();
}

void rt_hw_interrupt_enable(rt_base_t level)
{
	(void)osKernelRestoreLock((int32_t)level);
}
