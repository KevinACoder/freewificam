/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * pi_stream.c - capture the camera and send it to the Pi 4B.
 *
 * The stage is a profile decision, not a runtime switch:
 *   1 = link only (join + DHCP + ICMP)
 *   2 = one captured JPEG to the Pi, then stop
 *   3 = 20 s of 640x480 MJPEG to the Pi
 *   4 = 20 s of MJPEG + the camera microphone's PCM
 *
 * The camera and the microphone are driven by the existing first-party UVC
 * and UAC classes; this file only owns the consumer side.  The network half
 * lives in pi_net.c (WICED flag world), so nothing here includes a WICED
 * header.
 *
 * Anchors:
 *   stream: link join_rc=... join_ms=... dhcp=... ip=... gw=...
 *   stream: ping sent=20 recv=...
 *   stream: rx uvc frames=... bytes=... soi=... eoi=... max=... dropped=...
 *              invalid=... err=... urb_err=...
 *   stream: tx video msgs=... bytes=... drops=... connects=... fail=...
 *   stream: rx uac rate=... ch=... packets=... bytes=... gaps=... err=...
 *              overrun=... nz=... peak=...
 *   stream: tx audio blocks=... bytes=... drops=... connects=... fail=...
 *   stream: PASS LINK|FRAME|VIDEO|AV
 *
 * No print is made while the isochronous urbs are in flight: the console
 * print lock disables interrupts, and a print there costs camera frames.
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "usbh_core.h"
#include "usbh_video.h"
#include "usbh_uvc_stream.h"
#include "usb_glue.h"
#include "usbh_uac.h"
#include "usbh_uac_stream.h"

#include "uvc_app.h"
#include "uac_app.h"

#include "pi_net.h"
#include "fwc_proto.h"

extern int case_uvc_iso_smoke_run(void);

#ifndef CONFIG_STREAM_STAGE
#define CONFIG_STREAM_STAGE 1
#endif
#ifndef CONFIG_STREAM_WINDOW_MS
#define CONFIG_STREAM_WINDOW_MS 20000U
#endif
#ifndef CONFIG_STREAM_VIDEO_PORT
#define CONFIG_STREAM_VIDEO_PORT 5000U
#endif
#ifndef CONFIG_STREAM_AUDIO_PORT
#define CONFIG_STREAM_AUDIO_PORT 5001U
#endif
#ifndef CONFIG_UVC_FRAME_BYTES
#define CONFIG_UVC_FRAME_BYTES (128U * 1024U)
#endif

#define STREAM_POOL_FRAMES 3U
#define STREAM_AUDIO_BLOCK 640U
#define STREAM_AUDIO_MAX (2U * STREAM_AUDIO_BLOCK)

#if CONFIG_STREAM_STAGE >= 2
static uint8_t stream_bufs[STREAM_POOL_FRAMES][CONFIG_UVC_FRAME_BYTES]
	__attribute__((section(".sdram"), aligned(64)));
static struct usbh_videoframe stream_pool[STREAM_POOL_FRAMES];
#endif

static volatile int s_stop;
static volatile int s_video_done;
static volatile int s_audio_done;
static volatile int s_one_shot;

static volatile uint32_t s_video_msgs;
static volatile uint32_t s_video_bytes;
static volatile uint32_t s_video_drops;
static volatile uint32_t s_video_fail;
static volatile uint32_t s_video_connects;
static volatile uint32_t s_video_soi;
static volatile uint32_t s_video_eoi;
static volatile uint32_t s_video_frames;

static volatile uint32_t s_audio_blocks;
static volatile uint32_t s_audio_bytes;
static volatile uint32_t s_audio_drops;
static volatile uint32_t s_audio_fail;
static volatile uint32_t s_audio_connects;
static volatile uint32_t s_audio_nz;
static volatile uint32_t s_audio_peak;

static uint32_t
now_ms(void)
{
	return (uint32_t)osKernelGetTickCount();
}

static int
frame_is_jpeg_soi(const struct usbh_videoframe *frame)
{
	return (frame->frame_size >= 2U) &&
	       (frame->frame_buf[0] == 0xffU) &&
	       (frame->frame_buf[1] == 0xd8U);
}

static int
frame_is_jpeg_eoi(const struct usbh_videoframe *frame)
{
	return (frame->frame_size >= 2U) &&
	       (frame->frame_buf[frame->frame_size - 2U] == 0xffU) &&
	       (frame->frame_buf[frame->frame_size - 1U] == 0xd9U);
}

