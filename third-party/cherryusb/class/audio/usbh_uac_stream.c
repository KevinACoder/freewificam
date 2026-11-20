/*
 * Copyright (c) 2026, zhugengyu
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-side UAC1 microphone capture: one isochronous urb of 16 one-millisecond
 * packets on the AudioStreaming IN endpoint, copied into a SPSC PCM ring, and
 * resubmitted from the urb's completion callback.
 *
 * Why the resubmit is in the completion callback and not in a pump thread: the
 * DWC2 engine completes the urb at its last packet, and the next urb must be
 * armed before the next 1 ms frame or that frame's audio is lost.  A thread
 * wake can be delayed past the frame boundary by another runnable task (the
 * video pump shares the image); an immediate resubmit from the callback runs
 * inside the same interrupt, with only register writes between the two urbs.
 * The callback's work is bounded: 16 packets x at most 36 bytes, plus one
 * submit.
 *
 * The counters are the honesty layer: a packet with a transport error, a
 * zero-length packet, a ring overrun and a frame the host did not schedule are
 * four different defects and are counted separately.
 */
#include "usbh_core.h"
#include "usbh_uac.h"
#include "usbh_uac_stream.h"

#undef USB_DBG_TAG
#define USB_DBG_TAG "uac_stream"
#include "usb_log.h"

/* The 10-bit periodic frame counter is the only common clock between urbs. */
#define UAC_FRAME_MASK 0x3ffU

struct uac_stream {
    struct usbh_uac *uac;
    volatile bool running;

    struct usbh_urb *urb;
    uint8_t *packet_mem;

    uint8_t *ring;
    uint32_t ring_size;
    volatile uint32_t head;
    volatile uint32_t tail;
    usb_osal_sem_t data_sem;

    uint16_t last_start;
    bool have_last_start;
    uint32_t boundaries;

    struct uac_stream_stats stats;
};

static struct uac_stream uac_stream;

/* ------------------------------------------------------------------------
 * PCM ring: one producer (the urb completion, interrupt context) and one
 * consumer (the reader).  The counters are monotonic and wrap in 32 bits; the
 * used count is their difference, which stays far below 2^31 for any ring this
 * line allocates.  No lock is needed on one core with one writer and one
 * reader; the data semaphore only says "the ring became non-empty".
 * --------------------------------------------------------------------- */

static void ring_write(struct uac_stream *s, const uint8_t *data, uint32_t len)
{
    uint32_t used = s->head - s->tail;
    uint32_t room = s->ring_size - used;
    uint32_t first;

    if (room == 0U) {
        s->stats.overrun += len;
        return;
    }
    if (len > room) {
        s->stats.overrun += len - room;
        len = room;
    }

    first = s->ring_size - (s->head % s->ring_size);
    if (first > len) {
        first = len;
    }
    memcpy(s->ring + (s->head % s->ring_size), data, first);
    if (len > first) {
        memcpy(s->ring, data + first, len - first);
    }
    s->head += len;
    s->stats.bytes += len;
    if (used == 0U) {
        usb_osal_sem_give(s->data_sem);
    }
}

static uint32_t ring_read(struct uac_stream *s, uint8_t *data, uint32_t len)
{
    uint32_t used = s->head - s->tail;
    uint32_t first;

    if (len > used) {
        len = used;
    }
    if (len == 0U) {
        return 0;
    }
    first = s->ring_size - (s->tail % s->ring_size);
    if (first > len) {
        first = len;
    }
    memcpy(data, s->ring + (s->tail % s->ring_size), first);
    if (len > first) {
        memcpy(data + first, s->ring, len - first);
    }
    s->tail += len;
    return len;
}

/* ------------------------------------------------------------------------
 * The urb path
 * --------------------------------------------------------------------- */

static void uac_note_start(struct uac_stream *s)
{
    uint16_t delta;

    if (!s->have_last_start) {
        s->last_start = (uint16_t)(s->urb->start_frame & UAC_FRAME_MASK);
        s->have_last_start = true;
        return;
    }
    delta = (uint16_t)((s->urb->start_frame - s->last_start) & UAC_FRAME_MASK);
    s->last_start = (uint16_t)(s->urb->start_frame & UAC_FRAME_MASK);
    if (s->boundaries == 0U) {
        /* The first boundary after start: recorded separately so a startup
         * artefact cannot hide inside the steady-state gap count. */
        s->stats.gap_first = (delta > CONFIG_UAC_STREAM_PACKETS) ?
                             (uint32_t)delta - CONFIG_UAC_STREAM_PACKETS : 0U;
    }
    s->boundaries++;
    /* A boundary seen in the same frame the previous packet retired gives a
     * delta of packets-1; only a larger delta is a frame the host skipped. */
    if ((delta > CONFIG_UAC_STREAM_PACKETS) && (delta < 0x200U)) {
        uint32_t missed = (uint32_t)delta - CONFIG_UAC_STREAM_PACKETS;

        s->stats.gaps += missed;
        if (missed > s->stats.gap_max) {
            s->stats.gap_max = missed;
        }
    }
}

