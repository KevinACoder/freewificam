/*
 * @file   uvc_stream.c
 * @brief  Case uvc_stream: the stream engine on the camera - frames, rate and
 *         bandwidth over a bounded window.
 *
 * The consumer shape is dequeue -> use -> enqueue, the same shape the upstream
 * host-UVC demos use; the engine is class/video/usbh_uvc_stream.c (imported by
 * patch 0004).  The case starts from the camera the open case already brought
 * up: the engine supports an already-open camera and then issues no
 * video-streaming class requests at all.  Taking the camera out of its
 * streaming setting is what can leave it refusing those requests for the rest
 * of the boot, so the camera is handed over open and the engine's stop leaves
 * it open (it was not opened by the engine).
 *
 * The window is bounded and the anchors carry everything a verdict needs -
 * frames, payload bytes, per-frame bound, rate, and the engine's own drop and
 * error counters.  The comparison against a Linux reference is the release
 * record's job, not this case's.
 *
 * Anchors (tools/rtos/judge.py matches these):
 *   uvc: stream first frame size=N
 *   uvc: stream frames=N bytes=N avg=N ms=N fps=N.NNN kbit=N dropped=N invalid=N err=N max=N
 *   uvc: stream PASS | uvc: stream FAIL <reason>
 */

#include <stdint.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "usbh_core.h"
#include "usbh_video.h"
#include "usbh_uvc_stream.h"

#include "uvc_app.h"

#define UVC_STREAM_WINDOW_MS   20000U /* the bounded measurement window */
#define UVC_STREAM_DEQUEUE_MS  500U
#define UVC_STREAM_POOL_FRAMES 3U
#define UVC_STREAM_FRAME_BYTES (48U * 1024U) /* 640x480 MJPEG fits well inside */

static uint8_t stream_bufs[UVC_STREAM_POOL_FRAMES][UVC_STREAM_FRAME_BYTES]
	__attribute__((aligned(64)));
static struct usbh_videoframe stream_pool[UVC_STREAM_POOL_FRAMES];

int case_uvc_stream_run(void)
{
	struct usbh_video *video_class;
	struct uvc_stream_stats stats;
	uint32_t start_ms, elapsed_ms;
	uint32_t frames = 0, timeouts = 0, bytes = 0, max_size = 0;
	uint32_t fps_milli, avg, kbit;
	unsigned i;

	if (uvc_app_class() == 0) {
		CASE_ANCHOR("uvc: stream FAIL camera not detected\n");
		return 1;
	}
	if (uvc_app_ensure_open() != 0) {
		CASE_ANCHOR("uvc: stream FAIL camera not open\n");
		return 1;
	}

	for (i = 0; i < UVC_STREAM_POOL_FRAMES; i++) {
		stream_pool[i].frame_buf = stream_bufs[i];
		stream_pool[i].frame_bufsize = UVC_STREAM_FRAME_BYTES;
		stream_pool[i].frame_size = 0;
	}
	usbh_video_stream_create(stream_pool, UVC_STREAM_POOL_FRAMES);

	if (usbh_video_stream_start(640, 480, USBH_VIDEO_FORMAT_MJPEG) != 0) {
		CASE_ANCHOR("uvc: stream FAIL usbh_video_stream_start\n");
		return 1;
	}

	video_class = uvc_app_class();
	CASE_ANCHOR("uvc: stream open mps=%u dmp=%u pool=%u\n",
		    (unsigned)video_class->isoin_mps,
		    (unsigned)video_class->probe.dwMaxPayloadTransferSize,
		    UVC_STREAM_POOL_FRAMES);

	start_ms = osKernelGetTickCount();
	while ((osKernelGetTickCount() - start_ms) < UVC_STREAM_WINDOW_MS) {
		struct usbh_videoframe *frame = 0;

		if (usbh_video_stream_dequeue(&frame, UVC_STREAM_DEQUEUE_MS) == 0) {
			if (frames == 0) {
				CASE_ANCHOR("uvc: stream first frame size=%u\n",
					    (unsigned)frame->frame_size);
			}
			frames++;
			bytes += frame->frame_size;
			if (frame->frame_size > max_size) {
				max_size = frame->frame_size;
			}
			usbh_video_stream_enqueue(frame);
		} else {
			timeouts++;
		}
	}
	elapsed_ms = osKernelGetTickCount() - start_ms;
	if (elapsed_ms == 0) {
		elapsed_ms = 1;
	}

	usbh_video_stream_stop();
	usbh_video_stream_stats(&stats);

	avg = (frames != 0) ? (bytes / frames) : 0;
	fps_milli = (uint32_t)(((uint64_t)frames * 1000000U) / elapsed_ms);
	kbit = (uint32_t)(((uint64_t)bytes * 8U) / elapsed_ms);

	CASE_ANCHOR("uvc: stream frames=%u bytes=%u avg=%u ms=%u fps=%u.%03u kbit=%u dropped=%u invalid=%u err=%u max=%u\n",
		    frames, bytes, avg, elapsed_ms,
		    fps_milli / 1000U, fps_milli % 1000U,
		    kbit, (unsigned)stats.dropped, (unsigned)stats.invalid,
		    (unsigned)stats.err_packets, max_size);

	if (frames == 0) {
		CASE_ANCHOR("uvc: stream FAIL no video frame in the window\n");
		return 1;
	}
	if (max_size > UVC_STREAM_FRAME_BYTES) {
		CASE_ANCHOR("uvc: stream FAIL a frame exceeded the pool buffer\n");
		return 1;
	}
	if (stats.err_packets != 0) {
		CASE_ANCHOR("uvc: stream FAIL transport errors during the window\n");
		return 1;
	}

	CASE_ANCHOR("uvc: stream PASS\n");
	return 0;
}
