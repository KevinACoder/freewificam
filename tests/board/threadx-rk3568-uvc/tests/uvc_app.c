/**
 * @file    uvc_app.c
 * @brief   Application half of the video class: run/stop hooks and open policy.
 * @author  zhugengyu
 * @date    13.10.2026
 * @design  template method - the imported class driver owns matching and the
 *          enumeration/opening transport (class/video/usbh_video.c) and calls
 *          the weak run/stop hooks; this file is the integrator's half, the
 *          same split the storage class uses (ports/cherryusb/class/msc_app.c).
 *          The strategy the hooks carry is the application's: which format and
 *          resolution to open, and when the stream engine starts.
 *
 * The camera cases share the state this file keeps: the detect case waits for
 * the class pointer the run hook captures; the open step brings the camera to
 * the 640x480 MJPEG configuration with a hardcoded alternate setting (the
 * smallest sufficient one on this camera, recorded in the board's camera
 * ground truth) and prints what the negotiation returned.
 */

#include <stdint.h>

#include "log.h"

#include "usbh_core.h"
#include "usbh_video.h"
#include "usbh_uvc_stream.h"

/* The acceptance slice's format: 640x480 MJPEG - the only configuration the
 * Linux ground truth on this board was captured with. */
#define UVC_APP_WIDTH      640U
#define UVC_APP_HEIGHT     480U

/* The 640x480 MJPEG configuration's smallest sufficient alternate setting on
 * this camera: 640-byte isochronous transactions, the descriptor ladder's
 * alt4. The stream round replaces this constant with the selection rule; the
 * bare smoke round uses it as a known-good operating point. */
#define UVC_APP_ALTSETTING 4U

static struct usbh_video *uvc_instance;

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

/* Close the camera if it is open.  The stream case uses this to hand the
 * camera back so the stream engine performs its own open sequence (probe,
 * bandwidth question, alternate-setting selection). */
int uvc_app_close(void)
{
	if ((uvc_instance != 0) && uvc_instance->is_opened) {
		return usbh_video_close(uvc_instance);
	}
	return 0;
}

/* Idempotent: the class driver treats an already-open camera as success, so
 * the cases can call this without ordering constraints. Returns 0 when the
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
