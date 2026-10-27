/*
 * Copyright (c) 2026, zhugengyu
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-side UVC streaming engine: the running-stream half of the video class.
 * The class driver (usbh_video.c) enumerates the camera, negotiates the probe
 * and commit controls and selects an alternate setting; this file keeps a ring
 * of isochronous urbs on the video-streaming endpoint, parses the UVC payload
 * headers of every packet and assembles the payload bytes into the frames of
 * an application-provided pool.  The consumer takes finished frames with
 * usbh_video_stream_dequeue() and returns them with usbh_video_stream_enqueue().
 *
 * Design, in the shape the class's own open sequence has:
 *
 *  - the alternate setting is chosen per the device's answer: after the probe
 *    exchange the device reports the bandwidth the configuration needs (bytes
 *    per USB frame, dwMaxPayloadTransferSize); the smallest alternate setting
 *    whose endpoint carries at least that many bytes per frame (wMaxPacketSize
 *    times the transaction multiplier) is selected, "no fast enough setting"
 *    is an error;
 *  - one urb spans CONFIG_UVC_STREAM_PACKETS micro-frames and the ring is
 *    resubmitted as completions arrive, so the frame list stays covered; the
 *    submit path tiles the frames, which is what keeps the device's
 *    transactions from colliding between urbs;
 *  - the completion callback runs in interrupt context and does nothing but
 *    mark the slot and give a semaphore; a pump thread parses the packets in
 *    frame order (a completed urb's packets are consumed in the order the
 *    controller scheduled them), assembles frames in the caller's buffers and
 *    resubmits the urb;
 *  - frame boundaries come from the payload header: the frame-id bit flipping
 *    ends the previous frame and opens the next, the end-of-frame bit ends the
 *    current one, and the error bit - like a transport error - drops the
 *    frame being assembled.  A packet after a completed frame always opens a
 *    new one, which is the channel that keeps cameras which never flip the
 *    frame-id (a known family) working; cameras which never set end-of-frame
 *    are covered by the flip itself - either channel alone suffices;
 *  - a frame that overflows its buffer, arrives with the error bit, or finds
 *    no free buffer is dropped as a whole; the counters keep every class of
 *    drop visible.
 *
 * The engine is one instance: the video class dimensioning it accompanies
 * (CONFIG_USBHOST_MAX_VIDEO_CLASS) is the camera count of the image's device
 * set, and the attach call rebinds the single instance to whichever class
 * instance the application's run hook reports.
 */

#include "usbh_core.h"
#include "usbh_video.h"
#include "usbh_uvc_stream.h"

#undef USB_DBG_TAG
#define USB_DBG_TAG "uvc_stream"
#include "usb_log.h"

/* The class's probe-control helpers are defined in usbh_video.c but not
 * declared in its header at this pin; the engine uses them for the pre-open
 * bandwidth question (bytes per USB frame the configuration needs). */
int usbh_videostreaming_get_cur_probe(struct usbh_video *video_class);
int usbh_videostreaming_set_cur_probe(struct usbh_video *video_class,
                                      uint8_t formatindex, uint8_t frameindex,
                                      uint32_t dwFrameInterval);

/* UVC payload header bits (bmHeaderInfo), UVC 1.0/1.1 section 2.4.3.4. */
#define UVC_STREAM_FID 0x01
#define UVC_STREAM_EOF 0x02
#define UVC_STREAM_ERR 0x40

/* The frame number in the urb contract is the 10-bit periodic frame counter. */
#define UVC_STREAM_FRAME_MASK 0x3ff

struct uvc_urb_slot {
    struct usbh_urb *urb;
    uint8_t *packet_mem; /* allocation base the packet pointers are carved from */
    volatile bool done;
};

enum uvc_stream_state {
    UVC_ST_IDLE,      /* no frame being assembled */
    UVC_ST_ASSEMBLING,
    UVC_ST_DROPPING,  /* a frame was dropped; payload is discarded until its end */
};

struct uvc_stream {
    struct usbh_video *video;
    volatile bool running;

    struct usbh_videoframe *pool;
    uint32_t pool_count;
    struct usbh_videoframe **free_ring;
    struct usbh_videoframe **ready_ring;
    uint32_t free_head, free_tail, free_count;
    uint32_t ready_head, ready_tail, ready_count;
    usb_osal_mutex_t lock;
    usb_osal_sem_t frame_ready;

