/*
 * Copyright (c) 2026, zhugengyu
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-side USB Audio Class 1.0 microphone: the running-stream half.  It
 * keeps one isochronous urb on the AudioStreaming IN endpoint, parses each
 * packet as raw PCM (no payload header in UAC1) into a single-producer /
 * single-consumer byte ring, and resubmits the urb from a pump thread.
 *
 * The urb geometry is the DWC2 isochronous engine's contract: one channel per
 * endpoint, one packet per 1 ms frame, the urb's packet array is the batch.
 * CONFIG_UAC_STREAM_PACKETS therefore trades resubmit frequency against
 * latency; 16 packets is 16 ms, which keeps a missed boundary short.  The
 * resubmit runs in the completion callback (interrupt context), so there is no
 * pump task to be scheduled late.
 */
#ifndef USBH_UAC_STREAM_H
#define USBH_UAC_STREAM_H

#include "usbh_uac.h"

/* One urb of 16 one-millisecond packets.  The engine completes the urb at the
 * last packet; the pump then consumes the 16 lengths and resubmits. */
#ifndef CONFIG_UAC_STREAM_PACKETS
#define CONFIG_UAC_STREAM_PACKETS 16
#endif

/* Ring depth in milliseconds of audio.  200 ms absorbs the consumer's 20 ms
 * read cadence with a wide margin; the ring is allocated from the FreeRTOS
 * heap (SDRAM on the profiles that stream). */
#ifndef CONFIG_UAC_RING_MS
#define CONFIG_UAC_RING_MS 200
#endif

struct uac_stream_stats {
    uint32_t packets;  /* 1 ms packets consumed */
    uint32_t urbs;     /* urbs completed */
    uint32_t bytes;    /* PCM bytes pushed into the ring */
    uint32_t gaps;     /* 1 ms frames the host did not schedule between urbs */
    uint32_t err;      /* packet transport errors + refused resubmissions */
    uint32_t empty;    /* zero-length packets */
    uint32_t overrun;  /* bytes dropped because the ring was full */
    uint32_t shutdown;     /* packets the stop path marked as not transferred */
    uint32_t gap_first;    /* the first inter-urb boundary's gap, if any */
    uint32_t err_run;      /* current run of consecutive packet errors */
    uint32_t err_max_run;  /* longest such run */
    uint32_t err_first;    /* errors on a urb's first packet */
    uint32_t err_last;     /* errors on a urb's last packet */
    uint32_t err_mid;      /* errors on a packet between them */
    uint32_t gap_max;      /* largest single boundary gap, in frames */
    uint32_t rate;     /* snapshot of the class rate */
    uint8_t channels;
    uint8_t subframe;
    uint16_t mps;
};

#ifdef __cplusplus
extern "C" {
#endif

/* Bind the class instance published by the application's usbh_uac_run hook. */
int usbh_uac_stream_attach(struct usbh_uac *uac);

/* Open (through the class) and start capture at the class's selected rate. */
int usbh_uac_stream_start(void);

/* Stop capture: kill the urb, park the pump, free the ring.  The streaming
 * interface is deliberately left at its alternate setting (see usbh_uac.h). */
void usbh_uac_stream_stop(void);

/* Disconnect path: stop capture, callable when the device is already gone. */
void usbh_uac_stream_abort(void);

/* Copy up to `len` PCM bytes; 0 when the ring has none (after waiting up to
 * `timeout_ms` for the first byte).  Returns the byte count. */
int usbh_uac_stream_read(void *buf, uint32_t len, uint32_t timeout_ms);

/* A copy of the session counters. */
void usbh_uac_stream_stats(struct uac_stream_stats *stats);

#ifdef __cplusplus
}
#endif

#endif /* USBH_UAC_STREAM_H */