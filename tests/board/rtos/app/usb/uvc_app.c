/*
 * @file   uvc_app.c
 * @brief  Application half of the CherryUSB video class on the STM32F429.
 *
 * The imported class driver (class/video/usbh_video.c) owns matching, the
 * UVC control transport and the probe/commit sequence; this file owns the
 * board policy: one OTG_HS host, one camera, the target mode, and the mode
 * census that answers whether the camera's 720-line mode can fit a full-speed
 * isochronous endpoint at all.
 *
 * The camera is never closed after a stream.  A stream stop that takes this
 * family out of its streaming setting can leave it refusing the
 * video-streaming class requests for the rest of the boot; the old line's
 * workaround is to keep the camera open and let the next case reuse it.
 */

#include <stdint.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"

#include "log.h"

#include "usb_glue.h"

#include "usbh_core.h"
#include "usbh_video.h"
#include "usbh_uvc_stream.h"

#include "uvc_app.h"

#ifndef CONFIG_UVC_TARGET_WIDTH
#define CONFIG_UVC_TARGET_WIDTH 1280U
#endif
#ifndef CONFIG_UVC_TARGET_HEIGHT
#define CONFIG_UVC_TARGET_HEIGHT 720U
#endif

#define UVC_USB_BUS 0U
#define UVC_WAIT_STEPS 600U /* 100 ms each: 60 s */

/* The class's probe/commit helpers are not declared in its header at this
 * pin; the stream engine declares the same two. */
int usbh_videostreaming_get_cur_probe(struct usbh_video *video_class);
int usbh_videostreaming_set_cur_probe(struct usbh_video *video_class,
                                      uint8_t formatindex, uint8_t frameindex,
                                      uint32_t dwFrameInterval);

static struct usbh_video *uvc_instance;
static int uvc_bus_up;

unsigned uvc_app_bus(void)
{
	return UVC_USB_BUS;
}

struct usbh_video *uvc_app_class(void)
{
	return uvc_instance;
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

		LOG_ANCHOR("uvc: host up bus=%u hcd=dwc2 base=0x%lx\n",
			   (unsigned)UVC_USB_BUS,
			   (unsigned long)usb_board_reg_base(UVC_USB_BUS));
		uvc_bus_up = 1;
	}

	for (spin = 0; spin < UVC_WAIT_STEPS; spin++) {
		video_class = uvc_instance;
		if (video_class != 0) {
			return 0;
		}
		osDelay(100U);
	}

	LOG_ERROR("uvc: no video class instance within %u s\n",
		  (unsigned)(UVC_WAIT_STEPS / 10U));
	return -1;
}

/* ------------------------------------------------------------------------
 * Mode table, raw descriptors and the 720-line feasibility census
 * --------------------------------------------------------------------- */

static uint16_t uvc_endpoint_psize(const struct usb_endpoint_descriptor *ep,
                                   uint8_t speed)
{
	uint16_t mps = ep->wMaxPacketSize & USB_MAXPACKETSIZE_MASK;
	uint8_t mult;

	if (speed == USB_SPEED_HIGH) {
		mult = (ep->wMaxPacketSize &
			USB_MAXPACKETSIZE_ADDITIONAL_TRANSCATION_MASK) >>
		       USB_MAXPACKETSIZE_ADDITIONAL_TRANSCATION_SHIFT;
	} else {
		/* Bits 12:11 are reserved below high speed; some devices leave
		 * them non-zero and a multiplier would overstate the endpoint. */
		mult = 0;
	}

	return (uint16_t)(mps * (uint16_t)(mult + 1U));
}

