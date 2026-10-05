/*
 * @file   rtthread.h
 * @brief  RT-Thread API shim for the vendored netutils sources.
 *
 * The vendored netutils code is RT-Thread's, written against rtthread.h.
 * Rather than rewriting every component, this shadow header (found first on
 * the netutils world's include path) provides the rt_* surface this trunk
 * actually needs, mapped onto the image's real substrate:
 *
 *   rt_tick_get          -> osKernelGetTickCount (tick == 1 ms, 1:1)
 *   rt_malloc/free/...   -> the image's libc malloc family (TLSF-backed;
 *                           os/threadx/heap.c provides them under the
 *                           standard names - one allocator image-wide)
 *   rt_kprintf           -> mpaland/printf via minilibc, routed per session
 *                           (netutils_shim.c; unbound sessions go to the
 *                           locked console sink)
 *   rt_thread_*          -> CMSIS-RTOS2 threads (deferred create: RT-Thread
 *                           creates suspended and then calls
 *                           rt_thread_startup; this port has no
 *                           osThreadSuspend, so startup() does the real
 *                           osThreadNew)
 *   rt_sem/rt_mutex/rt_mb-> CMSIS-RTOS2 semaphores/mutexes/message queues
 *   rt_memcpy/...        -> the libc str/mem names (minilibc)
 *
 * Deliberately NOT shimmed (S7b 裁剪面, spec FR-6: 裁剪集零用量): rt_sem/
 * rt_mutex/rt_mb (IPC 三族), rt_thread_create/startup/self (线程创建对),
 * rt_device_* (tcpdump 不进), rt_work_* (ntp 不进), rt_ringbuffer_* (telnet
 * 不进), rt_console_set_device / finsh runtime API (telnet 不进), 墙钟
 * (netutils_ntp_* - ntp/tcpdump 均不进)。
 */

#ifndef FWC_NETUTILS_SHIM_RTTHREAD_H
#define FWC_NETUTILS_SHIM_RTTHREAD_H

#include <rtconfig.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>		/* rt_snprintf -> snprintf */
#include <stdlib.h>		/* rt_malloc -> malloc family */

#ifdef __cplusplus
extern "C" {
#endif

/* --- types ----------------------------------------------------------------- */

typedef uint8_t		rt_uint8_t;
typedef uint16_t	rt_uint16_t;
typedef uint32_t	rt_uint32_t;
typedef uint64_t	rt_uint64_t;
typedef int8_t		rt_int8_t;
typedef int16_t		rt_int16_t;
typedef int32_t		rt_int32_t;
typedef int64_t		rt_int64_t;
typedef size_t		rt_size_t;
typedef int		rt_err_t;
typedef int		rt_bool_t;
/* tx_time_get() yields ULONG; on this aarch64 LP64 toolchain that is 64-bit.
 * rt_tick_t being wider than RT-Thread's 32-bit only widens intermediate
 * arithmetic - the values themselves are tick counts that fit either way. */
typedef unsigned long	rt_tick_t;
typedef long		rt_base_t;
typedef unsigned long	rt_ubase_t;
typedef void *		rt_thread_t;
typedef void *		rt_sem_t;
typedef void *		rt_mutex_t;
typedef void *		rt_mailbox_t;
typedef void *		rt_device_t;

/* --- constants -------------------------------------------------------------- */

#define RT_NULL				NULL
#define RT_EOK				0
#define RT_ERROR			1
#define RT_TRUE				1
#define RT_FALSE			0
#define RT_WAITING_FOREVER		-1
#define RT_WAITING_NO			0
#define RT_IPC_FLAG_FIFO		0x00
#define RT_IPC_FLAG_PRIO		0x01

/* --- libc name maps ---------------------------------------------------------- */

#define rt_memcpy			memcpy
#define rt_memset			memset
#define rt_memcmp			memcmp
#define rt_memmove			memmove
#define rt_strcpy			strcpy
#define rt_strncpy			strncpy
#define rt_strcmp			strcmp
#define rt_strncmp			strncmp
#define rt_strlen			strlen
#define rt_strnlen			strnlen
#define rt_snprintf			snprintf
#define rt_vsnprintf			vsnprintf

/* --- attributes -------------------------------------------------------------- */

#define RT_WEAK
#define rt_weak				RT_WEAK
#define RT_USED				__attribute__((used))
#define RT_UNUSED			__attribute__((unused))
#define RT_ALIGN(size, align)		(((size) + (align) - 1) & ~((align) - 1))
#define RT_ALIGN_DOWN(size, align)	((size) & ~((align) - 1))

/* heap.c owns __assert_func for the freestanding build; a failed RT_ASSERT
 * names the file and line instead of vanishing. */
void __assert_func(const char *file, int line, const char *func,
		   const char *expr);
#define RT_ASSERT(x)							\
	do {								\
		if (!(x)) {						\
			__assert_func(__FILE__, __LINE__,		\
				      __func__, #x);			\
		}							\
	} while (0)

/* --- output ------------------------------------------------------------------ */

int rt_kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Session binding for output routing: a telnet (or UART) shell session
 * binds its executing thread, and rt_kprintf/LOG_* from that thread land on
 * the session instead of the UART. Nothing binds before the telnet feat. */
void netutils_shim_bind_session(void *csh);
void netutils_shim_unbind_session(void);

/* --- heap --------------------------------------------------------------------- */

void *rt_malloc(rt_size_t size);
void rt_free(void *ptr);
void *rt_calloc(rt_size_t count, rt_size_t size);
void *rt_realloc(void *ptr, rt_size_t size);
char *rt_strdup(const char *s);

/* --- tick / time ---------------------------------------------------------------- */

rt_tick_t rt_tick_get(void);
rt_tick_t rt_tick_from_millisecond(rt_int32_t ms);

/* --- threads (S7b 裁剪面：只留 delay；create/startup/self 不迁) ---------------------- */

rt_err_t rt_thread_delay(rt_tick_t tick);
rt_err_t rt_thread_mdelay(rt_int32_t ms);

/* --- interrupts ---------------------------------------------------------------------- */

rt_base_t rt_hw_interrupt_disable(void);
void rt_hw_interrupt_enable(rt_base_t level);

#ifdef __cplusplus
}
#endif

#endif /* FWC_NETUTILS_SHIM_RTTHREAD_H */
