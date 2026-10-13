/**
 * @file    stubs.c
 * @brief   Host doubles for the seams the UVC stream engine sits on.
 * @author  zhugengyu
 * @date    14.10.2026
 * @design  seam doubles - ref tests/host/ehci-iso-schedule-test/host_stubs.c
 *          (the same shape: the suite drives the real translation unit and
 *          supplies only the platform seams) and the class-facing side: the
 *          video class instance, the probe/open helpers the engine calls and
 *          the urb transport.  The device model is the board's camera ladder
 *          (the ground-truth descriptors): alternate settings 1..11 with
 *          wMaxPacketSize 192/384/512/640/800/944/1280/1600/1984/2688/3060.
 *
 * The osal doubles are pthreads: counting semaphores with timeouts, a plain
 * mutex, and threads whose self-delete form exits the pthread (the pump uses
 * usb_osal_thread_delete(NULL), the hub thread's convention).
 */

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "usbh_core.h"
#include "usbh_video.h"
#include "usbh_uvc_stream.h"

/* ------------------------------------------------------------------------
 * osal doubles
 * --------------------------------------------------------------------- */

struct host_sem {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    uint32_t count;
    uint32_t max;
};

usb_osal_sem_t usb_osal_sem_create(uint32_t initial_count)
{
    return usb_osal_sem_create_counting(initial_count ? initial_count : 1);
}

usb_osal_sem_t usb_osal_sem_create_counting(uint32_t max_count)
{
    struct host_sem *sem = calloc(1, sizeof(*sem));

    if (sem == NULL) {
        return NULL;
    }
    pthread_mutex_init(&sem->lock, NULL);
    pthread_cond_init(&sem->cond, NULL);
    sem->max = max_count ? max_count : 1;
    return sem;
}

void usb_osal_sem_delete(usb_osal_sem_t handle)
{
    struct host_sem *sem = handle;

    if (sem != NULL) {
        pthread_mutex_destroy(&sem->lock);
        pthread_cond_destroy(&sem->cond);
        free(sem);
    }
}

int usb_osal_sem_take(usb_osal_sem_t handle, uint32_t timeout)
{
    struct host_sem *sem = handle;
    int ret = 0;

    pthread_mutex_lock(&sem->lock);
    while (sem->count == 0) {
        if (timeout == USB_OSAL_WAITING_FOREVER) {
            pthread_cond_wait(&sem->cond, &sem->lock);
        } else {
            struct timespec deadline;
            struct timeval now;

            gettimeofday(&now, NULL);
            deadline.tv_sec = now.tv_sec + timeout / 1000u;
            deadline.tv_nsec = (long)now.tv_usec * 1000 + (long)(timeout % 1000u) * 1000000;
            if (deadline.tv_nsec >= 1000000000L) {
                deadline.tv_sec++;
                deadline.tv_nsec -= 1000000000L;
            }
            if (pthread_cond_timedwait(&sem->cond, &sem->lock, &deadline) == ETIMEDOUT) {
                ret = -USB_ERR_TIMEOUT;
                break;
            }
        }
    }
    if (ret == 0) {
        sem->count--;
    }
    pthread_mutex_unlock(&sem->lock);
    return ret;
}

int usb_osal_sem_give(usb_osal_sem_t handle)
{
    struct host_sem *sem = handle;

    pthread_mutex_lock(&sem->lock);
    if (sem->count < sem->max) {
        sem->count++;
    }
    pthread_cond_signal(&sem->cond);
    pthread_mutex_unlock(&sem->lock);
    return 0;
}

usb_osal_mutex_t usb_osal_mutex_create(void)
{
    pthread_mutex_t *mutex = calloc(1, sizeof(*mutex));

    if (mutex != NULL) {
        pthread_mutex_init(mutex, NULL);
    }
    return mutex;
}

void usb_osal_mutex_delete(usb_osal_mutex_t handle)
{
    pthread_mutex_t *mutex = handle;

    if (mutex != NULL) {
        pthread_mutex_destroy(mutex);
        free(mutex);
    }
}

