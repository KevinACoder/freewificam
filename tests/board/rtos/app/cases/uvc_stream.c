/*
 * @file   uvc_stream.c
 * @brief  Case uvc_stream: run the UVC stream engine over a 20 s window and
 *         report frames, rate, bandwidth, drops and transport errors.
 *
 * The consumer shape is dequeue -> inspect -> enqueue.  The frame pool lives
 * in the FMC SDRAM (the 1 MiB flash / 192 KiB SRAM part cannot hold three
 * 720-line MJPEG buffers); the stream engine's packet ring is allocated from
 * the FreeRTOS heap, which the profiles also place in SDRAM.  The camera was
 * opened by uvc_open and is left open: a close/stop can leave this camera
 * family refusing the video-streaming requests for the rest of the boot.
 *
 * Anchors:
 *   uvc: stream open mps=N dmp=N pool=N
 *   uvc: stream first frame size=N
 *   uvc: stream frames=N bytes=N avg=N ms=N fps=N.NNN kbit=N dropped=N invalid=N err=N max=N
 *   uvc: stream jpeg soi=N eoi=N
 *   uvc: 720p verdict=yes|720line|no ...
 *   uvc: stream PASS | uvc: stream FAIL <reason>
 */

#include <stdint.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "usbh_core.h"
#include "usbh_video.h"
#include "usbh_uvc_stream.h"

#include "uvc_app.h"

#define UVC_STREAM_WINDOW_MS   20000U
#define UVC_STREAM_DEQUEUE_MS  500U
#define UVC_STREAM_POOL_FRAMES 3U

#ifndef CONFIG_UVC_FRAME_BYTES
#define CONFIG_UVC_FRAME_BYTES (512U * 1024U)
#endif

static uint8_t stream_bufs[UVC_STREAM_POOL_FRAMES][CONFIG_UVC_FRAME_BYTES]
	__attribute__((section(".sdram"), aligned(64)));
static struct usbh_videoframe stream_pool[UVC_STREAM_POOL_FRAMES];

static int frame_is_jpeg_soi(const struct usbh_videoframe *frame)
{
	return (frame->frame_size >= 2U) &&
	       (frame->frame_buf[0] == 0xffU) &&
	       (frame->frame_buf[1] == 0xd8U);
}

static int frame_is_jpeg_eoi(const struct usbh_videoframe *frame)
{
	return (frame->frame_size >= 2U) &&
	       (frame->frame_buf[frame->frame_size - 2U] == 0xffU) &&
	       (frame->frame_buf[frame->frame_size - 1U] == 0xd9U);
}

