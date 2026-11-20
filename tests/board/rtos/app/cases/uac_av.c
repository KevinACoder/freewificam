/*
 * @file   uac_av.c
 * @brief  Case uac_av: run the UVC video stream and the UAC1 microphone
 *         capture together for 20 s and report both sides' counters.
 *
 * This is the line's core unknown: two isochronous endpoints (video IN and
 * audio IN) on one full-speed DWC2 host, each with its own channel and its own
 * 1 ms packet stream.  The gates are the video engine's own frame/error
 * counters and the audio ring's gaps/errors; neither side is allowed to
 * degrade the other.
 *
 * Anchors:
 *   uac: av video_frames=N video_dequeued=N video_bytes=N video_soi=N video_eoi=N
 *              video_err=0 video_urb_err=0 video_dropped=N
 *              audio_packets=N audio_bytes=N audio_nz=N audio_gaps=0 audio_err=0
 *              audio_overrun=0 ms=20000
 *   uac: av PASS | uac: av FAIL <reason>
 */

#include <stdint.h>

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

#include "usbh_core.h"
#include "usbh_video.h"
#include "usbh_uvc_stream.h"
#include "usbh_uac.h"
#include "usbh_uac_stream.h"

#include "uvc_app.h"
#include "uac_app.h"

#define UAC_AV_WINDOW_MS 20000U
#define UAC_AV_POOL_FRAMES 3U
#define UAC_AV_DEQUEUE_MS 20U

#ifndef CONFIG_UVC_FRAME_BYTES
#define CONFIG_UVC_FRAME_BYTES (128U * 1024U)
#endif

static uint8_t av_bufs[UAC_AV_POOL_FRAMES][CONFIG_UVC_FRAME_BYTES]
	__attribute__((section(".sdram"), aligned(64)));
static struct usbh_videoframe av_pool[UAC_AV_POOL_FRAMES];
static uint8_t av_audio_buf[2048] __attribute__((aligned(4)));

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

static void av_measure_audio(const uint8_t *buf, unsigned len, uint32_t *nz, uint32_t *peak)
{
	const int16_t *samples = (const int16_t *)buf;
	unsigned count = len / 2U;

	for (unsigned i = 0; i < count; i++) {
		int32_t v = samples[i];
		int32_t a;

		if (v != 0) {
			(*nz)++;
		}
		a = (v < 0) ? -v : v;
		if ((uint32_t)a > *peak) {
			*peak = (uint32_t)a;
		}
	}
}

