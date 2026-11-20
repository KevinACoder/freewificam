/*
 * @file   uvc_open.c
 * @brief  Case uvc_open: negotiate the profile's target MJPEG mode.
 *
 * The open body is app/usb/uvc_app.c: it asks the camera for the target's
 * payload, chooses the smallest streaming alternate setting that carries it,
 * and runs the class's probe/commit + SET_INTERFACE sequence.  The checks here
 * are the full-speed contract: an isochronous IN endpoint, at most 1023 bytes
 * per frame, interval 1 (one frame), and a device payload not above the
 * endpoint bound.
 *
 * Anchors (tools/rtos/judge.py matches these):
 *   uvc: open mjpeg WxH alt=N fmt=N frm=N mps=N dmp=N ival=N
 *   uvc: open PASS | uvc: open FAIL <reason>
 */

#include <stdint.h>

#include "cases.h"

#include "usbh_core.h"
#include "usbh_video.h"

#include "uvc_app.h"

int case_uvc_open_run(void)
{
	struct usbh_video *video_class;

	if (uvc_app_ensure_open() != 0) {
		CASE_ANCHOR("uvc: open FAIL usbh_video_open returned an error\n");
		return 1;
	}

	video_class = uvc_app_class();
	if ((video_class == 0) || !video_class->is_opened) {
		CASE_ANCHOR("uvc: open FAIL camera not open after the call\n");
		return 1;
	}
	if (video_class->isoin == 0) {
		CASE_ANCHOR("uvc: open FAIL no isochronous endpoint after open\n");
		return 1;
	}
	if ((video_class->isoin_mps < 1U) || (video_class->isoin_mps > 1023U)) {
		CASE_ANCHOR("uvc: open FAIL full-speed endpoint bound %u outside 1..1023\n",
			    (unsigned)video_class->isoin_mps);
		return 1;
	}
	if (video_class->isoin->bInterval != 1U) {
		CASE_ANCHOR("uvc: open FAIL endpoint interval %u, full speed needs 1\n",
			    (unsigned)video_class->isoin->bInterval);
		return 1;
	}
	if ((video_class->probe.dwMaxPayloadTransferSize == 0U) ||
	    (video_class->probe.dwMaxPayloadTransferSize > video_class->isoin_mps)) {
		CASE_ANCHOR("uvc: open FAIL device payload %u above endpoint bound %u\n",
			    (unsigned)video_class->probe.dwMaxPayloadTransferSize,
			    (unsigned)video_class->isoin_mps);
		return 1;
	}

	CASE_ANCHOR("uvc: open PASS\n");
	return 0;
}
