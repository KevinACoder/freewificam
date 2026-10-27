/*
 * @file   uvc_open.c
 * @brief  Case uvc_open: negotiate the target configuration on the camera.
 *
 * The open body is the application half's policy call (ports/cherryusb/class/
 * uvc_app.c), which runs the class driver's probe/commit sequence and selects
 * the alternate setting through SET_INTERFACE (class/video/usbh_video.c).  The
 * anchor line carries what the negotiation returned: format/frame indices,
 * the alternate setting, the endpoint's payload bound and the device's
 * reported payload size.
 *
 * Checks: the camera is open, it has an isochronous endpoint, the endpoint's
 * payload bound is a real one, and the interval is inside the engine's
 * micro-frame table (an interval outside it would be rejected at the first
 * submit, which would look like a stream failure much later).
 *
 * Anchors (tools/rtos/judge.py matches these):
 *   uvc: open mjpeg 640x480 alt=N fmt=N frm=N mps=N dmp=N ival=N
 *   uvc: open PASS | uvc: open FAIL <reason>
 */

#include <stdint.h>

#include "cases.h"

#include "usbh_core.h"
#include "usbh_video.h"

#include "uvc_app.h"

#define UVC_OPEN_MPS_MIN  1U
#define UVC_OPEN_MPS_MAX  4096U
#define UVC_OPEN_IVAL_MAX 4U

int case_uvc_open_run(void)
{
	struct usbh_video *video_class;

	if (uvc_app_ensure_open() != 0) {
		CASE_ANCHOR("uvc: open FAIL usbh_video_open returned an error\n");
		return 1;
	}

	video_class = uvc_app_class();
	if (video_class == 0 || !video_class->is_opened) {
		CASE_ANCHOR("uvc: open FAIL camera not open after the call\n");
		return 1;
	}
	if (video_class->isoin == 0) {
		CASE_ANCHOR("uvc: open FAIL no isochronous endpoint after open\n");
		return 1;
	}
	if (video_class->isoin_mps < UVC_OPEN_MPS_MIN ||
	    video_class->isoin_mps > UVC_OPEN_MPS_MAX) {
		CASE_ANCHOR("uvc: open FAIL endpoint payload bound out of range\n");
		return 1;
	}
	if (video_class->isoin->bInterval < 1 ||
	    video_class->isoin->bInterval > UVC_OPEN_IVAL_MAX) {
		CASE_ANCHOR("uvc: open FAIL endpoint interval outside the engine's table\n");
		return 1;
	}

	CASE_ANCHOR("uvc: open PASS\n");
	return 0;
}