int case_uac_av_run(void)
{
	struct usbh_uac *uac;
	struct uac_stream_stats ast;
	struct uvc_stream_stats vst;
	uint32_t start_ms, elapsed_ms, expected;
	uint32_t video_dequeued = 0, video_bytes = 0, video_max = 0;
	uint32_t video_soi = 0, video_eoi = 0, video_timeouts = 0;
	uint32_t audio_read = 0, audio_nz = 0, audio_peak = 0;
	unsigned i;
	int ret;

	if (uac_app_ensure_up() != 0) {
		CASE_ANCHOR("uac: av FAIL no audio class instance\n");
		return 1;
	}
	if (uvc_app_ensure_open() != 0) {
		CASE_ANCHOR("uac: av FAIL camera not open\n");
		return 1;
	}
	uac = uac_app_class();
	if (uac == 0) {
		CASE_ANCHOR("uac: av FAIL audio class pointer lost\n");
		return 1;
	}

	for (i = 0; i < UAC_AV_POOL_FRAMES; i++) {
		av_pool[i].frame_buf = av_bufs[i];
		av_pool[i].frame_bufsize = CONFIG_UVC_FRAME_BYTES;
		av_pool[i].frame_size = 0;
	}
	usbh_video_stream_create(av_pool, UAC_AV_POOL_FRAMES);

	if (uac_app_ensure_open() != 0) {
		CASE_ANCHOR("uac: av FAIL microphone not open\n");
		return 1;
	}

	if (usbh_video_stream_start(CONFIG_UVC_TARGET_WIDTH, CONFIG_UVC_TARGET_HEIGHT,
				    USBH_VIDEO_FORMAT_MJPEG) != 0) {
		CASE_ANCHOR("uac: av FAIL usbh_video_stream_start\n");
		return 1;
	}
	ret = usbh_uac_stream_start();
	if (ret != 0) {
		usbh_video_stream_stop();
		CASE_ANCHOR("uac: av FAIL usbh_uac_stream_start rc=%d\n", ret);
		return 1;
	}

	start_ms = osKernelGetTickCount();
	while ((osKernelGetTickCount() - start_ms) < UAC_AV_WINDOW_MS) {
		struct usbh_videoframe *frame = 0;

		if (usbh_video_stream_dequeue(&frame, UAC_AV_DEQUEUE_MS) == 0) {
			video_dequeued++;
			video_bytes += frame->frame_size;
			if (frame->frame_size > video_max) {
				video_max = frame->frame_size;
			}
			if (frame_is_jpeg_soi(frame)) {
				video_soi++;
			}
			if (frame_is_jpeg_eoi(frame)) {
				video_eoi++;
			}
			usbh_video_stream_enqueue(frame);
		} else {
			video_timeouts++;
		}

		for (;;) {
			int n = usbh_uac_stream_read(av_audio_buf, sizeof(av_audio_buf), 0);

			if (n <= 0) {
				break;
			}
			audio_read += (uint32_t)n;
			av_measure_audio(av_audio_buf, (unsigned)n, &audio_nz, &audio_peak);
		}
	}
	for (;;) {
		int n = usbh_uac_stream_read(av_audio_buf, sizeof(av_audio_buf), 0);

		if (n <= 0) {
			break;
		}
		audio_read += (uint32_t)n;
		av_measure_audio(av_audio_buf, (unsigned)n, &audio_nz, &audio_peak);
	}
	elapsed_ms = osKernelGetTickCount() - start_ms;
	if (elapsed_ms == 0U) {
		elapsed_ms = 1U;
	}

	usbh_uac_stream_stop();
	usbh_video_stream_stop();
	usbh_uac_stream_stats(&ast);
	usbh_video_stream_stats(&vst);

	expected = (ast.rate * (uint32_t)ast.channels * (uint32_t)ast.subframe * elapsed_ms) / 1000U;

	/* Two anchors, not one: the log sink caps a message at 256 bytes, and a
	 * single line carrying both sides' counters is longer than that. */
	CASE_ANCHOR("uac: av video frames=%u dequeued=%u bytes=%u soi=%u eoi=%u "
		    "max=%u err=%u urb_err=%u dropped=%u invalid=%u timeouts=%u\n",
		    (unsigned)vst.frames, (unsigned)video_dequeued, (unsigned)video_bytes,
		    (unsigned)video_soi, (unsigned)video_eoi, (unsigned)video_max,
		    (unsigned)vst.err_packets, (unsigned)vst.urb_errors,
		    (unsigned)vst.dropped, (unsigned)vst.invalid, (unsigned)video_timeouts);
	CASE_ANCHOR("uac: av audio rate=%u ch=%u bits=%u packets=%u urbs=%u "
		    "bytes=%u read=%u nz=%u peak=%u gaps=%u err=%u empty=%u "
		    "overrun=%u shutdown=%u gap_first=%u err_run=%u err_first=%u "
		    "err_last=%u err_mid=%u "
		    "gap_max=%u ms=%u\n",
		    (unsigned)ast.rate, (unsigned)ast.channels,
		    (unsigned)((uint32_t)ast.subframe * 8U),
		    (unsigned)ast.packets, (unsigned)ast.urbs,
		    (unsigned)ast.bytes, (unsigned)audio_read, (unsigned)audio_nz,
		    (unsigned)audio_peak, (unsigned)ast.gaps, (unsigned)ast.err,
		    (unsigned)ast.empty, (unsigned)ast.overrun,
		    (unsigned)ast.shutdown, (unsigned)ast.gap_first,
		    (unsigned)ast.err_max_run, (unsigned)ast.err_first,
		    (unsigned)ast.err_last, (unsigned)ast.err_mid,
		    (unsigned)ast.gap_max, (unsigned)elapsed_ms);

	if (vst.frames < 200U) {
		CASE_ANCHOR("uac: av FAIL video frames=%u below the 200 floor\n",
			    (unsigned)vst.frames);
		return 1;
	}
	if (vst.err_packets != 0U) {
		CASE_ANCHOR("uac: av FAIL %u video packet errors\n", (unsigned)vst.err_packets);
		return 1;
	}
	if (vst.urb_errors != 0U) {
		CASE_ANCHOR("uac: av FAIL %u video urb errors\n", (unsigned)vst.urb_errors);
		return 1;
	}
	if ((video_soi != vst.frames) || (video_eoi != vst.frames)) {
		CASE_ANCHOR("uac: av FAIL jpeg soi=%u eoi=%u frames=%u\n",
			    (unsigned)video_soi, (unsigned)video_eoi, (unsigned)vst.frames);
		return 1;
	}
	if ((ast.packets == 0U) || (ast.bytes == 0U)) {
		CASE_ANCHOR("uac: av FAIL no audio under the video stream\n");
		return 1;
	}
	if (ast.gaps != 0U) {
		CASE_ANCHOR("uac: av FAIL %u audio frames missed\n", (unsigned)ast.gaps);
		return 1;
	}
	if (ast.err != 0U) {
		CASE_ANCHOR("uac: av FAIL %u audio transport errors\n", (unsigned)ast.err);
		return 1;
	}
	if (ast.overrun != 0U) {
		CASE_ANCHOR("uac: av FAIL audio ring overrun %u bytes\n", (unsigned)ast.overrun);
		return 1;
	}
	if ((expected == 0U) || (ast.bytes < (expected * 80U) / 100U) ||
	    (ast.bytes > (expected * 120U) / 100U)) {
		CASE_ANCHOR("uac: av FAIL audio bytes=%u outside 80-120%% of %u\n",
			    (unsigned)ast.bytes, (unsigned)expected);
		return 1;
	}
	if (audio_nz == 0U) {
		CASE_ANCHOR("uac: av FAIL audio PCM is all zero\n");
		return 1;
	}

	CASE_ANCHOR("uac: av PASS\n");
	return 0;
}