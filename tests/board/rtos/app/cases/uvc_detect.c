/*
 * @file   uvc_detect.c
 * @brief  Case uvc_detect: bring the camera's host controller up, identify the
 *         camera, and print the mode table plus the 720-line feasibility
 *         census.
 *
 * The census probes each 720-line mode's frame intervals (and the profile's
 * target / 640x480 control mode) without committing a stream: the camera's
 * dwMaxPayloadTransferSize is compared with the streaming interface's
 * full-speed endpoint bound.  That is the fact the 720p decision needs; the
 * later cases only open and run what this table says is possible.
 *
 * Anchors (tools/rtos/judge.py matches these):
 *   uvc: host up bus=0 hcd=dwc2 base=0x...
 *   uvc: connect vid=046d pid=.... bus=0 dev=N minor=N formats=N alts=N
 *   uvc: mode fmt=... ...
 *   uvc: alt=... ...
 *   uvc: probe fmt=... ... fits=yes|no
 *   uvc: detect PASS | uvc: detect FAIL <reason>
 */

#include <stdint.h>

#include "cases.h"

#include "usbh_core.h"
#include "usbh_video.h"

#include "uvc_app.h"

#define UVC_CAMERA_VID 0x046dU

int case_uvc_detect_run(void)
{
	struct usbh_video *video_class;

	if (uvc_app_ensure_up() != 0) {
		CASE_ANCHOR("uvc: detect FAIL no video class instance\n");
		return 1;
	}

	video_class = uvc_app_class();
	if (video_class == 0) {
		CASE_ANCHOR("uvc: detect FAIL class pointer lost\n");
		return 1;
	}
	if (video_class->hport->device_desc.idVendor != UVC_CAMERA_VID) {
		CASE_ANCHOR("uvc: detect FAIL unexpected video device vid=%04x\n",
			    (unsigned)video_class->hport->device_desc.idVendor);
		return 1;
	}
	if (video_class->hport->bus->busid != uvc_app_bus()) {
		CASE_ANCHOR("uvc: detect FAIL camera on bus %u, expected %u\n",
			    (unsigned)video_class->hport->bus->busid,
			    uvc_app_bus());
		return 1;
	}

	uvc_app_mode_dump();
	uvc_app_probe_census();

	CASE_ANCHOR("uvc: detect PASS\n");
	return 0;
}