    /* Whether the engine's own start path ran the class open: the camera is
     * then the engine's to hand back on stop.  A camera the caller opened is
     * left open (and in its streaming setting) on stop. */
    bool opened_by_engine;

    struct uvc_urb_slot slots[CONFIG_UVC_STREAM_URBS];
    usb_osal_sem_t urb_done;
    usb_osal_sem_t pump_exited;
    usb_osal_thread_t pump;
    uint16_t last_start;

    struct usbh_videoframe *cur;
    uint32_t cur_offset;
    enum uvc_stream_state state;
    int last_fid;

    struct uvc_stream_stats stats;
};

static struct uvc_stream uvc_stream;

/* The pool lock and the frame-ready semaphore outlive any single session
 * (frames can still be drained after a stop); they are created on first use. */
static void stream_ensure(struct uvc_stream *s)
{
    if (s->lock == NULL) {
        s->lock = usb_osal_mutex_create();
    }
    if (s->frame_ready == NULL) {
        s->frame_ready = usb_osal_sem_create_counting(CONFIG_UVC_STREAM_URBS * CONFIG_UVC_STREAM_PACKETS);
    }
}

/* ------------------------------------------------------------------------
 * Frame pool: index rings over the caller's frame array (mutex-guarded)
 * --------------------------------------------------------------------- */

static void stream_free_push(struct uvc_stream *s, struct usbh_videoframe *frame)
{
    s->free_ring[s->free_tail] = frame;
    s->free_tail = (s->free_tail + 1) % s->pool_count;
    s->free_count++;
}

static struct usbh_videoframe *stream_free_pop(struct uvc_stream *s)
{
    struct usbh_videoframe *frame;

    if (s->free_count == 0) {
        return NULL;
    }
    frame = s->free_ring[s->free_head];
    s->free_head = (s->free_head + 1) % s->pool_count;
    s->free_count--;
    return frame;
}

static void stream_ready_push(struct uvc_stream *s, struct usbh_videoframe *frame)
{
    s->ready_ring[s->ready_tail] = frame;
    s->ready_tail = (s->ready_tail + 1) % s->pool_count;
    s->ready_count++;
}

static struct usbh_videoframe *stream_ready_pop(struct uvc_stream *s)
{
    struct usbh_videoframe *frame;

    if (s->ready_count == 0) {
        return NULL;
    }
    frame = s->ready_ring[s->ready_head];
    s->ready_head = (s->ready_head + 1) % s->pool_count;
    s->ready_count--;
    return frame;
}

/* ------------------------------------------------------------------------
 * Assembly: payload header state machine (consumer side of one session)
 * --------------------------------------------------------------------- */

static bool stream_frame_open(struct uvc_stream *s)
{
    struct usbh_videoframe *frame;

    usb_osal_mutex_take(s->lock);
    frame = stream_free_pop(s);
    usb_osal_mutex_give(s->lock);

    s->cur = frame;
    s->cur_offset = 0;
    return frame != NULL; /* a dry pool drops the frame instead of stalling */
}

static bool stream_frame_append(struct uvc_stream *s, const uint8_t *data, uint32_t len)
{
    if (len > (s->cur->frame_bufsize - s->cur_offset)) {
        return false; /* overflow: the frame is not representable, drop it */
    }
    memcpy(s->cur->frame_buf + s->cur_offset, data, len);
    s->cur_offset += len;
    return true;
}

static void stream_frame_drop(struct uvc_stream *s)
{
    if (s->cur != NULL) {
        usb_osal_mutex_take(s->lock);
        stream_free_push(s, s->cur);
        usb_osal_mutex_give(s->lock);
        s->cur = NULL;
    }
    s->cur_offset = 0;
    s->stats.dropped++;
}

static void stream_frame_complete(struct uvc_stream *s)
{
    if ((s->cur == NULL) || (s->cur_offset == 0)) {
        stream_frame_drop(s);
        return;
    }
    s->cur->frame_size = s->cur_offset;
    s->cur->frame_format = s->video ? s->video->current_format : USBH_VIDEO_FORMAT_MJPEG;

    usb_osal_mutex_take(s->lock);
    stream_ready_push(s, s->cur);
    usb_osal_mutex_give(s->lock);

    s->stats.frames++;
    s->stats.bytes += s->cur_offset;
    usb_osal_sem_give(s->frame_ready);
    s->cur = NULL;
    s->cur_offset = 0;
}

