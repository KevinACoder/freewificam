/*
 * @file   uvc_iso_smoke.c
 * @brief  Case uvc_iso_smoke: raw isochronous IN transfers on the camera's
 *         endpoint, independent of any payload assembly.
 *
 * The submission shape follows the class driver's own transport: a urb with a
 * trailing iso-packet array, one packet per micro-frame, driven through the
 * public entry (usbh_submit_urb) so the whole path (scheduler, frame list,
 * completion scan) runs exactly as it does for the stream engine.  The wait is
 * the engine's own synchronous mode; the case does not peek inside the
 * controller.
 *
 * This is the transport's own gate: if the engine is wrong, the failure is
 * local to it and does not have to be untangled from frame/payload assembly.
 *
 * Anchors (tools/rtos/judge.py matches these):
 *   uvc: iso smoke urbs=16 done=N err_urbs=N err_pkts=N bytes=N max=N
 *   uvc: iso smoke PASS | uvc: iso smoke FAIL <reason>
 */

#include <stddef.h>
#include <stdint.h>

#include "cases.h"

#include "usbh_core.h"
#include "usbh_video.h"

#include "uvc_app.h"

#define UVC_SMOKE_PKTS        8U   /* micro-frames per urb: one USB frame */
#define UVC_SMOKE_URBS        16U
#define UVC_SMOKE_TIMEOUT_MS  500U
#define UVC_SMOKE_MAX_PAYLOAD 1024U /* the smoke geometry's single-transaction bound */

/* One urb whose trailing packet array is the storage the flexible array
 * member (urb.iso_packet) refers to; the static assertion pins the two views
 * to the same address, so either name may be used. */
struct uvc_smoke_urb {
	struct usbh_urb urb;
	struct usbh_iso_frame_packet packets[UVC_SMOKE_PKTS];
};

_Static_assert(offsetof(struct uvc_smoke_urb, packets) ==
	       offsetof(struct usbh_urb, iso_packet),
	       "the packet storage must sit where the urb's flexible array starts");

static struct uvc_smoke_urb smoke;
static uint8_t smoke_buf[UVC_SMOKE_PKTS * UVC_SMOKE_MAX_PAYLOAD] __attribute__((aligned(64)));

int case_uvc_iso_smoke_run(void)
{
	struct usbh_video *video_class;
	uint32_t stride;
	unsigned done = 0, err_urbs = 0, err_pkts = 0;
	uint32_t bytes = 0, max_len = 0;
	unsigned round, i;

	if (uvc_app_ensure_open() != 0) {
		CASE_ANCHOR("uvc: iso smoke FAIL camera not open\n");
		return 1;
	}
	video_class = uvc_app_class();

	/* The smoke's geometry is the single-transaction region; a larger payload
	 * bound needs the multi-transaction layout the stream engine owns. */
	if (video_class->isoin_mps < 1 || video_class->isoin_mps > UVC_SMOKE_MAX_PAYLOAD) {
		CASE_ANCHOR("uvc: iso smoke FAIL packet geometry outside the bound\n");
		return 1;
	}
	stride = ((uint32_t)video_class->isoin_mps + 63U) & ~63U;
	if (stride * UVC_SMOKE_PKTS > sizeof(smoke_buf)) {
		CASE_ANCHOR("uvc: iso smoke FAIL buffer smaller than the geometry\n");
		return 1;
	}

	smoke.urb.hport = video_class->hport;
	smoke.urb.ep = video_class->isoin;
	smoke.urb.num_of_iso_packets = UVC_SMOKE_PKTS;
	smoke.urb.timeout = UVC_SMOKE_TIMEOUT_MS;
	for (i = 0; i < UVC_SMOKE_PKTS; i++) {
		smoke.packets[i].transfer_buffer = smoke_buf + (i * stride);
		smoke.packets[i].transfer_buffer_length = video_class->isoin_mps;
	}

	for (round = 0; round < UVC_SMOKE_URBS; round++) {
		int ret;

		for (i = 0; i < UVC_SMOKE_PKTS; i++) {
			smoke.packets[i].actual_length = 0;
			smoke.packets[i].errorcode = 0;
		}
		smoke.urb.actual_length = 0;
		smoke.urb.errorcode = 0;
		smoke.urb.timeout = UVC_SMOKE_TIMEOUT_MS;

		ret = usbh_submit_urb(&smoke.urb);
		if (ret == 0) {
			done++;
			bytes += smoke.urb.actual_length;
		} else {
			err_urbs++;
		}

		for (i = 0; i < UVC_SMOKE_PKTS; i++) {
			if (smoke.packets[i].errorcode != 0) {
				err_pkts++;
			}
			if (smoke.packets[i].actual_length > max_len) {
				max_len = smoke.packets[i].actual_length;
			}
		}
	}

	CASE_ANCHOR("uvc: iso smoke urbs=%u done=%u err_urbs=%u err_pkts=%u bytes=%u max=%u\n",
		    UVC_SMOKE_URBS, done, err_urbs, err_pkts, bytes, max_len);

	if (done == 0) {
		CASE_ANCHOR("uvc: iso smoke FAIL no urb completed\n");
		return 1;
	}
	if (done < UVC_SMOKE_URBS * 3U / 4U) {
		CASE_ANCHOR("uvc: iso smoke FAIL too few urbs completed\n");
		return 1;
	}
	if (max_len > video_class->isoin_mps) {
		CASE_ANCHOR("uvc: iso smoke FAIL packet longer than the endpoint bound\n");
		return 1;
	}

	CASE_ANCHOR("uvc: iso smoke PASS\n");
	return 0;
}