static int
reconnect_video(int *fd, uint32_t *next_ms)
{
	uint32_t now = now_ms();

	if (*fd >= 0 || now < *next_ms)
		return 0;
	*fd = pi_sender_connect((uint16_t)CONFIG_STREAM_VIDEO_PORT);
	if (*fd >= 0) {
		s_video_connects++;
		return 1;
	}
	*next_ms = now + 500U;
	return 0;
}

static void
video_sender(void *arg)
{
	int fd = -1;
	uint32_t seq = 0U;
	uint32_t next_ms = 0U;

	(void)arg;

	for (;;) {
		struct usbh_videoframe *frame = 0;
		int sent = 0;

		if (s_stop)
			break;
		if (usbh_video_stream_dequeue(&frame, 100U) != 0)
			continue;
		if (frame == 0)
			continue;

		s_video_frames++;
		if (frame_is_jpeg_soi(frame))
			s_video_soi++;
		if (frame_is_jpeg_eoi(frame))
			s_video_eoi++;

		if (!s_stop) {
			if (fd < 0)
				(void)reconnect_video(&fd, &next_ms);
			if (fd >= 0) {
				uint32_t crc = fwc_crc32(frame->frame_buf,
							 frame->frame_size);

				if (pi_sender_send(&fd, FWC_TYPE_VIDEO,
						   frame->frame_buf,
						   frame->frame_size,
						   now_ms() * 1000U, seq,
						   crc) == 0) {
					seq++;
					s_video_msgs++;
					s_video_bytes += frame->frame_size;
					sent = 1;
				} else {
					s_video_fail++;
					next_ms = now_ms() + 500U;
				}
			} else {
				s_video_drops++;
			}
		}

		usbh_video_stream_enqueue(frame);
		if (s_one_shot && sent)
			break;
	}
	/* Leave the socket open: closing it here raced the last segment and
	 * the receiver saw a truncated message.  The image is one boot; the
	 * power cycle closes the connection. */
	s_video_done = 1;
	osThreadExit();
}

static void
measure_audio(const uint8_t *buf, unsigned len)
{
	const int16_t *samples = (const int16_t *)buf;
	unsigned count = len / 2U;
	unsigned i;

	for (i = 0U; i < count; i++) {
		int32_t v = samples[i];
		uint32_t a;

		if (v != 0)
			s_audio_nz++;
		a = (uint32_t)((v < 0) ? -v : v);
		if (a > s_audio_peak)
			s_audio_peak = a;
	}
}

static int
reconnect_audio(int *fd, uint32_t *next_ms)
{
	uint32_t now = now_ms();

	if (*fd >= 0 || now < *next_ms)
		return 0;
	*fd = pi_sender_connect((uint16_t)CONFIG_STREAM_AUDIO_PORT);
	if (*fd >= 0) {
		s_audio_connects++;
		return 1;
	}
	*next_ms = now + 500U;
	return 0;
}

static void
audio_sender(void *arg)
{
	uint8_t buf[STREAM_AUDIO_MAX];
	uint32_t fill = 0U;
	uint32_t seq = 0U;
	uint32_t next_ms = 0U;
	uint32_t t0 = now_ms();
	int fd = -1;

	(void)arg;

	for (;;) {
		int n = 0;

		if (s_stop)
			break;

		if (fill < STREAM_AUDIO_BLOCK) {
			n = usbh_uac_stream_read(buf + fill,
						 STREAM_AUDIO_MAX - fill, 5U);
			if (n > 0)
				fill += (uint32_t)n;
		}

		if (fill >= STREAM_AUDIO_BLOCK ||
		    (fill > 0U && (now_ms() - t0) >= 20U)) {
			measure_audio(buf, fill);
			if (fd < 0)
				(void)reconnect_audio(&fd, &next_ms);
			if (fd >= 0) {
				uint32_t crc = fwc_crc32(buf, fill);

				if (pi_sender_send(&fd, FWC_TYPE_AUDIO, buf,
						   fill, now_ms() * 1000U,
						   seq, crc) == 0) {
					seq++;
					s_audio_blocks++;
					s_audio_bytes += fill;
				} else {
					s_audio_fail++;
					next_ms = now_ms() + 500U;
				}
			} else {
				s_audio_drops++;
			}
			fill = 0U;
			t0 = now_ms();
		} else if (n <= 0) {
			osDelay(1U);
		}
	}
	s_audio_done = 1;
	osThreadExit();
}

