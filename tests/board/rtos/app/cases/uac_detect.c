/*
 * @file   uac_detect.c
 * @brief  Case uac_detect: identify the camera's UAC1 microphone and print
 *         its AudioControl/AudioStreaming descriptors.
 *
 * This case does not open or stream anything.  It is the descriptor census
 * the capture engine is built against: the AC/AS interface pair, bcdADC, the
 * input terminal type, channels, subframe/bit resolution, the sample-rate
 * table, and the streaming endpoint's direction, size and interval.  A
 * descriptor outside this line's subset (UAC2, an OUT endpoint, 8/24-bit PCM,
 * a non-1 ms interval) fails here with a named reason instead of failing
 * later in the transport.
 *
 * Anchors:
 *   uac: host up bus=0 hcd=dwc2 base=0x...
 *   uac: connect vid=046d pid=0990 bus=0 dev=N ac=2 as=3 bcdADC=0x0100
 *   uac: desc ac_intf=2 as_intf=3 bcdADC=0x0100 ...
 *   uac: terminal in=0x0201 ...
 *   uac: raw ac/as ...
 *   uac: detect PASS | uac: detect FAIL <reason>
 */

#include <stdint.h>

#include "cases.h"

#include "usbh_core.h"
#include "usbh_uac.h"

#include "uac_app.h"

#define UAC_CAMERA_VID 0x046dU

static int uac_rate_supported(const struct usbh_uac *uac, uint32_t rate)
{
	if (uac->as_num_rates == 0U) {
		return (rate >= uac->as_rate_min) && (rate <= uac->as_rate_max);
	}
	for (uint8_t i = 0; i < uac->as_num_rates; i++) {
		if (uac->as_rates[i] == rate) {
			return 1;
		}
	}
	return 0;
}

int case_uac_detect_run(void)
{
	struct usbh_uac *uac;

	if (uac_app_ensure_up() != 0) {
		CASE_ANCHOR("uac: detect FAIL no audio class instance\n");
		return 1;
	}
	uac = uac_app_class();
	if (uac == 0) {
		CASE_ANCHOR("uac: detect FAIL class pointer lost\n");
		return 1;
	}

	uac_app_descriptor_dump();

	if (uac->error != 0) {
		CASE_ANCHOR("uac: detect FAIL %s\n",
			    (uac->reason[0] != '\0') ? uac->reason : "descriptor census refused");
		return 1;
	}
	if (uac->hport->device_desc.idVendor != UAC_CAMERA_VID) {
		CASE_ANCHOR("uac: detect FAIL unexpected camera vid=%04x\n",
			    (unsigned)uac->hport->device_desc.idVendor);
		return 1;
	}
	if ((uac->bcdADC != 0x0100U) || (uac->protocol != 0x00U)) {
		CASE_ANCHOR("uac: detect FAIL audio class not UAC1 (bcdADC=0x%04x proto=0x%02x)\n",
			    (unsigned)uac->bcdADC, (unsigned)uac->protocol);
		return 1;
	}
	if (uac->ctrl_intf == uac->stream_intf) {
		CASE_ANCHOR("uac: detect FAIL control and streaming share interface %u\n",
			    (unsigned)uac->ctrl_intf);
		return 1;
	}
	if (!uac->as_ep_in || (uac->isoin == 0)) {
		CASE_ANCHOR("uac: detect FAIL no isochronous IN endpoint\n");
		return 1;
	}
	if ((uac->as_mps < 1U) || (uac->as_mps > 1023U)) {
		CASE_ANCHOR("uac: detect FAIL full-speed endpoint bound %u outside 1..1023\n",
			    (unsigned)uac->as_mps);
		return 1;
	}
	if (uac->as_interval != 1U) {
		CASE_ANCHOR("uac: detect FAIL endpoint interval %u, full speed needs 1\n",
			    (unsigned)uac->as_interval);
		return 1;
	}
	if ((uac->as_format_type != 0x01U) || (uac->as_format_tag != 0x0001U)) {
		CASE_ANCHOR("uac: detect FAIL format type=%u tag=0x%04x, want type I PCM\n",
			    (unsigned)uac->as_format_type,
			    (unsigned)uac->as_format_tag);
		return 1;
	}
	if ((uac->as_subframe_size != 2U) || (uac->as_bit_resolution != 16U)) {
		CASE_ANCHOR("uac: detect FAIL subframe=%u bits=%u, this line does 16-bit\n",
			    (unsigned)uac->as_subframe_size,
			    (unsigned)uac->as_bit_resolution);
		return 1;
	}
	if (uac->channels < 1U) {
		CASE_ANCHOR("uac: detect FAIL no channels\n");
		return 1;
	}
	if ((uac->ac_input_terminal_type & 0xff00U) != 0x0200U) {
		CASE_ANCHOR("uac: detect FAIL input terminal 0x%04x is not a microphone\n",
			    (unsigned)uac->ac_input_terminal_type);
		return 1;
	}
	if (!uac_rate_supported(uac, CONFIG_UAC_TARGET_RATE)) {
		CASE_ANCHOR("uac: detect FAIL target rate %u not in the device's table\n",
			    (unsigned)CONFIG_UAC_TARGET_RATE);
		return 1;
	}

	CASE_ANCHOR("uac: detect PASS\n");
	return 0;
}