static uint16_t uvc_max_mps(const struct usbh_video *video)
{
	const struct usbh_interface *intf = &video->hport->config.intf[video->data_intf];
	uint16_t best = 0;

	for (uint8_t alt = 1; alt < intf->altsetting_num; alt++) {
		const struct usb_endpoint_descriptor *ep = &intf->altsetting[alt].ep[0].ep_desc;

		if ((ep->bLength == 0U) ||
		    (USB_GET_ENDPOINT_TYPE(ep->bmAttributes) != USB_ENDPOINT_TYPE_ISOCHRONOUS) ||
		    ((ep->bEndpointAddress & 0x80U) == 0U)) {
			continue;
		}
		uint16_t psize = uvc_endpoint_psize(ep, video->hport->speed);

		if (psize > best) {
			best = psize;
		}
	}
	return best;
}

static uint8_t uvc_pick_alt(const struct usbh_video *video, uint32_t want,
                            uint16_t *picked_psize)
{
	const struct usbh_interface *intf = &video->hport->config.intf[video->data_intf];
	uint16_t best = 0xffffU;
	uint8_t best_alt = 0;

	for (uint8_t alt = 1; alt < intf->altsetting_num; alt++) {
		const struct usb_endpoint_descriptor *ep = &intf->altsetting[alt].ep[0].ep_desc;
		uint16_t psize;

		if ((ep->bLength == 0U) ||
		    (USB_GET_ENDPOINT_TYPE(ep->bmAttributes) != USB_ENDPOINT_TYPE_ISOCHRONOUS) ||
		    ((ep->bEndpointAddress & 0x80U) == 0U)) {
			continue;
		}
		psize = uvc_endpoint_psize(ep, video->hport->speed);
		if ((psize >= want) && (psize < best)) {
			best = psize;
			best_alt = alt;
		}
	}

	if (picked_psize != 0) {
		*picked_psize = (best_alt != 0U) ? best : 0U;
	}
	return best_alt;
}

/* The raw VS frame descriptors carry the interval table the class's parsed
 * view does not keep (it keeps only dwDefaultFrameInterval).  Copy the
 * discrete intervals for one width/height into out[]; a continuous-interval
 * descriptor yields its default interval only. */
static unsigned uvc_raw_intervals(const struct usbh_video *video,
                                  uint16_t w, uint16_t h,
                                  uint32_t *out, unsigned max)
{
	const uint8_t *p = video->hport->raw_config_desc;
	uint32_t total = video->hport->config.config_desc.wTotalLength;
	uint32_t off = 0;
	unsigned n = 0;

	if (p == 0) {
		return 0;
	}

	while ((off + 2U) <= total) {
		uint8_t len = p[0];

		if (len < 2U) {
			break;
		}
		if ((len >= 3U) && (p[1] == VIDEO_CS_INTERFACE_DESCRIPTOR_TYPE) &&
		    (p[2] == VIDEO_VS_FRAME_MJPEG_DESCRIPTOR_SUBTYPE ||
		     p[2] == VIDEO_VS_FRAME_UNCOMPRESSED_DESCRIPTOR_SUBTYPE) &&
		    (len >= 26U)) {
			const struct video_cs_if_vs_frame_mjpeg_descriptor *d =
				(const struct video_cs_if_vs_frame_mjpeg_descriptor *)p;

			if ((d->wWidth == w) && (d->wHeight == h)) {
				if (d->bFrameIntervalType == 0U) {
					if (n < max) {
						out[n++] = d->dwDefaultFrameInterval;
					}
				} else {
					unsigned cnt = d->bFrameIntervalType;

					for (unsigned i = 0; (i < cnt) && (n < max); i++) {
						if ((26U + 4U * (i + 1U)) > len) {
							break;
						}
						out[n++] = d->dwFrameInterval[i];
					}
				}
			}
		}
		off += len;
		p += len;
	}
	return n;
}