#if CONFIG_STREAM_STAGE >= 2
static int
video_start(void)
{
	unsigned i;

	for (i = 0U; i < STREAM_POOL_FRAMES; i++) {
		stream_pool[i].frame_buf = stream_bufs[i];
		stream_pool[i].frame_bufsize = CONFIG_UVC_FRAME_BYTES;
		stream_pool[i].frame_size = 0U;
	}
	usbh_video_stream_create(stream_pool, STREAM_POOL_FRAMES);
	if (usbh_video_stream_start(CONFIG_UVC_TARGET_WIDTH,
				    CONFIG_UVC_TARGET_HEIGHT,
				    USBH_VIDEO_FORMAT_MJPEG) != 0)
		return -1;
	return 0;
}
#endif

static int
wait_flag(volatile int *flag, uint32_t timeout_ms)
{
	uint32_t start = now_ms();

	while (!*flag) {
		if ((now_ms() - start) >= timeout_ms)
			return -1;
		osDelay(10U);
	}
	return 0;
}

int
case_pi_stream_run(void)
{
	struct pi_link_info link;
	int rc;

	if (pi_net_up(&link) != 0) {
		CASE_ANCHOR("stream: link join_rc=%d join_ms=%u dhcp=%d ip=%s gw=%s\n",
			    link.join_rc, link.join_ms, link.dhcp,
			    link.ip[0] ? link.ip : "?",
			    link.gw[0] ? link.gw : "?");
		CASE_ANCHOR("stream: FAIL LINK join_rc=%d\n", link.join_rc);
		return 1;
	}
	CASE_ANCHOR("stream: link join_rc=%d join_ms=%u dhcp=%d ip=%s gw=%s\n",
		    link.join_rc, link.join_ms, link.dhcp, link.ip, link.gw);

#if CONFIG_STREAM_STAGE == 1
	{
		unsigned recv = 0U;

		/* DHCP's last ACK and the AP's neighbour entry settle after the
		 * address appears; the first ICMP would otherwise race the ARP
		 * exchange.  Give the link one second, then send the same 20. */
		osDelay(1000U);
		(void)pi_net_ping(20U, &recv);
		CASE_ANCHOR("stream: ping sent=20 recv=%u\n", recv);
		if (link.dhcp != 0 && recv == 20U) {
			CASE_ANCHOR("stream: PASS LINK\n");
			return 0;
		}
		CASE_ANCHOR("stream: FAIL LINK dhcp=%d recv=%u\n",
			    link.dhcp, recv);
		return 1;
	}
#else
	{
		struct usbh_video *video;
		struct usbh_uac *uac = 0;
		struct uvc_stream_stats vst;
		struct uac_stream_stats ast;
		uint32_t start_ms;
		uint32_t elapsed_ms;
		uint32_t expected;

		memset(&vst, 0, sizeof(vst));
		memset(&ast, 0, sizeof(ast));

#if CONFIG_STREAM_STAGE >= 4
		if (uac_app_ensure_up() != 0) {
			CASE_ANCHOR("stream: FAIL AV no audio class\n");
			return 1;
		}
#else
		if (uvc_app_ensure_up() != 0) {
			CASE_ANCHOR("stream: FAIL VIDEO no camera\n");
			return 1;
		}
#endif
		video = uvc_app_class();
		if (video == 0) {
			CASE_ANCHOR("stream: FAIL cam no class\n");
			return 1;
		}
		if (uvc_app_ensure_open() != 0) {
			CASE_ANCHOR("stream: FAIL cam open\n");
			return 1;
		}
		/* The class open alone leaves this camera silent under the stream
		 * engine; one raw ISO batch (the same shape the camera ladder's
		 * smoke case uses) puts the endpoint in motion before the engine's
		 * own urbs take over. */
		{
			struct pi_port_stats ps;
			pi_net_port_stats(&ps);
			CASE_INFO("stream: pre-smoke sdio_intrs=%lu rx=%lu tx=%lu bufail=%lu usb_irqs=%u\n",
				  (unsigned long)ps.sdio_intrs,
				  (unsigned long)ps.rx_frames,
				  (unsigned long)ps.tx_frames,
				  (unsigned long)ps.buffer_fail,
				  usb_board_irq_count(0));
		}
		(void)case_uvc_iso_smoke_run();
		{
			struct pi_port_stats ps;
			pi_net_port_stats(&ps);
			volatile uint32_t *base =
				(volatile uint32_t *)usb_board_reg_base(0);
			CASE_INFO("stream: post-smoke sdio_intrs=%lu rx=%lu tx=%lu bufail=%lu usb_irqs=%u storm=%u\n",
				  (unsigned long)ps.sdio_intrs,
				  (unsigned long)ps.rx_frames,
				  (unsigned long)ps.tx_frames,
				  (unsigned long)ps.buffer_fail,
				  usb_board_irq_count(0),
				  usb_board_irq_storm_get());
			CASE_INFO("stream: dwc2 gintsts=%08x haint=%08x haintmsk=%08x hprt=%08x\n",
				  (unsigned)base[0x14 / 4],
				  (unsigned)base[0x518 / 4],
				  (unsigned)base[0x51C / 4],
				  (unsigned)base[0x440 / 4]);
		}
#if CONFIG_STREAM_STAGE >= 4
		if (uac_app_ensure_open() != 0) {
			CASE_ANCHOR("stream: FAIL AV mic open\n");
			return 1;
		}
		uac = uac_app_class();
		if (uac == 0) {
			CASE_ANCHOR("stream: FAIL AV mic class\n");
			return 1;
		}
#endif
		rc = video_start();
		CASE_INFO("stream: video_start rc=%d t=%lu\n", rc,
			  (unsigned long)now_ms());
		if (rc != 0) {
			CASE_ANCHOR("stream: FAIL VIDEO stream start\n");
			return 1;
		}
#if CONFIG_STREAM_STAGE >= 4
		if (usbh_uac_stream_start() != 0) {
			usbh_video_stream_stop();
			CASE_ANCHOR("stream: FAIL AV uac start\n");
			return 1;
		}
#endif

		s_one_shot = (CONFIG_STREAM_STAGE == 2) ? 1 : 0;
		if (osThreadNew(video_sender, NULL,
				&(osThreadAttr_t){
					.name = "pi_video",
					.stack_size = 8192U,
					.priority = osPriorityNormal,
				}) == NULL) {
			CASE_ANCHOR("stream: FAIL VIDEO video thread\n");
			usbh_video_stream_stop();
			return 1;
		}
#if CONFIG_STREAM_STAGE >= 4
		if (osThreadNew(audio_sender, NULL,
				&(osThreadAttr_t){
					.name = "pi_audio",
					.stack_size = 4096U,
					.priority = osPriorityNormal,
				}) == NULL) {
			s_stop = 1;
			(void)wait_flag(&s_video_done, 2000U);
			usbh_uac_stream_stop();
			usbh_video_stream_stop();
			CASE_ANCHOR("stream: FAIL AV audio thread\n");
			return 1;
		}
#endif

		start_ms = now_ms();
		if (CONFIG_STREAM_STAGE == 2)
			rc = wait_flag(&s_video_done, 10000U);
		else
			rc = 0;
		if (CONFIG_STREAM_STAGE != 2) {
			/* No console output while the isochronous urbs are in
			 * flight: the print lock masks interrupts, and each
			 * line costs the microphone a 1 ms packet. */
			while ((now_ms() - start_ms) <
			       (uint32_t)CONFIG_STREAM_WINDOW_MS)
				osDelay(50U);
		}

		s_stop = 1;
		if (wait_flag(&s_video_done, 2000U) != 0)
			rc = -1;
#if CONFIG_STREAM_STAGE >= 4
		if (wait_flag(&s_audio_done, 2000U) != 0)
			rc = -1;
#endif
		elapsed_ms = now_ms() - start_ms;
		if (elapsed_ms == 0U)
			elapsed_ms = 1U;

#if CONFIG_STREAM_STAGE >= 4
		usbh_uac_stream_stop();
#endif
		usbh_video_stream_stop();
		usbh_video_stream_stats(&vst);
#if CONFIG_STREAM_STAGE >= 4
		usbh_uac_stream_stats(&ast);
#endif

		CASE_ANCHOR("stream: rx uvc frames=%lu bytes=%llu soi=%lu eoi=%lu max=%lu dropped=%lu invalid=%lu err=%lu urb_err=%lu\n",
			    (unsigned long)vst.frames,
			    (unsigned long long)vst.bytes,
			    (unsigned long)s_video_soi,
			    (unsigned long)s_video_eoi,
			    (unsigned long)(s_video_frames ? s_video_bytes / s_video_frames : 0U),
			    (unsigned long)vst.dropped,
			    (unsigned long)vst.invalid,
			    (unsigned long)vst.err_packets,
			    (unsigned long)vst.urb_errors);
		CASE_ANCHOR("stream: tx video frames=%lu msgs=%lu bytes=%lu drops=%lu connects=%lu fail=%lu\n",
			    (unsigned long)s_video_frames,
			    (unsigned long)s_video_msgs,
			    (unsigned long)s_video_bytes,
			    (unsigned long)s_video_drops,
			    (unsigned long)s_video_connects,
			    (unsigned long)s_video_fail);

#if CONFIG_STREAM_STAGE >= 4
		CASE_ANCHOR("stream: rx uac rate=%lu ch=%u packets=%lu bytes=%lu gaps=%lu first=%lu max=%lu err=%lu overrun=%lu nz=%lu peak=%lu\n",
			    (unsigned long)ast.rate, (unsigned)ast.channels,
			    (unsigned long)ast.packets,
			    (unsigned long)ast.bytes,
			    (unsigned long)ast.gaps,
			    (unsigned long)ast.gap_first,
			    (unsigned long)ast.gap_max,
			    (unsigned long)ast.err,
			    (unsigned long)ast.overrun,
			    (unsigned long)s_audio_nz,
			    (unsigned long)s_audio_peak);
		CASE_ANCHOR("stream: tx audio blocks=%lu bytes=%lu drops=%lu connects=%lu fail=%lu\n",
			    (unsigned long)s_audio_blocks,
			    (unsigned long)s_audio_bytes,
			    (unsigned long)s_audio_drops,
			    (unsigned long)s_audio_connects,
			    (unsigned long)s_audio_fail);
#endif

		if (CONFIG_STREAM_STAGE == 2) {
			if (s_video_msgs == 1U && s_video_bytes > 0U &&
			    s_video_soi == 1U && s_video_eoi == 1U) {
				CASE_ANCHOR("stream: PASS FRAME\n");
				return 0;
			}
			CASE_ANCHOR("stream: FAIL FRAME msgs=%lu soi=%lu eoi=%lu\n",
				    (unsigned long)s_video_msgs,
				    (unsigned long)s_video_soi,
				    (unsigned long)s_video_eoi);
			return 1;
		}

		if (vst.frames < 200U || vst.err_packets != 0U ||
		    vst.urb_errors != 0U || vst.dropped > 5U ||
		    s_video_soi != vst.frames || s_video_eoi != vst.frames ||
		    s_video_msgs < 190U || s_video_fail > 5U ||
		    s_video_connects == 0U) {
			CASE_ANCHOR("stream: FAIL VIDEO frames=%lu msgs=%lu soi=%lu eoi=%lu err=%lu urb_err=%lu dropped=%lu fail=%lu\n",
				    (unsigned long)vst.frames,
				    (unsigned long)s_video_msgs,
				    (unsigned long)s_video_soi,
				    (unsigned long)s_video_eoi,
				    (unsigned long)vst.err_packets,
				    (unsigned long)vst.urb_errors,
				    (unsigned long)vst.dropped,
				    (unsigned long)s_video_fail);
			return 1;
		}

#if CONFIG_STREAM_STAGE >= 4
		expected = (ast.rate * (uint32_t)ast.channels *
			    (uint32_t)ast.subframe * elapsed_ms) / 1000U;
		/* The sender's block count depends on the UAC urb size (one
		 * read per completed urb), so gate on bytes and the rate
		 * window instead of a block count. */
		if (ast.packets == 0U || ast.bytes == 0U || ast.gaps != 0U ||
		    ast.err != 0U || ast.overrun != 0U ||
		    s_audio_nz == 0U ||
		    (expected != 0U &&
		     (ast.bytes < (expected * 80U) / 100U ||
		      ast.bytes > (expected * 120U) / 100U))) {
			CASE_ANCHOR("stream: FAIL AV packets=%lu bytes=%lu gaps=%lu err=%lu overrun=%lu blocks=%lu nz=%lu\n",
				    (unsigned long)ast.packets,
				    (unsigned long)ast.bytes,
				    (unsigned long)ast.gaps,
				    (unsigned long)ast.err,
				    (unsigned long)ast.overrun,
				    (unsigned long)s_audio_blocks,
				    (unsigned long)s_audio_nz);
			return 1;
		}
		CASE_ANCHOR("stream: PASS AV\n");
#else
		CASE_ANCHOR("stream: PASS VIDEO\n");
#endif
		return 0;
	}
#endif
}