/* One submission attempt.  Called from the completion callback (interrupt
 * context) and from start() (thread context); both paths take the DWC2
 * engine's critical section, which this port maps to the FromISR pair when
 * IPSR is non-zero.  There is no sleep and no retry here for that reason: a
 * submit that fails at a boundary is counted and the capture stops rather than
 * blocking the interrupt. */
static bool uac_submit(struct uac_stream *s)
{
    struct usbh_urb *urb = s->urb;
    int ret;

    for (uint32_t p = 0; p < urb->num_of_iso_packets; p++) {
        urb->iso_packet[p].actual_length = 0;
        urb->iso_packet[p].errorcode = 0;
    }
    urb->actual_length = 0;
    urb->errorcode = 0;
    urb->timeout = 0;

    ret = usbh_submit_urb(urb);
    if (ret == 0) {
        uac_note_start(s);
        return true;
    }
    s->stats.err++;
    return false;
}

static void uac_consume(struct uac_stream *s)
{
    struct usbh_urb *urb = s->urb;

    for (uint32_t i = 0; i < urb->num_of_iso_packets; i++) {
        struct usbh_iso_frame_packet *packet = &urb->iso_packet[i];

        if (packet->errorcode < 0) {
            if (packet->errorcode == -USB_ERR_SHUTDOWN) {
                /* The stop path marks the remaining packets of the in-flight
                 * urb with this code; they were never attempted, so they are
                 * not transport errors. */
                s->stats.shutdown++;
                continue;
            }
            s->stats.err++;
            s->stats.err_run++;
            if (s->stats.err_run > s->stats.err_max_run) {
                s->stats.err_max_run = s->stats.err_run;
            }
            if (i == 0U) {
                s->stats.err_first++;
            } else if (i == (urb->num_of_iso_packets - 1U)) {
                s->stats.err_last++;
            } else {
                s->stats.err_mid++;
            }
            continue;
        }
        s->stats.err_run = 0;
        s->stats.packets++;
        if (packet->actual_length == 0U) {
            s->stats.empty++;
            continue;
        }
        ring_write(s, packet->transfer_buffer, packet->actual_length);
    }
    s->stats.urbs++;
}

static void uac_urb_complete(void *arg, int nbytes)
{
    struct uac_stream *s = arg;

    (void)nbytes;
    uac_consume(s);
    if (!s->running) {
        return;
    }
    if (!uac_submit(s)) {
        s->running = false;
    }
}

/* ------------------------------------------------------------------------
 * Public interface
 * --------------------------------------------------------------------- */

int usbh_uac_stream_attach(struct usbh_uac *uac)
{
    if ((uac == NULL) || (uac->hport == NULL)) {
        return -USB_ERR_NODEV;
    }
    uac_stream.uac = uac;
    return 0;
}

