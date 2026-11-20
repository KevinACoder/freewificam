/*
 * @file   uvc_iso_smoke.c
 * @brief  Case uvc_iso_smoke: raw isochronous IN transfers on the camera's
 *         endpoint, independent of payload assembly.
 *
 * Sixteen synchronous URBs, eight packets each, through the public
 * usbh_submit_urb() entry: the DWC2 engine's per-packet recording and the
 * channel-halt path run exactly as they do for the stream engine.  This is
 * the transport gate; if it fails, the failure is local to the ISO engine and
 * does not have to be untangled from UVC frame assembly.
 *
 * Anchors:
 *   uvc: iso smoke urbs=16 done=N err_urbs=N err_pkts=N bytes=N max=N
 *   uvc: iso smoke PASS | uvc: iso smoke FAIL <reason>
 */

#include <stddef.h>
#include <stdint.h>

#include "cases.h"

#include "usbh_core.h"
#include "usbh_video.h"

#include "uvc_app.h"

#define UVC_SMOKE_PKTS        8U
#define UVC_SMOKE_URBS        16U
#define UVC_SMOKE_TIMEOUT_MS  500U
#define UVC_SMOKE_MAX_PAYLOAD 1024U

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
	if (video_class == 0) {
		CASE_ANCHOR("uvc: iso smoke FAIL class pointer lost\n");
		return 1;
	}

	if ((video_class->isoin_mps < 1U) ||
	    (video_class->isoin_mps > UVC_SMOKE_MAX_PAYLOAD)) {
		CASE_ANCHOR("uvc: iso smoke FAIL packet geometry %u outside the bound\n",
			    (unsigned)video_class->isoin_mps);
		return 1;
	}
	stride = ((uint32_t)video_class->isoin_mps + 63U) & ~63U;
	if ((stride * UVC_SMOKE_PKTS) > sizeof(smoke_buf)) {
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
			if (err_urbs == 0U)
				CASE_INFO("uvc: iso smoke first submit rc=%d\n", ret);
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
	if (err_urbs != 0U) {
		CASE_ANCHOR("uvc: iso smoke FAIL %u urbs returned an error\n", err_urbs);
		return 1;
	}
	if (err_pkts != 0U) {
		CASE_ANCHOR("uvc: iso smoke FAIL %u packets carried an error\n", err_pkts);
		return 1;
	}
	if (bytes == 0U) {
		CASE_ANCHOR("uvc: iso smoke FAIL no bytes arrived\n");
		return 1;
	}
	if (max_len > video_class->isoin_mps) {
		CASE_ANCHOR("uvc: iso smoke FAIL packet %u longer than the endpoint bound %u\n",
			    max_len, (unsigned)video_class->isoin_mps);
		return 1;
	}

	CASE_ANCHOR("uvc: iso smoke PASS\n");
	return 0;
}