int usb_osal_mutex_take(usb_osal_mutex_t handle)
{
    return pthread_mutex_lock(handle) == 0 ? 0 : -USB_ERR_IO;
}

int usb_osal_mutex_give(usb_osal_mutex_t handle)
{
    return pthread_mutex_unlock(handle) == 0 ? 0 : -USB_ERR_IO;
}

struct host_thread {
    pthread_t tid;
    usb_thread_entry_t entry;
    void *arg;
};

static void *host_thread_trampoline(void *handle)
{
    struct host_thread *thread = handle;

    thread->entry(thread->arg);
    return NULL;
}

usb_osal_thread_t usb_osal_thread_create(const char *name, uint32_t stack_size, uint32_t prio,
                                         usb_thread_entry_t entry, void *args)
{
    struct host_thread *thread = calloc(1, sizeof(*thread));

    (void)name;
    (void)stack_size;
    (void)prio;
    if (thread == NULL) {
        return NULL;
    }
    thread->entry = entry;
    thread->arg = args;
    if (pthread_create(&thread->tid, NULL, host_thread_trampoline, thread) != 0) {
        free(thread);
        return NULL;
    }
    return thread;
}

void usb_osal_thread_delete(usb_osal_thread_t handle)
{
    if (handle == NULL) {
        /* the engine's pump deletes itself */
        pthread_exit(NULL);
        return;
    }
    pthread_cancel(((struct host_thread *)handle)->tid);
    pthread_join(((struct host_thread *)handle)->tid, NULL);
    free(handle);
}

void usb_osal_msleep(uint32_t delay)
{
    struct timespec ts = { .tv_sec = (time_t)(delay / 1000u),
                           .tv_nsec = (long)(delay % 1000u) * 1000000L };

    nanosleep(&ts, NULL);
}

void *usb_osal_malloc(size_t size)
{
    return malloc(size);
}

void usb_osal_free(void *ptr)
{
    free(ptr);
}

size_t usb_osal_enter_critical_section(void)
{
    return 0;
}

void usb_osal_leave_critical_section(size_t flag)
{
    (void)flag;
}

/* The board's console sink (the stack's diagnostics reach it through
 * CONFIG_USB_PRINTF); the suite prints them to stdout. */