int usbh_uac_stream_start(void)
{
    struct uac_stream *s = &uac_stream;
    struct usbh_uac *uac = s->uac;
    uint32_t pkt_size, urb_size, ring_size;

    if ((uac == NULL) || uac->error != 0 || !uac->is_opened) {
        return -USB_ERR_NODEV;
    }
    if (s->running) {
        return 0;
    }
    if ((uac->as_ep_in == false) || (uac->isoin == NULL) || (uac->as_mps == 0U)) {
        return -USB_ERR_NODEV;
    }

    memset(&s->stats, 0, sizeof(s->stats));
    s->stats.rate = uac->rate;
    s->stats.channels = uac->channels;
    s->stats.subframe = uac->as_subframe_size;
    s->stats.mps = uac->as_mps;

    pkt_size = ((uint32_t)uac->as_mps + CONFIG_USB_ALIGN_SIZE - 1U) &
               ~((uint32_t)CONFIG_USB_ALIGN_SIZE - 1U);
    urb_size = sizeof(struct usbh_urb) +
               ((uint32_t)CONFIG_UAC_STREAM_PACKETS * sizeof(struct usbh_iso_frame_packet));

    ring_size = (uac->rate * (uint32_t)uac->channels * (uint32_t)uac->as_subframe_size *
                 (uint32_t)CONFIG_UAC_RING_MS) / 1000U;
    ring_size = (ring_size + 63U) & ~63U;
    if (ring_size < 1024U) {
        ring_size = 1024U;
    }

    s->urb = usb_osal_malloc(urb_size);
    s->packet_mem = usb_osal_malloc((pkt_size * CONFIG_UAC_STREAM_PACKETS) + CONFIG_USB_ALIGN_SIZE);
    s->ring = usb_osal_malloc(ring_size);
    if ((s->urb == NULL) || (s->packet_mem == NULL) || (s->ring == NULL)) {
        USB_LOG_ERR("start: allocation failed (urb=%p pkt=%p ring=%p)\r\n",
                    s->urb, s->packet_mem, s->ring);
        goto fail;
    }
    memset(s->urb, 0, urb_size);

    s->urb->hport = uac->hport;
    s->urb->ep = uac->isoin;
    s->urb->num_of_iso_packets = CONFIG_UAC_STREAM_PACKETS;
    s->urb->complete = uac_urb_complete;
    s->urb->arg = s;
    {
        uint8_t *base = (uint8_t *)(((uintptr_t)s->packet_mem + CONFIG_USB_ALIGN_SIZE - 1U) &
                                    ~(uintptr_t)(CONFIG_USB_ALIGN_SIZE - 1U));

        for (uint32_t p = 0; p < CONFIG_UAC_STREAM_PACKETS; p++) {
            s->urb->iso_packet[p].transfer_buffer = base + (p * pkt_size);
            s->urb->iso_packet[p].transfer_buffer_length = uac->as_mps;
        }
    }

    s->ring_size = ring_size;
    s->head = 0;
    s->tail = 0;
    s->have_last_start = false;
    s->boundaries = 0;
    s->data_sem = usb_osal_sem_create_counting(1);
    if (s->data_sem == NULL) {
        USB_LOG_ERR("start: semaphore creation failed\r\n");
        goto fail;
    }

    /* The camera's audio engine starts a few milliseconds after the
     * SET_INTERFACE/SET_CUR pair; arming the first urb before that yields a
     * whole urb of empty packets (measured: one 16-packet burst at start).
     * The delay is inside start, so the caller's measurement window begins
     * after the device is actually streaming. */
    usb_osal_msleep(50);

    /* The banner is printed before the first urb: the console sink holds the
     * print lock with interrupts masked, and a line printed while an urb is in
     * flight costs the frames the lock spans. */
    USB_LOG_INFO("capture started rate=%u ch=%u sub=%u mps=%u ring=%u\r\n",
                 (unsigned)uac->rate, (unsigned)uac->channels,
                 (unsigned)uac->as_subframe_size, (unsigned)uac->as_mps,
                 (unsigned)ring_size);

    s->running = true;
    if (!uac_submit(s)) {
        USB_LOG_ERR("start: first urb refused\r\n");
        s->running = false;
        goto fail;
    }
    return 0;

fail:
    s->running = false;
    if ((s->urb != NULL) && (s->urb->hcpriv != NULL)) {
        (void)usbh_kill_urb(s->urb);
    }
    if (s->data_sem != NULL) {
        usb_osal_sem_delete(s->data_sem);
        s->data_sem = NULL;
    }
    if (s->urb != NULL) {
        usb_osal_free(s->urb);
        s->urb = NULL;
    }
    if (s->packet_mem != NULL) {
        usb_osal_free(s->packet_mem);
        s->packet_mem = NULL;
    }
    if (s->ring != NULL) {
        usb_osal_free(s->ring);
        s->ring = NULL;
    }
    s->ring_size = 0;
    return -USB_ERR_NOMEM;
}

void usbh_uac_stream_stop(void)
{
    struct uac_stream *s = &uac_stream;

    if (!s->running && (s->urb == NULL)) {
        return;
    }
    s->running = false;
    if ((s->urb != NULL) && (s->urb->hcpriv != NULL)) {
        /* kill invokes the completion callback with -USB_ERR_SHUTDOWN; the
         * callback consumes the packets that did move and sees running=false,
         * so it does not resubmit. */
        (void)usbh_kill_urb(s->urb);
    }
    if (s->data_sem != NULL) {
        usb_osal_sem_delete(s->data_sem);
        s->data_sem = NULL;
    }
    if (s->urb != NULL) {
        usb_osal_free(s->urb);
        s->urb = NULL;
    }
    if (s->packet_mem != NULL) {
        usb_osal_free(s->packet_mem);
        s->packet_mem = NULL;
    }
    if (s->ring != NULL) {
        usb_osal_free(s->ring);
        s->ring = NULL;
    }
    s->ring_size = 0;
    s->head = 0;
    s->tail = 0;
    s->have_last_start = false;
    s->boundaries = 0;
}

void usbh_uac_stream_abort(void)
{
    usbh_uac_stream_stop();
}

int usbh_uac_stream_read(void *buf, uint32_t len, uint32_t timeout_ms)
{
    struct uac_stream *s = &uac_stream;
    uint32_t n;

    if ((buf == NULL) || (len == 0U) || (s->ring == NULL)) {
        return 0;
    }
    n = ring_read(s, buf, len);
    if (n != 0U) {
        return (int)n;
    }
    if (timeout_ms == 0U) {
        return 0;
    }
    if (usb_osal_sem_take(s->data_sem, timeout_ms) < 0) {
        return 0;
    }
    return (int)ring_read(s, buf, len);
}

void usbh_uac_stream_stats(struct uac_stream_stats *stats)
{
    if (stats != NULL) {
        *stats = uac_stream.stats;
    }
}