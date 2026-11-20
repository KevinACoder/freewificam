/*
 * @file   uac_app.c
 * @brief  Application half of the first-party UAC1 microphone class on the
 *         STM32F429: board policy, descriptor census and the open sequence.
 *
 * The camera's audio function is one AudioControl interface followed by one
 * AudioStreaming interface.  The class driver parses both while the raw
 * configuration descriptor is alive; this file only reads the parsed view, so
 * the census cannot race the hub thread's free of that buffer.
 */

#include <stdint.h>
#include <stdio.h>

#include "os/cmsis2/cmsis2.h"

#include "log.h"

#include "usb_glue.h"

#include "usbh_core.h"
#include "usbh_uac.h"
#include "usbh_uac_stream.h"

#include "usb_app.h"
#include "uac_app.h"

#ifndef CONFIG_UAC_TARGET_RATE
#define CONFIG_UAC_TARGET_RATE 16000U
#endif

#define UAC_USB_BUS 0U
#define UAC_WAIT_STEPS 600U /* 100 ms each: 60 s */

static struct usbh_uac *uac_instance;

unsigned uac_app_bus(void)
{
	return UAC_USB_BUS;
}

struct usbh_uac *uac_app_class(void)
{
	return uac_instance;
}

void usbh_uac_run(struct usbh_uac *uac)
{
	uac_instance = uac;
	(void)usbh_uac_stream_attach(uac);

	LOG_ANCHOR("uac: connect vid=%04x pid=%04x bus=%u dev=%u ac=%u as=%u bcdADC=0x%04x\n",
		   (unsigned)uac->hport->device_desc.idVendor,
		   (unsigned)uac->hport->device_desc.idProduct,
		   (unsigned)uac->hport->bus->busid,
		   (unsigned)uac->hport->dev_addr,
		   (unsigned)uac->ctrl_intf,
		   (unsigned)uac->stream_intf,
		   (unsigned)uac->bcdADC);
}

void usbh_uac_stop(struct usbh_uac *uac)
{
	if (uac == uac_instance) {
		uac_instance = 0;
	}
	usbh_uac_stream_abort();

	LOG_ANCHOR("uac: disconnect bus=%u dev=%u\n",
		   (unsigned)uac->hport->bus->busid,
		   (unsigned)uac->hport->dev_addr);
}

int uac_app_ensure_up(void)
{
	struct usbh_uac *uac;
	unsigned spin;

	if (!usb_app_bus_is_up(UAC_USB_BUS)) {
		if (usb_app_bus_ensure(UAC_USB_BUS) != 0) {
			LOG_ERROR("uac: usb board bring-up failed bus=%u\n",
				  (unsigned)UAC_USB_BUS);
			return -1;
		}
	}
	LOG_ANCHOR("uac: host up bus=%u hcd=dwc2 base=0x%lx\n",
		   (unsigned)UAC_USB_BUS,
		   (unsigned long)usb_board_reg_base(UAC_USB_BUS));

	for (spin = 0; spin < UAC_WAIT_STEPS; spin++) {
		uac = uac_instance;
		if (uac != 0) {
			return 0;
		}
		osDelay(100U);
	}

	LOG_ERROR("uac: no audio class instance within %u s\n",
		  (unsigned)(UAC_WAIT_STEPS / 10U));
	return -1;
}

/* ------------------------------------------------------------------------
 * Descriptor census
 * --------------------------------------------------------------------- */

static void uac_raw_dump(const char *tag, const uint8_t *buf, uint8_t len)
{
	char line[3U * 16U + 1U];
	unsigned off;

	for (off = 0; off < len; off += 16U) {
		unsigned i, n = 0;

		for (i = 0; (i < 16U) && ((off + i) < len); i++) {
			n += (unsigned)snprintf(line + n, sizeof(line) - n, "%02x ",
						(unsigned)buf[off + i]);
		}
		line[n] = '\0';
		LOG_ANCHOR("uac: raw %s off=%02x %s\n", tag, off, line);
	}
}

void uac_app_descriptor_dump(void)
{
	struct usbh_uac *uac = uac_instance;
	char rates[64];
	size_t n = 0;

	if (uac == 0) {
		return;
	}

	rates[0] = '\0';
	if (uac->as_num_rates == 0U) {
		(void)snprintf(rates, sizeof(rates), "%u-%u",
			       (unsigned)uac->as_rate_min, (unsigned)uac->as_rate_max);
	} else {
		for (uint8_t i = 0; i < uac->as_num_rates; i++) {
			n += (size_t)snprintf(rates + n, sizeof(rates) - n, "%s%u",
					      (i == 0U) ? "" : ",",
					      (unsigned)uac->as_rates[i]);
		}
	}

	LOG_ANCHOR("uac: desc ac_intf=%u as_intf=%u bcdADC=0x%04x proto=0x%02x "
		   "ch=%u sub=%u bits=%u fmt=%u rates=%s ep=0x%02x mps=%u ival=%u dir=%s sfctrl=%u\n",
		   (unsigned)uac->ctrl_intf,
		   (unsigned)uac->stream_intf,
		   (unsigned)uac->bcdADC,
		   (unsigned)uac->protocol,
		   (unsigned)uac->channels,
		   (unsigned)uac->as_subframe_size,
		   (unsigned)uac->as_bit_resolution,
		   (unsigned)uac->as_format_tag,
		   rates,
		   (unsigned)uac->as_ep_addr,
		   (unsigned)uac->as_mps,
		   (unsigned)uac->as_interval,
		   uac->as_ep_in ? "in" : "out",
		   (unsigned)uac->as_sfctrl);

	LOG_ANCHOR("uac: terminal in=0x%04x in_id=%u out_id=%u src=%u "
		   "fu=%u mute_ch=%d vol_ch=%d\n",
		   (unsigned)uac->ac_input_terminal_type,
		   (unsigned)uac->ac_input_terminal_id,
		   (unsigned)uac->ac_output_terminal_id,
		   (unsigned)uac->ac_output_terminal_source,
		   (unsigned)uac->fu_id,
		   (uac->fu_mute_ch == 0xffU) ? -1 : (int)uac->fu_mute_ch,
		   (uac->fu_volume_ch == 0xffU) ? -1 : (int)uac->fu_volume_ch);

	uac_raw_dump("ac", uac->raw_ac, uac->raw_ac_len);
	uac_raw_dump("as", uac->raw_as, uac->raw_as_len);
}