void usbh_console_printf(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

/* ------------------------------------------------------------------------
 * The device model: the board camera's streaming ladder
 * --------------------------------------------------------------------- */

static struct usbh_bus g_bus;
struct usbh_hubport g_hport;

static const uint16_t g_ladder[11] = {
    0x00c0, /* 192 */
    0x0180, /* 384 */
    0x0200, /* 512 */
    0x0280, /* 640 */
    0x0320, /* 800 */
    0x03b0, /* 944 */
    0x0a80, /* 1280 = 2 x 640 */
    0x0b20, /* 1600 = 2 x 800 */
    0x0be0, /* 1984 = 2 x 992 */
    0x1380, /* 2688 = 3 x 896 */
    0x13fc, /* 3060 = 3 x 1020 */
};

#define STUB_MAX_PENDING 8

/* The submitted urbs waiting for the test to complete them. */
struct usbh_urb *g_pending[STUB_MAX_PENDING];
int g_pending_n;
uint16_t g_frame;

/* The probe answer the fake device gives (bytes per USB frame). */
uint32_t g_stub_want = 640;
/* What the stub observed. */
uint8_t g_stub_open_alt;
uint32_t g_stub_probe_calls;
uint8_t g_stub_probe_formatidx, g_stub_probe_frameidx;
uint32_t g_stub_probe_interval;

void stub_device_reset(void)
{
    memset(&g_hport, 0, sizeof(g_hport));
    memset(&g_bus, 0, sizeof(g_bus));
    g_hport.bus = &g_bus;
    g_hport.connected = true;
    g_hport.dev_addr = 3;
    g_hport.speed = USB_SPEED_HIGH;

    struct usbh_interface *intf = &g_hport.config.intf[1];

    intf->altsetting_num = 12;
    for (uint8_t alt = 1; alt < 12; alt++) {
        struct usb_endpoint_descriptor *ep = &intf->altsetting[alt].ep[0].ep_desc;

        ep->bLength = 7;
        ep->bDescriptorType = USB_DESCRIPTOR_TYPE_ENDPOINT;
        ep->bmAttributes = USB_ENDPOINT_TYPE_ISOCHRONOUS;
        ep->bEndpointAddress = 0x81;
        ep->wMaxPacketSize = g_ladder[alt - 1];
        ep->bInterval = 1;
    }

    g_stub_open_alt = 0;
    g_stub_probe_calls = 0;
    g_stub_probe_formatidx = 0;
    g_stub_probe_frameidx = 0;
    g_stub_probe_interval = 0;
    g_frame = 0;
    g_pending_n = 0;
}

/* ------------------------------------------------------------------------
 * The class-facing helpers the engine calls
 * --------------------------------------------------------------------- */

int usbh_video_open(struct usbh_video *video_class, uint8_t format_type,
                    uint16_t wWidth, uint16_t wHeight, uint8_t altsetting)
{
    struct usb_endpoint_descriptor *ep =
        &video_class->hport->config.intf[video_class->data_intf].altsetting[altsetting].ep[0].ep_desc;
    uint16_t mps = ep->wMaxPacketSize & USB_MAXPACKETSIZE_MASK;
    uint8_t mult = (ep->wMaxPacketSize & USB_MAXPACKETSIZE_ADDITIONAL_TRANSCATION_MASK) >>
                   USB_MAXPACKETSIZE_ADDITIONAL_TRANSCATION_SHIFT;

    (void)format_type;
    (void)wWidth;
    (void)wHeight;
    g_stub_open_alt = altsetting;
    video_class->is_opened = true;
    video_class->isoin = ep;
    video_class->isoin_mps = (uint16_t)(mps * (mult + 1));
    return 0;
}

int usbh_video_close(struct usbh_video *video_class)
{
    video_class->is_opened = false;
    return 0;
}

int usbh_videostreaming_set_cur_probe(struct usbh_video *video_class, uint8_t formatindex,
                                      uint8_t frameindex, uint32_t dwFrameInterval)
{
    g_stub_probe_calls++;
    g_stub_probe_formatidx = formatindex;
    g_stub_probe_frameidx = frameindex;
    g_stub_probe_interval = dwFrameInterval;
    /* the device answers the probe with the bandwidth the configuration needs */
    video_class->probe.dwMaxPayloadTransferSize = g_stub_want;
    return 0;
}

int usbh_videostreaming_get_cur_probe(struct usbh_video *video_class)
{
    (void)video_class;
    return 0;
}

/* ------------------------------------------------------------------------
 * The urb transport: submissions are queued for the test to complete
 * --------------------------------------------------------------------- */

int usbh_submit_urb(struct usbh_urb *urb)
{
    if (g_pending_n >= STUB_MAX_PENDING) {
        return -USB_ERR_NOMEM;
    }
    urb->hcpriv = (void *)1;
    urb->start_frame = ++g_frame;
    g_pending[g_pending_n++] = urb;
    return 0;
}

static void pending_remove(int index)
{
    for (int i = index; i < g_pending_n - 1; i++) {
        g_pending[i] = g_pending[i + 1];
    }
    g_pending_n--;
}

int usbh_kill_urb(struct usbh_urb *urb)
{
    for (int i = 0; i < g_pending_n; i++) {
        if (g_pending[i] == urb) {
            pending_remove(i);
            break;
        }
    }
    urb->hcpriv = NULL;
    urb->errorcode = -USB_ERR_SHUTDOWN;
    if (urb->complete != NULL) {
        urb->complete(urb->arg, -USB_ERR_SHUTDOWN);
    }
    return 0;
}