void uvc_app_mode_dump(void)
{
	struct usbh_video *video = uvc_instance;
	const struct usbh_interface *intf;

	if (video == 0) {
		return;
	}

	for (uint8_t i = 0; i < video->num_of_formats; i++) {
		for (uint8_t j = 0; j < video->format[i].num_of_frames; j++) {
			LOG_ANCHOR("uvc: mode fmt=%u(%s) %ux%u def_ival=%u\n",
				   (unsigned)(i + 1U),
				   (video->format[i].format_type == USBH_VIDEO_FORMAT_MJPEG) ? "mjpeg" : "raw",
				   (unsigned)video->format[i].frame[j].wWidth,
				   (unsigned)video->format[i].frame[j].wHeight,
				   (unsigned)video->format[i].frame[j].dwDefaultFrameInterval);
		}
	}

	intf = &video->hport->config.intf[video->data_intf];
	for (uint8_t alt = 0; alt < intf->altsetting_num; alt++) {
		const struct usb_endpoint_descriptor *ep = &intf->altsetting[alt].ep[0].ep_desc;

		if (ep->bLength == 0U) {
			LOG_ANCHOR("uvc: alt=%u mps=0 ival=0 (no endpoint)\n",
				   (unsigned)alt);
			continue;
		}
		LOG_ANCHOR("uvc: alt=%u mps=%u ival=%u type=%u dir=%s\n",
			   (unsigned)alt,
			   (unsigned)uvc_endpoint_psize(ep, video->hport->speed),
			   (unsigned)ep->bInterval,
			   (unsigned)USB_GET_ENDPOINT_TYPE(ep->bmAttributes),
			   (ep->bEndpointAddress & 0x80U) ? "in" : "out");
	}

	/* The raw table: the class's parsed view above keeps one interval per
	 * frame; this is where the camera's slower intervals live. */
	{
		const uint8_t *p = video->hport->raw_config_desc;
		uint32_t total = video->hport->config.config_desc.wTotalLength;
		uint32_t off = 0;
		uint8_t cur_fmt = 0;

		while ((p != 0) && ((off + 2U) <= total)) {
			uint8_t len = p[0];

			if (len < 2U) {
				break;
			}
			if ((len >= 5U) && (p[1] == VIDEO_CS_INTERFACE_DESCRIPTOR_TYPE) &&
			    (p[2] == VIDEO_VS_FORMAT_MJPEG_DESCRIPTOR_SUBTYPE ||
			     p[2] == VIDEO_VS_FORMAT_UNCOMPRESSED_DESCRIPTOR_SUBTYPE)) {
				cur_fmt = p[3];
				LOG_ANCHOR("uvc: raw format idx=%u frames=%u type=%s\n",
					   (unsigned)cur_fmt, (unsigned)p[4],
					   (p[2] == VIDEO_VS_FORMAT_MJPEG_DESCRIPTOR_SUBTYPE) ? "mjpeg" : "raw");
			} else if ((len >= 26U) && (p[1] == VIDEO_CS_INTERFACE_DESCRIPTOR_TYPE) &&
				   (p[2] == VIDEO_VS_FRAME_MJPEG_DESCRIPTOR_SUBTYPE ||
				    p[2] == VIDEO_VS_FRAME_UNCOMPRESSED_DESCRIPTOR_SUBTYPE)) {
				const struct video_cs_if_vs_frame_mjpeg_descriptor *d =
					(const struct video_cs_if_vs_frame_mjpeg_descriptor *)p;
				unsigned cnt = d->bFrameIntervalType;

				LOG_ANCHOR("uvc: raw frame fmt=%u idx=%u %ux%u def=%u max=%u intervals=%u\n",
					   (unsigned)cur_fmt, (unsigned)d->bFrameIndex,
					   (unsigned)d->wWidth, (unsigned)d->wHeight,
					   (unsigned)d->dwDefaultFrameInterval,
					   (unsigned)d->dwMaxVideoFrameBufferSize,
					   cnt);
				if (cnt == 0U) {
					LOG_ANCHOR("uvc: raw ival fmt=%u idx=%u i=0 us=%u (continuous)\n",
						   (unsigned)cur_fmt, (unsigned)d->bFrameIndex,
						   (unsigned)d->dwDefaultFrameInterval);
				} else {
					for (unsigned i = 0; (i < cnt) && ((26U + 4U * (i + 1U)) <= len); i++) {
						LOG_ANCHOR("uvc: raw ival fmt=%u idx=%u i=%u us=%u\n",
							   (unsigned)cur_fmt,
							   (unsigned)d->bFrameIndex,
							   i, (unsigned)d->dwFrameInterval[i]);
					}
				}
			}
			off += len;
			p += len;
		}
	}
}

