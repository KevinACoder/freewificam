/*
 * @file   uac_mic.c
 * @brief  Case uac_mic: run the UAC1 microphone capture engine over a 10 s
 *         window and report packets, bytes, gaps, errors, ring overruns and
 *         the PCM's non-zero sample count.
 *
 * The gate is the ring's own counters plus the byte budget the negotiated
 * rate implies; the non-zero count separates "the device sent packets" from
 * "the device sent silence" (a muted feature unit is the usual cause, and the
 * case clears mute before the window when the unit exposes it).
 *
 * Anchors:
 *   uac: open rate=... alt=... setcur=... getcur=...
 *   uac: mic rate=... packets=N urbs=N bytes=N ms=10000 gaps=0 err=0
 *              empty=N overrun=0 nz=N peak=N mute=0|1|-1 unmuted=0|1
 *   uac: mic PASS | uac: mic FAIL <reason>
 */

#include <stdint.h>

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

#include "usbh_core.h"
#include "usbh_uac.h"
#include "usbh_uac_stream.h"

#include "uac_app.h"

#define UAC_MIC_WINDOW_MS 10000U
#define UAC_MIC_READ_MS   20U
#define UAC_MIC_BUF_BYTES 2048U

static uint8_t mic_buf[UAC_MIC_BUF_BYTES] __attribute__((aligned(4)));

static void mic_measure(const uint8_t *buf, unsigned len, uint32_t *nz, uint32_t *peak)
{
	const int16_t *samples = (const int16_t *)buf;
	unsigned count = len / 2U;

	for (unsigned i = 0; i < count; i++) {
		int32_t v = samples[i];
		int32_t a;

		if (v != 0) {
			(*nz)++;
		}
		a = (v < 0) ? -v : v;
		if ((uint32_t)a > *peak) {
			*peak = (uint32_t)a;
		}
	}
}

int case_uac_mic_run(void)
{
	struct usbh_uac *uac;
	struct uac_stream_stats st;
	uint32_t start_ms, elapsed_ms, expected;
	uint32_t nz = 0, peak = 0, read_bytes = 0;
	int muted;
	unsigned unmuted = 0;

	if (uac_app_ensure_up() != 0) {
		CASE_ANCHOR("uac: mic FAIL no audio class instance\n");
		return 1;
	}
	if (uac_app_ensure_open() != 0) {
		CASE_ANCHOR("uac: mic FAIL microphone not open\n");
		return 1;
	}
	uac = uac_app_class();
	if ((uac == 0) || !uac->is_opened) {
		CASE_ANCHOR("uac: mic FAIL class not open\n");
		return 1;
	}

	/* Clear a muted feature unit before the window, not inside it: a control
	 * transfer while the isochronous channel is live would mix the two
	 * transports for no reason. */
	muted = usbh_uac_get_mute(uac);
	if (muted == 1) {
		if (usbh_uac_set_mute(uac, false) == 0) {
			unmuted = 1;
			muted = 0;
		}
	}

	if (usbh_uac_stream_start() != 0) {
		CASE_ANCHOR("uac: mic FAIL usbh_uac_stream_start\n");
		return 1;
	}

	start_ms = osKernelGetTickCount();
	while ((osKernelGetTickCount() - start_ms) < UAC_MIC_WINDOW_MS) {
		int n = usbh_uac_stream_read(mic_buf, sizeof(mic_buf), UAC_MIC_READ_MS);

		if (n > 0) {
			read_bytes += (uint32_t)n;
			mic_measure(mic_buf, (unsigned)n, &nz, &peak);
		}
	}
	for (;;) {
		int n = usbh_uac_stream_read(mic_buf, sizeof(mic_buf), 0);

		if (n <= 0) {
			break;
		}
		read_bytes += (uint32_t)n;
		mic_measure(mic_buf, (unsigned)n, &nz, &peak);
	}
	elapsed_ms = osKernelGetTickCount() - start_ms;
	if (elapsed_ms == 0U) {
		elapsed_ms = 1U;
	}

	usbh_uac_stream_stop();
	usbh_uac_stream_stats(&st);

	expected = (st.rate * (uint32_t)st.channels * (uint32_t)st.subframe * elapsed_ms) / 1000U;

	CASE_ANCHOR("uac: mic rate=%u ch=%u bits=%u ep=0x%02x mps=%u packets=%u urbs=%u "
		    "bytes=%u read=%u ms=%u gaps=%u err=%u empty=%u overrun=%u "
		    "shutdown=%u gap_first=%u err_run=%u err_first=%u err_last=%u "
		    "err_mid=%u gap_max=%u "
		    "nz=%u peak=%u mute=%d unmuted=%u\n",
		    (unsigned)st.rate,
		    (unsigned)st.channels,
		    (unsigned)((uint32_t)st.subframe * 8U),
		    (unsigned)uac->as_ep_addr,
		    (unsigned)st.mps,
		    (unsigned)st.packets,
		    (unsigned)st.urbs,
		    (unsigned)st.bytes,
		    (unsigned)read_bytes,
		    (unsigned)elapsed_ms,
		    (unsigned)st.gaps,
		    (unsigned)st.err,
		    (unsigned)st.empty,
		    (unsigned)st.overrun,
		    (unsigned)st.shutdown,
		    (unsigned)st.gap_first,
		    (unsigned)st.err_max_run,
		    (unsigned)st.err_first,
		    (unsigned)st.err_last,
		    (unsigned)st.err_mid,
		    (unsigned)st.gap_max,
		    (unsigned)nz,
		    (unsigned)peak,
		    (muted == 1) ? 1 : ((muted == 0) ? 0 : -1),
		    unmuted);

	if (st.packets == 0U || st.bytes == 0U) {
		CASE_ANCHOR("uac: mic FAIL no PCM arrived\n");
		return 1;
	}
	if (st.gaps != 0U) {
		CASE_ANCHOR("uac: mic FAIL %u missed USB frames\n", (unsigned)st.gaps);
		return 1;
	}
	if (st.err != 0U) {
		CASE_ANCHOR("uac: mic FAIL %u transport errors\n", (unsigned)st.err);
		return 1;
	}
	if (st.overrun != 0U) {
		CASE_ANCHOR("uac: mic FAIL ring overrun %u bytes\n", (unsigned)st.overrun);
		return 1;
	}
	if ((expected == 0U) || (st.bytes < (expected * 80U) / 100U) ||
	    (st.bytes > (expected * 120U) / 100U)) {
		CASE_ANCHOR("uac: mic FAIL bytes=%u outside 80-120%% of %u\n",
			    (unsigned)st.bytes, (unsigned)expected);
		return 1;
	}
	if (nz == 0U) {
		CASE_ANCHOR("uac: mic FAIL PCM is all zero (mute=%d)\n", muted);
		return 1;
	}

	CASE_ANCHOR("uac: mic PASS\n");
	return 0;
}