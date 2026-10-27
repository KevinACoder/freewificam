/*
 * @file    uvc_app.c
 * @brief   Application half of the video class: run/stop hooks, bring-up and
 *          the open policy the camera cases share.
 * @author  zhugengyu
 * @date    2026-10-27
 * @design  template method - the imported class driver owns matching and the
 *          enumeration/opening transport (class/video/usbh_video.c) and calls
 *          the weak run/stop hooks; this file is the integrator's half, the
 *          same split the storage class uses (ports/cherryusb/class/msc_app.c).
 *          The strategy the hooks carry belongs to the application: which
 *          controller to bring up, what to wait for, and which format and
 *          resolution to open.
 *
 * The camera cases share the state this file keeps: the first case brings the
 * host controller up and waits for the class instance the run hook captures;
 * the open step brings the camera to the 640x480 MJPEG configuration with a
 * hardcoded alternate setting (the smallest sufficient one on this camera,
 * recorded in the board's camera ground truth); the stream cases reuse that
 * open state.
 *
 * The camera is never closed.  A stream stop that takes this camera out of
 * its streaming setting can leave it refusing the video-streaming class
 * requests for the rest of the boot, and the recovery sequences measured so
 * far are not reproducible; leaving it open is the workaround the camera line
 * ships.  The class driver's close path stays untouched. The user-level
 * consequence is that the stream cases start from the camera the open case
 * already brought up, instead of re-opening a closed one.
 */

#include <stdint.h>

#include "os/cmsis2/cmsis2.h"

#include "log.h"

#include "usb_glue.h"

#include "usbh_core.h"
#include "usbh_video.h"
#include "usbh_uvc_stream.h"

#include "uvc_app.h"

/* The camera sits on the first USB2 root on this board (the panel hub group);
 * the profile can move it by defining UVC_USB_BUS. */
#ifndef UVC_USB_BUS
#define UVC_USB_BUS 0
#endif

#define UVC_APP_WIDTH      640U
#define UVC_APP_HEIGHT     480U

/* 640x480 MJPEG's smallest sufficient alternate setting on this camera:
 * 640-byte isochronous transactions (the descriptor ladder's alt4). */
#define UVC_APP_ALTSETTING 4U

/* The camera's enumeration window, in 100 ms steps (60 s). */
#define UVC_APP_WAIT_STEPS 600U

static struct usbh_video *uvc_instance;
static int uvc_bus_up;

unsigned uvc_app_bus(void)
{
	return UVC_USB_BUS;
}

void usbh_video_run(struct usbh_video *video_class)
{
	uvc_instance = video_class;
	(void)usbh_video_stream_attach(video_class);

	LOG_ANCHOR("uvc: connect vid=%04x pid=%04x bus=%u dev=%u minor=%u formats=%u alts=%u\n",
		    (unsigned)video_class->hport->device_desc.idVendor,
		    (unsigned)video_class->hport->device_desc.idProduct,
		    (unsigned)video_class->hport->bus->busid,
		    (unsigned)video_class->hport->dev_addr,
		    (unsigned)video_class->minor,
		    (unsigned)video_class->num_of_formats,
		    (unsigned)video_class->num_of_intf_altsettings);
}

void usbh_video_stop(struct usbh_video *video_class)
{
	if (video_class == uvc_instance) {
		uvc_instance = 0;
	}

	LOG_ANCHOR("uvc: disconnect bus=%u dev=%u\n",
		    (unsigned)video_class->hport->bus->busid,
		    (unsigned)video_class->hport->dev_addr);
}

struct usbh_video *uvc_app_class(void)
{
	return uvc_instance;
}

/* Bring the camera's controller up once and wait for the class instance.
 * Idempotent: the second and later cases reuse the controller and the
 * instance.  Returns 0 when the camera is known, a negative value otherwise
 * (the caller prints the failure with its own reason). */
int uvc_app_ensure_up(void)
{
	struct usbh_video *video_class;
	unsigned spin;

	if (uvc_bus_up == 0) {
		if (usb_board_init(UVC_USB_BUS) != 0) {
			LOG_ERROR("uvc: usb board init failed bus=%u\n",
				  (unsigned)UVC_USB_BUS);
			return -1;
		}
		if (usbh_initialize((uint8_t)UVC_USB_BUS,
				    usb_board_reg_base(UVC_USB_BUS),
				    usb_board_event_handler) != 0) {
			LOG_ERROR("uvc: usbh_initialize failed bus=%u\n",
				  (unsigned)UVC_USB_BUS);
			return -1;
		}
		usb_board_after_start((uint8_t)UVC_USB_BUS);

		LOG_ANCHOR("uvc: host up bus=%u hcd=ehci base=0x%lx\n",
			    (unsigned)UVC_USB_BUS,
			    (unsigned long)usb_board_reg_base(UVC_USB_BUS));
		uvc_bus_up = 1;
	}

	for (spin = 0; spin < UVC_APP_WAIT_STEPS; spin++) {
		video_class = uvc_instance;
		if (video_class != 0) {
			return 0;
		}
		osDelay(100U);
	}

	LOG_ERROR("uvc: no video class instance within %u s\n",
		  (unsigned)(UVC_APP_WAIT_STEPS / 10U));
	return -1;
}

/* Idempotent: the class driver treats an already-open camera as success, so
 * the cases can call this without ordering constraints.  Returns 0 when the
 * camera is open, a negative value otherwise. */
int uvc_app_ensure_open(void)
{
	struct usbh_video *video_class = uvc_instance;
	int ret;

	if (video_class == 0) {
		return -1;
	}
	if (video_class->is_opened) {
		return 0;
	}

	ret = usbh_video_open(video_class, USBH_VIDEO_FORMAT_MJPEG,
			      UVC_APP_WIDTH, UVC_APP_HEIGHT, UVC_APP_ALTSETTING);
	if (ret < 0) {
		LOG_ERROR("uvc: open failed ret=%d\n", ret);
		return ret;
	}

	LOG_ANCHOR("uvc: open mjpeg %ux%u alt=%u fmt=%u frm=%u mps=%u dmp=%u ival=%u\n",
		    UVC_APP_WIDTH, UVC_APP_HEIGHT, UVC_APP_ALTSETTING,
		    (unsigned)video_class->probe.bFormatIndex,
		    (unsigned)video_class->probe.bFrameIndex,
		    (unsigned)video_class->isoin_mps,
		    (unsigned)video_class->probe.dwMaxPayloadTransferSize,
		    (unsigned)(video_class->isoin ? video_class->isoin->bInterval : 0));
	return 0;
}