static void uvc_probe_one(struct usbh_video *video, uint8_t fmtidx, uint8_t frmidx,
                          uint32_t interval, uint16_t max_mps)
{
	uint32_t want;
	uint32_t fps_milli;
	int ret;

	ret = usbh_videostreaming_set_cur_probe(video, fmtidx, frmidx, interval);
	if (ret < 0) {
		LOG_ANCHOR("uvc: probe fmt=%u frm=%u ival=%u unavailable set_rc=%d\n",
			   (unsigned)fmtidx, (unsigned)frmidx,
			   (unsigned)interval, ret);
		return;
	}
	ret = usbh_videostreaming_get_cur_probe(video);
	if (ret < 0) {
		LOG_ANCHOR("uvc: probe fmt=%u frm=%u ival=%u unavailable get_rc=%d\n",
			   (unsigned)fmtidx, (unsigned)frmidx,
			   (unsigned)interval, ret);
		return;
	}

	want = video->probe.dwMaxPayloadTransferSize;
	fps_milli = (interval != 0U) ? (uint32_t)(10000000000ULL / interval) : 0U;
	LOG_ANCHOR("uvc: probe fmt=%u frm=%u ival=%u fps=%u.%03u want=%u frame=%u max_mps=%u fits=%s\n",
		   (unsigned)fmtidx, (unsigned)frmidx, (unsigned)interval,
		   fps_milli / 1000U, fps_milli % 1000U,
		   (unsigned)want,
		   (unsigned)video->probe.dwMaxVideoFrameSize,
		   (unsigned)max_mps,
		   ((want != 0U) && (want <= max_mps)) ? "yes" : "no");
}

void uvc_app_probe_census(void)
{
	struct usbh_video *video = uvc_instance;
	uint16_t max_mps;

	if (video == 0) {
		return;
	}
	max_mps = uvc_max_mps(video);
	{
		unsigned n720 = 0;

		for (uint8_t i = 0; i < video->num_of_formats; i++) {
			for (uint8_t j = 0; j < video->format[i].num_of_frames; j++) {
				if (video->format[i].frame[j].wHeight == 720U) {
					n720++;
				}
			}
		}
		LOG_ANCHOR("uvc: census max_mps=%u target=%ux%u 720_line_modes=%u\n",
			   (unsigned)max_mps,
			   (unsigned)CONFIG_UVC_TARGET_WIDTH,
			   (unsigned)CONFIG_UVC_TARGET_HEIGHT, n720);
	}

	for (uint8_t i = 0; i < video->num_of_formats; i++) {
		if (video->format[i].format_type != USBH_VIDEO_FORMAT_MJPEG) {
			continue;
		}
		for (uint8_t j = 0; j < video->format[i].num_of_frames; j++) {
			uint16_t w = video->format[i].frame[j].wWidth;
			uint16_t h = video->format[i].frame[j].wHeight;
			uint32_t intervals[8];
			unsigned n;

			if ((h != 720U) &&
			    !((w == CONFIG_UVC_TARGET_WIDTH) && (h == CONFIG_UVC_TARGET_HEIGHT)) &&
			    !((w == 640U) && (h == 480U)) &&
			    !((w == 800U) && (h == 600U))) {
				continue;
			}
			n = uvc_raw_intervals(video, w, h, intervals, 8U);
			if (n == 0U) {
				intervals[0] = video->format[i].frame[j].dwDefaultFrameInterval;
				n = 1U;
			}
			for (unsigned k = 0; k < n; k++) {
				uvc_probe_one(video, (uint8_t)(i + 1U),
					      (uint8_t)(j + 1U), intervals[k], max_mps);
			}
		}
	}
}

