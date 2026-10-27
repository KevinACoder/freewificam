/*
 * Copyright (c) 2026, zhugengyu
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-side UVC streaming: the other half of class/video/usbh_video.c.  The
 * class driver enumerates and opens the camera; this file owns the running
 * stream - isochronous urbs on the video-streaming endpoint, UVC payload
 * header parsing and frame assembly into an application-provided buffer pool.
 *
 * The interface mirrors the calls the upstream host-UVC demos make (they ship
 * without the engine; this fills it): usbh_video_stream_create() with the
 * frame pool, usbh_video_stream_start()/stop() around the session, and
 * dequeue/enqueue as the consumer side.  usbh_video_stream_attach() binds the
 * class instance the application's usbh_video_run() hook received.
 */
#ifndef USBH_UVC_STREAM_H
#define USBH_UVC_STREAM_H

#include "usbh_video.h"

/* Isochronous urbs in flight.  Each one spans CONFIG_UVC_STREAM_PACKETS
 * micro-frames - one USB frame at the endpoints' usual bInterval of one - and
 * the stream engine resubmits a completed urb behind the frames the others
 * reserved, so the ring tiles the frame list without gaps. */
#ifndef CONFIG_UVC_STREAM_URBS
#define CONFIG_UVC_STREAM_URBS 4
#endif

#ifndef CONFIG_UVC_STREAM_PACKETS
#define CONFIG_UVC_STREAM_PACKETS 8
#endif

/* The pump thread: payload parsing and resubmission.  It runs whenever a
 * stream is up and parks on its semaphore between completions. */
#ifndef CONFIG_UVC_STREAM_STACKSIZE
#define CONFIG_UVC_STREAM_STACKSIZE 4096
#endif
#ifndef CONFIG_UVC_STREAM_PRIO
#define CONFIG_UVC_STREAM_PRIO 1
#endif

/* Counters of one streaming session (ledgers and board anchors). */
struct uvc_stream_stats {
    uint32_t frames;      /* frames completed and handed to the application */
    uint32_t dropped;     /* frames dropped (error, overflow, no free buffer) */
    uint32_t invalid;     /* payload headers that failed the sanity checks */
    uint32_t err_packets; /* packets that carried a transport or payload error */
    uint32_t urbs;        /* completed isochronous urbs */
    uint32_t urb_errors;  /* urbs whose resubmission failed */
    uint64_t bytes;       /* payload bytes assembled into completed frames */
};

#ifdef __cplusplus
extern "C" {
#endif

/* Bind the video class instance captured by the application's
 * usbh_video_run() hook.  Returns 0, or -USB_ERR_NODEV before a camera is
 * connected. */
int usbh_video_stream_attach(struct usbh_video *video_class);

/* The frame pool: `count` frames whose frame_buf/frame_bufsize the caller
 * filled.  Call before start. */
void usbh_video_stream_create(struct usbh_videoframe *frame, uint32_t count);

/* Open the camera at the requested format and resolution - the alternate
 * setting is the smallest one whose endpoint carries the bandwidth the device
 * asks for - and run the stream.  Returns 0, or a negative value. */
int usbh_video_stream_start(uint16_t wWidth, uint16_t wHeight, uint8_t format_type);

/* Stop the stream, close the streaming interface and release the ring. */
void usbh_video_stream_stop(void);

/* Take one completed frame out of the ready queue (timeout in ms; 0 waits
 * forever).  Returns 0 and sets *frame, or -USB_ERR_TIMEOUT. */
int usbh_video_stream_dequeue(struct usbh_videoframe **frame, uint32_t timeout);

/* Give a consumed frame back to the pool. */
void usbh_video_stream_enqueue(struct usbh_videoframe *frame);

/* A copy of the session counters. */
void usbh_video_stream_stats(struct uvc_stream_stats *stats);

#ifdef __cplusplus
}
#endif

#endif /* USBH_UVC_STREAM_H */
