/*
 * @file   uvc_detect.c
 * @brief  Case uvc_detect: bring the camera's host controller up and identify
 *         the camera behind it.
 *
 * The bring-up body follows the storage case (usb_msc.c): board layer, stack
 * initialiser, then wait for the class instance - because the enumeration
 * chain (root port, external hub, camera) either ends in that instance or the
 * run has to say where it stopped.  The wait is bounded so a camera that never
 * appears fails the case instead of hanging the image.
 *
 * The case names this camera: a different video device on the bus would
 * otherwise pass the row and fail every later step.
 *
 * Anchors (tools/rtos/judge.py matches these):
 *   uvc: host up bus=N hcd=ehci base=0x...
 *   uvc: connect vid=046d pid=0990 bus=N dev=N minor=N formats=N alts=N
 *   uvc: detect PASS | uvc: detect FAIL <reason>
 */

#include <stdint.h>

#include "cases.h"

#include "usbh_core.h"
#include "usbh_video.h"

#include "uvc_app.h"

#define UVC_CAMERA_VID 0x046dU
#define UVC_CAMERA_PID 0x0990U

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
	if (video_class->hport->device_desc.idVendor != UVC_CAMERA_VID ||
	    video_class->hport->device_desc.idProduct != UVC_CAMERA_PID) {
		CASE_ANCHOR("uvc: detect FAIL unexpected video device\n");
		return 1;
	}
	if (video_class->hport->bus->busid != uvc_app_bus()) {
		CASE_ANCHOR("uvc: detect FAIL camera on bus %u, expected %u\n",
			    (unsigned)video_class->hport->bus->busid,
			    uvc_app_bus());
		return 1;
	}

	CASE_ANCHOR("uvc: detect PASS\n");
	return 0;
}