static void stream_packet(struct uvc_stream *s, const uint8_t *data, uint32_t len)
{
    uint8_t hdr_len, info, fid;
    bool eof, err;
    uint32_t payload;

    if (len == 0) {
        return; /* an empty micro-frame carries no header and no data */
    }
    if ((len < 2) || (data[0] < 2) || (data[0] > len)) {
        s->stats.invalid++;
        return;
    }
    hdr_len = data[0];
    info = data[1];
    fid = info & UVC_STREAM_FID;
    eof = (info & UVC_STREAM_EOF) != 0;
    err = (info & UVC_STREAM_ERR) != 0;
    payload = len - hdr_len;

    if (err) {
        /* the payload of this frame is known bad; drop it whole */
        s->stats.err_packets++;
        if (s->state == UVC_ST_ASSEMBLING) {
            stream_frame_drop(s);
        }
        s->state = UVC_ST_DROPPING;
        s->last_fid = fid;
        return;
    }

    if ((s->last_fid >= 0) && ((int)fid != s->last_fid)) {
        /* the device flipped the frame id: the previous frame ends here and
         * this packet opens the next one */
        if (s->state == UVC_ST_ASSEMBLING) {
            stream_frame_complete(s);
        }
        s->state = UVC_ST_IDLE;
    }

    if ((s->state == UVC_ST_IDLE) && (payload > 0)) {
        if (stream_frame_open(s)) {
            s->state = UVC_ST_ASSEMBLING;
        } else {
            /* a dry pool drops the frame instead of stalling; the drop is
             * counted like the other transient failures */
            s->state = UVC_ST_DROPPING;
            s->stats.dropped++;
        }
    }

    if (s->state == UVC_ST_ASSEMBLING) {
        if (!stream_frame_append(s, data + hdr_len, payload)) {
            stream_frame_drop(s);
            s->state = UVC_ST_DROPPING;
            if (eof) {
                s->state = UVC_ST_IDLE;
            }
        } else if (eof) {
            stream_frame_complete(s);
            s->state = UVC_ST_IDLE;
        }
    } else if (s->state == UVC_ST_DROPPING) {
        if (eof) {
            s->state = UVC_ST_IDLE; /* the drop ends with the frame */
        }
    }
    s->last_fid = fid;
}

static void stream_urb_consume(struct uvc_stream *s, struct uvc_urb_slot *slot)
{
    struct usbh_urb *urb = slot->urb;

    for (uint32_t i = 0; i < urb->num_of_iso_packets; i++) {
        struct usbh_iso_frame_packet *packet = &urb->iso_packet[i];

        if (packet->errorcode < 0) {
            s->stats.err_packets++;
            if (s->state == UVC_ST_ASSEMBLING) {
                stream_frame_drop(s);
            }
            s->state = UVC_ST_DROPPING;
            continue;
        }
        stream_packet(s, packet->transfer_buffer, packet->actual_length);
    }
    s->stats.urbs++;
}

/* ------------------------------------------------------------------------
 * The pump: consume completed urbs in frame order, resubmit them
 * --------------------------------------------------------------------- */

static void uvc_stream_urb_complete(void *arg, int nbytes)
{
    struct uvc_urb_slot *slot = arg;

    (void)nbytes;
    slot->done = true;
    usb_osal_sem_give(uvc_stream.urb_done);
}