/* ------------------------------------------------------------------------
 * Open policy
 * --------------------------------------------------------------------- */

static int uvc_find_mode(const struct usbh_video *video, uint16_t w, uint16_t h,
                         uint8_t *fmtidx, uint8_t *frmidx)
{
	for (uint8_t i = 0; i < video->num_of_formats; i++) {
		if (video->format[i].format_type != USBH_VIDEO_FORMAT_MJPEG) {
			continue;
		}
		for (uint8_t j = 0; j < video->format[i].num_of_frames; j++) {
			if ((video->format[i].frame[j].wWidth == w) &&
			    (video->format[i].frame[j].wHeight == h)) {
				*fmtidx = (uint8_t)(i + 1U);
				*frmidx = (uint8_t)(j + 1U);
				return 0;
			}
		}
	}
	return -1;
}

int uvc_app_ensure_open(void)
{
	struct usbh_video *video = uvc_instance;
	uint8_t fmtidx = 0, frmidx = 0;
	uint16_t max_mps;
	uint16_t picked_mps = 0;
	uint8_t alt;
	uint32_t want;
	int ret;

	if (video == 0) {
		return -1;
	}
	if (video->is_opened) {
		return 0;
	}

	if (uvc_find_mode(video, CONFIG_UVC_TARGET_WIDTH, CONFIG_UVC_TARGET_HEIGHT,
			  &fmtidx, &frmidx) != 0) {
		LOG_ANCHOR("uvc: open FAIL mode %ux%u mjpeg not in the camera's tables\n",
			   (unsigned)CONFIG_UVC_TARGET_WIDTH,
			   (unsigned)CONFIG_UVC_TARGET_HEIGHT);
		return -2;
	}

	max_mps = uvc_max_mps(video);
	ret = usbh_videostreaming_get_cur_probe(video);
	if (ret < 0) {
		LOG_ANCHOR("uvc: open FAIL probe get rc=%d\n", ret);
		return ret;
	}
	ret = usbh_videostreaming_set_cur_probe(video, fmtidx, frmidx,
						video->format[fmtidx - 1U].frame[frmidx - 1U].dwDefaultFrameInterval);
	if (ret < 0) {
		LOG_ANCHOR("uvc: open FAIL probe set rc=%d\n", ret);
		return ret;
	}
	ret = usbh_videostreaming_get_cur_probe(video);
	if (ret < 0) {
		LOG_ANCHOR("uvc: open FAIL probe get-after-set rc=%d\n", ret);
		return ret;
	}

	want = video->probe.dwMaxPayloadTransferSize;
	if (want == 0U) {
		want = 1U;
	}

	alt = uvc_pick_alt(video, want, &picked_mps);
	if (alt == 0U) {
		LOG_ANCHOR("uvc: open FAIL want=%u above max_mps=%u (no full-speed alt carries it)\n",
			   (unsigned)want, (unsigned)max_mps);
		return -3;
	}

	ret = usbh_video_open(video, USBH_VIDEO_FORMAT_MJPEG,
			      CONFIG_UVC_TARGET_WIDTH, CONFIG_UVC_TARGET_HEIGHT, alt);
	if (ret < 0) {
		LOG_ANCHOR("uvc: open FAIL usbh_video_open rc=%d alt=%u\n",
			   ret, (unsigned)alt);
		return ret;
	}

	LOG_ANCHOR("uvc: open mjpeg %ux%u alt=%u fmt=%u frm=%u mps=%u dmp=%u ival=%u\n",
		   (unsigned)CONFIG_UVC_TARGET_WIDTH,
		   (unsigned)CONFIG_UVC_TARGET_HEIGHT,
		   (unsigned)alt,
		   (unsigned)video->probe.bFormatIndex,
		   (unsigned)video->probe.bFrameIndex,
		   (unsigned)video->isoin_mps,
		   (unsigned)video->probe.dwMaxPayloadTransferSize,
		   (unsigned)(video->isoin ? video->isoin->bInterval : 0U));
	return 0;
}