int case_uvc_stream_run(void)
{
	struct usbh_video *video_class;
	struct uvc_stream_stats stats;
	uint32_t start_ms, elapsed_ms;
	uint32_t frames = 0, timeouts = 0, bytes = 0, max_size = 0;
	uint32_t jpeg_soi = 0, jpeg_eoi = 0;
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
		stream_pool[i].frame_bufsize = CONFIG_UVC_FRAME_BYTES;
		stream_pool[i].frame_size = 0;
	}
	usbh_video_stream_create(stream_pool, UVC_STREAM_POOL_FRAMES);

	if (usbh_video_stream_start(CONFIG_UVC_TARGET_WIDTH,
				    CONFIG_UVC_TARGET_HEIGHT,
				    USBH_VIDEO_FORMAT_MJPEG) != 0) {
		CASE_ANCHOR("uvc: stream FAIL usbh_video_stream_start\n");
		return 1;
	}

	video_class = uvc_app_class();
	CASE_ANCHOR("uvc: stream open mps=%u dmp=%u pool=%u\n",
		    (unsigned)video_class->isoin_mps,
		    (unsigned)video_class->probe.dwMaxPayloadTransferSize,
		    (unsigned)UVC_STREAM_POOL_FRAMES);

	start_ms = osKernelGetTickCount();
	while ((osKernelGetTickCount() - start_ms) < UVC_STREAM_WINDOW_MS) {
		struct usbh_videoframe *frame = 0;

		if (usbh_video_stream_dequeue(&frame, UVC_STREAM_DEQUEUE_MS) == 0) {
			if (frames == 0U) {
				CASE_ANCHOR("uvc: stream first frame size=%u\n",
					    (unsigned)frame->frame_size);
			}
			frames++;
			bytes += frame->frame_size;
			if (frame->frame_size > max_size) {
				max_size = frame->frame_size;
			}
			if (frame_is_jpeg_soi(frame)) {
				jpeg_soi++;
			}
			if (frame_is_jpeg_eoi(frame)) {
				jpeg_eoi++;
			}
			usbh_video_stream_enqueue(frame);
		} else {
			timeouts++;
		}
	}
	elapsed_ms = osKernelGetTickCount() - start_ms;
	if (elapsed_ms == 0U) {
		elapsed_ms = 1U;
	}

	usbh_video_stream_stop();
	usbh_video_stream_stats(&stats);

	avg = (frames != 0U) ? (bytes / frames) : 0U;
	fps_milli = (uint32_t)(((uint64_t)frames * 1000000U) / elapsed_ms);
	kbit = (uint32_t)(((uint64_t)bytes * 8U) / elapsed_ms);

	CASE_ANCHOR("uvc: stream frames=%u bytes=%u avg=%u ms=%u fps=%u.%03u kbit=%u dropped=%u invalid=%u err=%u max=%u\n",
		    frames, bytes, avg, elapsed_ms,
		    fps_milli / 1000U, fps_milli % 1000U,
		    kbit, (unsigned)stats.dropped, (unsigned)stats.invalid,
		    (unsigned)stats.err_packets, max_size);
	CASE_ANCHOR("uvc: stream jpeg soi=%u eoi=%u timeouts=%u\n",
		    jpeg_soi, jpeg_eoi, timeouts);

#if (CONFIG_UVC_TARGET_WIDTH == 1280) && (CONFIG_UVC_TARGET_HEIGHT == 720)
	CASE_ANCHOR("uvc: 720p verdict=yes mode=1280x720 fps=%u.%03u kbit=%u frames=%u\n",
		    fps_milli / 1000U, fps_milli % 1000U, kbit, frames);
#elif (CONFIG_UVC_TARGET_HEIGHT == 720)
	CASE_ANCHOR("uvc: 720p verdict=720line mode=%ux%u fps=%u.%03u kbit=%u frames=%u\n",
		    (unsigned)CONFIG_UVC_TARGET_WIDTH,
		    (unsigned)CONFIG_UVC_TARGET_HEIGHT,
		    fps_milli / 1000U, fps_milli % 1000U, kbit, frames);
#else
	CASE_ANCHOR("uvc: 720p verdict=no target=%ux%u fps=%u.%03u kbit=%u frames=%u\n",
		    (unsigned)CONFIG_UVC_TARGET_WIDTH,
		    (unsigned)CONFIG_UVC_TARGET_HEIGHT,
		    fps_milli / 1000U, fps_milli % 1000U, kbit, frames);
#endif

	if (frames == 0U) {
		CASE_ANCHOR("uvc: stream FAIL no video frame in the window\n");
		return 1;
	}
	if (jpeg_soi == 0U) {
		CASE_ANCHOR("uvc: stream FAIL no frame carried a JPEG SOI\n");
		return 1;
	}
	if (max_size > CONFIG_UVC_FRAME_BYTES) {
		CASE_ANCHOR("uvc: stream FAIL a frame exceeded the pool buffer\n");
		return 1;
	}
	if (stats.err_packets != 0U) {
		CASE_ANCHOR("uvc: stream FAIL transport errors during the window\n");
		return 1;
	}
	if (stats.dropped > (frames / 10U)) {
		CASE_ANCHOR("uvc: stream FAIL %u frames dropped in %u\n",
			    (unsigned)stats.dropped, frames);
		return 1;
	}

	CASE_ANCHOR("uvc: stream PASS\n");
	return 0;
}