static void uvc_stream_pump(void *argument)
{
    struct uvc_stream *s = argument;

    while (s->running) {
        (void)usb_osal_sem_take(s->urb_done, USB_OSAL_WAITING_FOREVER);
        if (!s->running) {
            break;
        }
        for (;;) {
            struct uvc_urb_slot *pick = NULL;
            uint16_t best = 0;

            for (uint32_t i = 0; i < CONFIG_UVC_STREAM_URBS; i++) {
                uint16_t ahead;

                if (!s->slots[i].done) {
                    continue;
                }
                /* frame distance from the last consumed urb: the smallest one
                 * is the next in sequence */
                ahead = (uint16_t)((s->slots[i].urb->start_frame - s->last_start) & UVC_STREAM_FRAME_MASK);
                if ((pick == NULL) || (ahead < best)) {
                    pick = &s->slots[i];
                    best = ahead;
                }
            }
            if (pick == NULL) {
                break;
            }
            pick->done = false;
            stream_urb_consume(s, pick);
            s->last_start = pick->urb->start_frame;

            for (uint32_t retry = 0; retry < 3; retry++) {
                struct usbh_urb *urb = pick->urb;

                urb->errorcode = 0;
                urb->timeout = 0;
                urb->actual_length = 0;
                for (uint32_t p = 0; p < urb->num_of_iso_packets; p++) {
                    urb->iso_packet[p].actual_length = 0;
                    urb->iso_packet[p].errorcode = 0;
                }
                if (usbh_submit_urb(urb) == 0) {
                    break;
                }
                s->stats.urb_errors++;
                if (retry == 2) {
                    /* the ring is one urb short from here on; the stream stays
                     * up with what remains and the counter says so */
                    break;
                }
                usb_osal_msleep(6);
            }
        }
    }
    usb_osal_sem_give(s->pump_exited);
    usb_osal_thread_delete(NULL);
}

/* ------------------------------------------------------------------------
 * Open policy: format lookup and alternate-setting selection
 * --------------------------------------------------------------------- */

static int stream_lookup(struct usbh_video *video, uint8_t format_type,
                         uint16_t wWidth, uint16_t wHeight,
                         uint8_t *formatidx, uint8_t *frameidx, uint32_t *interval)
{
    for (uint8_t i = 0; i < video->num_of_formats; i++) {
        if (video->format[i].format_type != format_type) {
            continue;
        }
        for (uint8_t j = 0; j < video->format[i].num_of_frames; j++) {
            if ((video->format[i].frame[j].wWidth == wWidth) &&
                (video->format[i].frame[j].wHeight == wHeight)) {
                *formatidx = i + 1;
                *frameidx = j + 1;
                *interval = video->format[i].frame[j].dwDefaultFrameInterval;
                return 0;
            }
        }
    }
    return -USB_ERR_NODEV;
}

/* The image-side endpoint payload of one alternate setting: wMaxPacketSize
 * times the high-bandwidth transaction multiplier, the same quantity the
 * class's open sequence puts into isoin_mps. */
static uint16_t stream_alt_psize(const struct usb_endpoint_descriptor *ep)
{
    uint16_t mps = ep->wMaxPacketSize & USB_MAXPACKETSIZE_MASK;
    uint8_t mult = (ep->wMaxPacketSize & USB_MAXPACKETSIZE_ADDITIONAL_TRANSCATION_MASK) >>
                   USB_MAXPACKETSIZE_ADDITIONAL_TRANSCATION_SHIFT;

    return (uint16_t)(mps * (mult + 1));
}

/* The smallest alternate setting whose endpoint carries at least `want` bytes
 * per frame.  Returns 0 when no setting is fast enough. */
static uint8_t stream_alt_pick(struct usbh_video *video, uint32_t want)
{
    const struct usbh_interface *intf = &video->hport->config.intf[video->data_intf];
    uint16_t best_psize = 0xffff;
    uint8_t best = 0;

    for (uint8_t alt = 1; alt < intf->altsetting_num; alt++) {
        const struct usb_endpoint_descriptor *ep = &intf->altsetting[alt].ep[0].ep_desc;

        if (ep->bLength == 0) {
            continue; /* no endpoint in this setting */
        }
        if (USB_GET_ENDPOINT_TYPE(ep->bmAttributes) != USB_ENDPOINT_TYPE_ISOCHRONOUS) {
            continue;
        }
        if ((ep->bEndpointAddress & 0x80) == 0) {
            continue; /* the streaming endpoint is an IN endpoint */
        }
        uint16_t psize = stream_alt_psize(ep);

        if ((psize >= want) && (psize < best_psize)) {
            best = alt;
            best_psize = psize;
        }
    }
    return best;
}

/* ------------------------------------------------------------------------
 * Public interface
 * --------------------------------------------------------------------- */

int usbh_video_stream_attach(struct usbh_video *video_class)
{
    if ((video_class == NULL) || (video_class->hport == NULL)) {
        return -USB_ERR_NODEV;
    }
    uvc_stream.video = video_class;
    return 0;
}