/* ------------------------------------------------------------------------
 * Open policy
 * --------------------------------------------------------------------- */

static uint16_t uac_alt_psize(const struct usb_endpoint_descriptor *ep)
{
	return (uint16_t)(ep->wMaxPacketSize & USB_MAXPACKETSIZE_MASK);
}

int uac_app_ensure_open(void)
{
	struct usbh_uac *uac = uac_instance;
	const struct usbh_interface *intf;
	uint32_t rate = 0;
	uint32_t need;
	uint16_t best = 0xffffU;
	uint8_t alt = 0;
	const char *src = "target";
	const char *setcur;
	int ret;

	if ((uac == 0) || (uac->error != 0)) {
		return -1;
	}
	if (uac->is_opened) {
		return 0;
	}

	for (uint8_t i = 0; i < uac->as_num_rates; i++) {
		if (uac->as_rates[i] == (uint32_t)CONFIG_UAC_TARGET_RATE) {
			rate = (uint32_t)CONFIG_UAC_TARGET_RATE;
			break;
		}
	}
	if ((rate == 0U) && (uac->as_num_rates == 0U) && (uac->as_rate_min != 0U) &&
	    ((uint32_t)CONFIG_UAC_TARGET_RATE >= uac->as_rate_min) &&
	    ((uint32_t)CONFIG_UAC_TARGET_RATE <= uac->as_rate_max)) {
		rate = (uint32_t)CONFIG_UAC_TARGET_RATE;
	}
	if (rate == 0U) {
		if (uac->as_num_rates > 0U) {
			rate = uac->as_rates[0];
			src = "first";
		} else {
			rate = uac->as_rate_max;
			src = "range-max";
		}
	}
	if (rate == 0U) {
		LOG_ANCHOR("uac: open FAIL no usable sample rate\n");
		return -2;
	}

	need = ((rate * (uint32_t)uac->channels * (uint32_t)uac->as_subframe_size) + 999U) / 1000U;
	intf = &uac->hport->config.intf[uac->stream_intf];
	for (uint8_t a = 1; a < intf->altsetting_num; a++) {
		const struct usb_endpoint_descriptor *ep = &intf->altsetting[a].ep[0].ep_desc;
		uint16_t psize;

		if ((ep->bLength == 0U) ||
		    (USB_GET_ENDPOINT_TYPE(ep->bmAttributes) != USB_ENDPOINT_TYPE_ISOCHRONOUS) ||
		    ((ep->bEndpointAddress & 0x80U) == 0U)) {
			continue;
		}
		psize = uac_alt_psize(ep);
		if ((psize >= need) && (psize < best)) {
			best = psize;
			alt = a;
		}
	}
	if (alt == 0U) {
		LOG_ANCHOR("uac: open FAIL no alt setting carries %u B/ms (ch=%u sub=%u)\n",
			   (unsigned)need, (unsigned)uac->channels,
			   (unsigned)uac->as_subframe_size);
		return -3;
	}

	ret = usbh_uac_open(uac, rate, alt);
	if (ret < 0) {
		LOG_ANCHOR("uac: open FAIL usbh_uac_open rc=%d alt=%u\n",
			   ret, (unsigned)alt);
		return ret;
	}

	switch (uac->rate_set) {
		case 1:
			setcur = "ok";
			break;
		case 2:
			setcur = "skip";
			break;
		case 3:
			setcur = "stall";
			break;
		default:
			setcur = "?";
			break;
	}

	LOG_ANCHOR("uac: open rate=%u ch=%u sub=%u bits=%u alt=%u ep=0x%02x "
		   "mps=%u setcur=%s getcur=%u src=%s\n",
		   (unsigned)uac->rate,
		   (unsigned)uac->channels,
		   (unsigned)uac->as_subframe_size,
		   (unsigned)uac->as_bit_resolution,
		   (unsigned)alt,
		   (unsigned)uac->as_ep_addr,
		   (unsigned)uac->as_mps,
		   setcur,
		   (unsigned)uac->get_rate,
		   src);
	return 0;
}

int uac_app_unmute(void)
{
	struct usbh_uac *uac = uac_instance;
	int muted;

	if ((uac == 0) || (uac->error != 0)) {
		return -1;
	}
	muted = usbh_uac_get_mute(uac);
	if (muted < 0) {
		return -1;
	}
	if (muted == 0) {
		return 0;
	}
	if (usbh_uac_set_mute(uac, false) < 0) {
		return -1;
	}
	return 1;
}