/*
 * @file    host_stubs.c
 * @brief   Host doubles for the platform side of the EHCI port.
 *
 * The translation units under test (the vendored port/ehci/usb_hc_ehci.c and
 * the patched port/ehci/usb_hc_ehci_iso.c) reach out to the platform in
 * exactly the directions supplied here: the CMSIS-RTOS2 osal facade (the
 * critical section and the counting semaphores), the data-cache maintenance
 * calls of the cache-enabled build, the port's console sink, and the board
 * glue the vendored tree declares extern (the low-level hook, the port-speed
 * decoder, the hub wakeup).
 *
 * The critical section is a process-wide recursive lock - the same shape the
 * project's FreeRTOS host backend gives this facade, so the submitter and the
 * completion path are mutually excluded without an interrupt-latency claim
 * (recorded host divergence, see os/cmsis2/cmsis_os2_ext.h).
 *
 * The cache calls are no-ops: the host has coherent caches, and the suite
 * exercises the port's call *order* (clean before link, invalidate before the
 * status read) through the acceptance assertions, not through cache hardware.
 */
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "usbh_core.h"
#include "usbh_hub.h"

/* ---- the critical section ------------------------------------------------ */

static pthread_mutex_t cs_lock;
static pthread_once_t cs_once = PTHREAD_ONCE_INIT;

static void cs_init(void)
{
    pthread_mutexattr_t attr;

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&cs_lock, &attr);
    pthread_mutexattr_destroy(&attr);
}

size_t usb_osal_enter_critical_section(void)
{
    pthread_once(&cs_once, cs_init);
    pthread_mutex_lock(&cs_lock);
    return 0;
}

void usb_osal_leave_critical_section(size_t flag)
{
    (void)flag;
    pthread_mutex_unlock(&cs_lock);
}

/* ---- counting semaphores with a millisecond timeout ---------------------- */

struct host_sem {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    uint32_t count;
};

usb_osal_sem_t usb_osal_sem_create(uint32_t initial_count)
{
    struct host_sem *sem = calloc(1, sizeof(*sem));

    if (sem == NULL) {
        return NULL;
    }
    pthread_mutex_init(&sem->lock, NULL);
    pthread_cond_init(&sem->cond, NULL);
    sem->count = initial_count;
    return (usb_osal_sem_t)sem;
}

void usb_osal_sem_delete(usb_osal_sem_t sem)
{
    struct host_sem *s = (struct host_sem *)sem;

    if (s == NULL) {
        return;
    }
    pthread_mutex_destroy(&s->lock);
    pthread_cond_destroy(&s->cond);
    free(s);
}

int usb_osal_sem_take(usb_osal_sem_t sem, uint32_t timeout)
{
    struct host_sem *s = (struct host_sem *)sem;
    struct timespec ts;

    pthread_mutex_lock(&s->lock);
    while (s->count == 0) {
        if (timeout == 0) {
            /* the zero-timeout take is a try, never a wait */
            pthread_mutex_unlock(&s->lock);
            return -USB_ERR_TIMEOUT;
        }
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += (time_t)(timeout / 1000U);
        ts.tv_nsec += (long)(timeout % 1000U) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        if (pthread_cond_timedwait(&s->cond, &s->lock, &ts) == ETIMEDOUT && s->count == 0) {
            pthread_mutex_unlock(&s->lock);
            return -USB_ERR_TIMEOUT;
        }
    }
    s->count--;
    pthread_mutex_unlock(&s->lock);
    return 0;
}

int usb_osal_sem_give(usb_osal_sem_t sem)
{
    struct host_sem *s = (struct host_sem *)sem;

    pthread_mutex_lock(&s->lock);
    s->count++;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->lock);
    return 0;
}

void usb_osal_msleep(uint32_t delay)
{
    usleep((useconds_t)delay * 1000U);
}

/* ---- cache maintenance (host caches are coherent) ------------------------ */

/* The calls are recorded instead of performed: the host's caches are coherent,
 * so what the suite can assert is the port's call order and arguments (the
 * acceptance assertions), not cache hardware behaviour.  The descriptor
 * maintenance calls interleave with the transfer-buffer ones, so the log lets
 * a case count the calls that name one particular address. */
#define HOST_CACHE_LOG 64
static struct {
    uintptr_t addr;
    size_t size;
} host_cache_inv_log[HOST_CACHE_LOG];
static unsigned host_cache_inv_n;
uintptr_t host_cache_last_invalidate_addr;
size_t host_cache_last_invalidate_size;
int host_cache_invalidates;
uintptr_t host_cache_last_clean_addr;
size_t host_cache_last_clean_size;
int host_cache_cleans;

void host_cache_log_reset(void)
{
    host_cache_inv_n = 0;
    host_cache_last_invalidate_addr = 0;
    host_cache_last_invalidate_size = 0;
    host_cache_invalidates = 0;
    host_cache_last_clean_addr = 0;
    host_cache_last_clean_size = 0;
    host_cache_cleans = 0;
}

unsigned host_cache_invalidations_of(uintptr_t addr)
{
    unsigned hits = 0;

    for (unsigned i = 0; i < host_cache_inv_n; i++) {
        if (host_cache_inv_log[i].addr == addr) {
            hits++;
        }
    }
    return hits;
}

void usb_dcache_clean(uintptr_t addr, size_t size)
{
    host_cache_last_clean_addr = addr;
    host_cache_last_clean_size = size;
    host_cache_cleans++;
}

void usb_dcache_invalidate(uintptr_t addr, size_t size)
{
    host_cache_last_invalidate_addr = addr;
    host_cache_last_invalidate_size = size;
    host_cache_invalidates++;
    if (host_cache_inv_n < HOST_CACHE_LOG) {
        host_cache_inv_log[host_cache_inv_n].addr = addr;
        host_cache_inv_log[host_cache_inv_n].size = size;
        host_cache_inv_n++;
    }
}

void usb_dcache_flush(uintptr_t addr, size_t size)
{
    (void)addr;
    (void)size;
}

/* ---- the port's console sink --------------------------------------------- */

void usbh_console_printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* ---- board glue declared extern by the vendored tree --------------------- */

void usb_hc_low_level_init(struct usbh_bus *bus)
{
    (void)bus;
}

void usb_hc_low_level_deinit(struct usbh_bus *bus)
{
    (void)bus;
}

uint8_t usbh_get_port_speed(struct usbh_bus *bus, const uint8_t port)
{
    (void)bus;
    (void)port;
    return USB_SPEED_HIGH;
}

void usbh_hub_thread_wakeup(struct usbh_hub *hub)
{
    (void)hub;
}

/* The port registers its host-controller driver object (the multi-HC build);
 * nothing in this suite goes through the core's dispatcher, but the object is
 * part of the port's TU and refers to the bus table the core owns. */
struct usbh_bus g_usbhost_bus[CONFIG_USBHOST_MAX_BUS];