void usbh_video_stream_create(struct usbh_videoframe *frame, uint32_t count)
{
    struct uvc_stream *s = &uvc_stream;

    if ((frame == NULL) || (count == 0) || (s->running)) {
        return;
    }
    stream_ensure(s);
    if (s->free_ring != NULL) {
        usb_osal_free(s->free_ring);
        usb_osal_free(s->ready_ring);
        s->free_ring = NULL;
        s->ready_ring = NULL;
    }

    s->pool = frame;
    s->pool_count = count;
    s->free_ring = usb_osal_malloc(count * sizeof(struct usbh_videoframe *));
    s->ready_ring = usb_osal_malloc(count * sizeof(struct usbh_videoframe *));
    if ((s->free_ring == NULL) || (s->ready_ring == NULL)) {
        s->pool = NULL;
        s->pool_count = 0;
        return;
    }

    usb_osal_mutex_take(s->lock);
    s->free_head = s->free_tail = s->free_count = 0;
    s->ready_head = s->ready_tail = s->ready_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        frame[i].frame_size = 0;
        stream_free_push(s, &frame[i]);
    }
    usb_osal_mutex_give(s->lock);
}

int usbh_video_stream_start(uint16_t wWidth, uint16_t wHeight, uint8_t format_type)
{
    struct uvc_stream *s = &uvc_stream;
    struct usbh_video *video = s->video;
    uint32_t pkt_size, urb_size, want;
    uint32_t submitted = 0;
    int ret;

    if (video == NULL) {
        return -USB_ERR_NODEV;
    }
    if (s->running) {
        return 0;
    }
    if ((s->pool == NULL) || (s->free_ring == NULL)) {
        return -USB_ERR_INVAL;
    }
    stream_ensure(s);

    if (!video->is_opened) {
        uint8_t formatidx = 0, frameidx = 0;
        uint32_t interval = 0;
        uint8_t alt;

        /* A device may defer its video-streaming requests for a moment after
         * the interface leaves a streaming setting; the class's own close only
         * stops the interface, so give the device the settle the application's
         * gap would otherwise provide (a deferred request that is aborted can
         * wedge such a device's control engine, which is why the retries below
         * exist only as defence and the first attempt is patient). */
        usb_osal_msleep(200);

        ret = stream_lookup(video, format_type, wWidth, wHeight, &formatidx, &frameidx, &interval);
        if (ret < 0) {
            USB_LOG_ERR("start: %ux%u format %u not in the device's tables (%d)\r\n",
                        wWidth, wHeight, format_type, (int)ret);
            return ret;
        }
        /* Ask the device what the configuration needs (bytes per USB frame)
         * before committing, exactly as the class's open sequence does.  A
         * camera that just left a streaming configuration may defer or refuse
         * these requests for a moment; the retries are defence, bounded so a
         * camera that is simply gone still fails out.  (A device that has
         * been made to refuse by an earlier leave-notice may not recover
         * here at all; the integration policy that avoids provoking it is to
         * keep the camera in its streaming setting between streams, which the
         * class's open already establishes.) */
        for (uint32_t attempt = 0; attempt < 5; attempt++) {
            ret = usbh_videostreaming_set_cur_probe(video, formatidx, frameidx, interval);
            if (ret == 0) {
                break;
            }
            USB_LOG_ERR("start: probe attempt %u refused (%d), retrying\r\n",
                        (unsigned)(attempt + 1), (int)ret);
            usb_osal_msleep(100);
        }
        if (ret < 0) {
            USB_LOG_ERR("start: probe set failed (%d)\r\n", (int)ret);
            return ret;
        }
        for (uint32_t retry = 0; retry < 5; retry++) {
            ret = usbh_videostreaming_get_cur_probe(video);
            if (ret == 0) {
                break;
            }
            usb_osal_msleep(100);
        }
        if (ret < 0) {
            USB_LOG_ERR("start: probe get failed (%d)\r\n", (int)ret);
            return ret;
        }
        want = video->probe.dwMaxPayloadTransferSize;
        if (want == 0) {
            want = 1; /* a null bandwidth request takes the lowest setting */
        }
        alt = stream_alt_pick(video, want);
        if (alt == 0) {
            USB_LOG_ERR("start: no setting carries the requested %u bytes per frame\r\n",
                        (unsigned)want);
            return -USB_ERR_NOTSUPP;
        }
        ret = usbh_video_open(video, format_type, wWidth, wHeight, alt);
        if (ret < 0) {
            USB_LOG_ERR("start: open at setting %u failed (%d)\r\n", alt, (int)ret);
            return ret;
        }
        s->opened_by_engine = true;
    }

    if ((video->isoin == NULL) || (video->isoin_mps == 0)) {
        USB_LOG_ERR("start: no isochronous endpoint after open\r\n");
        return -USB_ERR_NODEV;
    }

    pkt_size = (uint32_t)(video->isoin_mps + CONFIG_USB_ALIGN_SIZE - 1) &
               (uint32_t)~(CONFIG_USB_ALIGN_SIZE - 1);
    urb_size = sizeof(struct usbh_urb) +
               (CONFIG_UVC_STREAM_PACKETS * sizeof(struct usbh_iso_frame_packet));

    s->urb_done = usb_osal_sem_create_counting(CONFIG_UVC_STREAM_URBS);
    s->pump_exited = usb_osal_sem_create(0);
    if ((s->urb_done == NULL) || (s->pump_exited == NULL)) {
        USB_LOG_ERR("start: semaphore creation failed\r\n");
        ret = -USB_ERR_NOMEM;
        goto fail;
    }

    for (uint32_t i = 0; i < CONFIG_UVC_STREAM_URBS; i++) {
        struct uvc_urb_slot *slot = &s->slots[i];
        struct usbh_urb *urb;
        uint8_t *base;

        slot->urb = usb_osal_malloc(urb_size);
        slot->packet_mem = usb_osal_malloc(pkt_size * CONFIG_UVC_STREAM_PACKETS + CONFIG_USB_ALIGN_SIZE);
        if ((slot->urb == NULL) || (slot->packet_mem == NULL)) {
            USB_LOG_ERR("start: ring allocation failed at urb %u\r\n", (unsigned)i);
            ret = -USB_ERR_NOMEM;
            goto fail;
        }
        base = (uint8_t *)(((uintptr_t)slot->packet_mem + CONFIG_USB_ALIGN_SIZE - 1) &
                           ~(uintptr_t)(CONFIG_USB_ALIGN_SIZE - 1));

        memset(slot->urb, 0, urb_size);
        urb = slot->urb;
        urb->hport = video->hport;
        urb->ep = video->isoin;
        urb->num_of_iso_packets = CONFIG_UVC_STREAM_PACKETS;
        urb->complete = uvc_stream_urb_complete;
        urb->arg = slot;
        for (uint32_t p = 0; p < CONFIG_UVC_STREAM_PACKETS; p++) {
            urb->iso_packet[p].transfer_buffer = base + (p * pkt_size);
            urb->iso_packet[p].transfer_buffer_length = video->isoin_mps;
        }
        slot->done = false;
    }

    s->state = UVC_ST_IDLE;
    s->last_fid = -1;
    s->last_start = 0;
    s->running = true;
    s->pump = usb_osal_thread_create("uvc_stream", CONFIG_UVC_STREAM_STACKSIZE,
                                     CONFIG_UVC_STREAM_PRIO, uvc_stream_pump, s);
    if (s->pump == NULL) {
        USB_LOG_ERR("start: pump thread creation failed\r\n");
        s->running = false;
        ret = -USB_ERR_NOMEM;
        goto fail;
    }

    for (uint32_t i = 0; i < CONFIG_UVC_STREAM_URBS; i++) {
        for (uint32_t retry = 0; retry < 3; retry++) {
            if (usbh_submit_urb(s->slots[i].urb) == 0) {
                submitted++;
                break;
            }
            s->stats.urb_errors++;
            usb_osal_msleep(6);
        }
    }
    if (submitted == 0) {
        USB_LOG_ERR("start: no urb was accepted\r\n");
        ret = -USB_ERR_IO;
        goto fail;
    }
    return 0;

fail:
    s->running = false;
    if (s->pump != NULL) {
        usb_osal_sem_give(s->urb_done);
        (void)usb_osal_sem_take(s->pump_exited, 1000);
        s->pump = NULL;
    }
    for (uint32_t i = 0; i < CONFIG_UVC_STREAM_URBS; i++) {
        if (s->slots[i].urb != NULL) {
            if (s->slots[i].urb->hcpriv != NULL) {
                (void)usbh_kill_urb(s->slots[i].urb);
            }
            usb_osal_free(s->slots[i].urb);
            s->slots[i].urb = NULL;
        }
        if (s->slots[i].packet_mem != NULL) {
            usb_osal_free(s->slots[i].packet_mem);
            s->slots[i].packet_mem = NULL;
        }
    }
    if (s->urb_done != NULL) {
        usb_osal_sem_delete(s->urb_done);
        s->urb_done = NULL;
    }
    if (s->pump_exited != NULL) {
        usb_osal_sem_delete(s->pump_exited);
        s->pump_exited = NULL;
    }
    if (video->is_opened) {
        (void)usbh_video_close(video);
    }
    s->opened_by_engine = false;
    return ret;
}

void usbh_video_stream_stop(void)
{
    struct uvc_stream *s = &uvc_stream;

    if (!s->running) {
        return;
    }

    s->running = false;
    usb_osal_sem_give(s->urb_done); /* wake a parked pump so it can exit */
    if (s->pump != NULL) {
        (void)usb_osal_sem_take(s->pump_exited, 1000);
        s->pump = NULL;
    }

    for (uint32_t i = 0; i < CONFIG_UVC_STREAM_URBS; i++) {
        struct uvc_urb_slot *slot = &s->slots[i];

        if ((slot->urb != NULL) && (slot->urb->hcpriv != NULL)) {
            (void)usbh_kill_urb(slot->urb);
        }
        if (slot->urb != NULL) {
            usb_osal_free(slot->urb);
            slot->urb = NULL;
        }
        if (slot->packet_mem != NULL) {
            usb_osal_free(slot->packet_mem);
            slot->packet_mem = NULL;
        }
        slot->done = false;
    }

    if (s->urb_done != NULL) {
        usb_osal_sem_delete(s->urb_done);
        s->urb_done = NULL;
    }
    if (s->pump_exited != NULL) {
        usb_osal_sem_delete(s->pump_exited);
        s->pump_exited = NULL;
    }

    if ((s->video != NULL) && s->video->is_opened) {
        if (s->opened_by_engine) {
            (void)usbh_video_close(s->video);
        } else {
            /* The caller opened the camera; leave it open and in its streaming
             * setting, so a later start needs no class requests at all.  The
             * board camera is also the reason: it can stop answering VS
             * settings after it has been made to leave a streaming setting,
             * and there is nothing to regain by taking it out here. */
            USB_LOG_INFO("stop: camera left open (caller-owned)\r\n");
        }
        s->opened_by_engine = false;
    }

    /* frames still in the ready queue stay there; the application's dequeue
     * drains them, and the counters keep the session visible */
    stream_frame_drop(s);
    s->state = UVC_ST_IDLE;
    s->last_fid = -1;
}

int usbh_video_stream_dequeue(struct usbh_videoframe **frame, uint32_t timeout)
{
    struct uvc_stream *s = &uvc_stream;
    struct usbh_videoframe *ready;

    if (frame == NULL) {
        return -USB_ERR_INVAL;
    }
    stream_ensure(s);
    for (;;) {
        usb_osal_mutex_take(s->lock);
        ready = stream_ready_pop(s);
        usb_osal_mutex_give(s->lock);
        if (ready != NULL) {
            *frame = ready;
            return 0;
        }
        if (usb_osal_sem_take(s->frame_ready, timeout) < 0) {
            return -USB_ERR_TIMEOUT;
        }
    }
}

void usbh_video_stream_enqueue(struct usbh_videoframe *frame)
{
    struct uvc_stream *s = &uvc_stream;

    if (frame == NULL) {
        return;
    }
    stream_ensure(s);
    usb_osal_mutex_take(s->lock);
    stream_free_push(s, frame);
    usb_osal_mutex_give(s->lock);
}

void usbh_video_stream_stats(struct uvc_stream_stats *stats)
{
    struct uvc_stream *s = &uvc_stream;

    if (stats == NULL) {
        return;
    }
    stream_ensure(s);
    usb_osal_mutex_take(s->lock);
    *stats = s->stats;
    usb_osal_mutex_give(s->lock);
}

