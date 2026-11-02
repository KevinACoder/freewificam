/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtw88.c
 * @brief  Case rtw88: the RTL8821CS (SDIO 024c:c821) enumerates on the board's
 *         mmc1 socket, the vendored rtw88 driver probes it over the linuxkpi
 *         SDIO engine, downloads its firmware and registers its hw; the bridge
 *         then runs the driver's start chain.
 *
 * This is the W6-01 round's E1 gate.  The chain is the reference image's, in
 * the order that order matters:
 *
 *   1. the linuxkpi core services (the system workqueue every deferred driver
 *      path rides on; with no queue the work is silently dropped);
 *   2. the linuxkpi-side bridge (the driver's probe registers an hw, and the
 *      seam has to exist before that call lands);
 *   3. the firmware blob (the registry is the only loader: there is no
 *      filesystem, and a lookup miss is a named failure rather than a hang);
 *   4. the SDIO backend (the imported fsl_sdmmc stack over the ported MCI
 *      controller) and the engine's bring-up, which registers the drivers the
 *      image carries and then probes them - the driver's own probe runs its
 *      power sequence, firmware download and MAC/BB/RF init before it
 *      registers a wiphy;
 *   5. the driver's own start chain (`wlan_bridge_up()`), which is what turns
 *      the receive path on.  Without it the chip sits configured but delivers
 *      nothing, and a scan would find an empty band.
 *
 * The driver bytes are the vendor's.  This file only reports what the stack
 * says; a failure line names the layer that failed, so the next step is a
 * verdict rather than a guess.
 *
 * Anchors:
 *   rtw88: sdio up card=024c:c821 funcs=<n> cccr=<n> clock=<n> width=<n> hs=<n>
 *   rtw88: fw <the driver's own Firmware version line>
 *   rtw88: attach ok after <ms> ms mac=<addr>
 *   rtw88: start ok attached=1 rx=<n>
 *   rtw88: cmd53 read blkcnt=<n> bytes=<n> func=<n> addr=0x<addr> errors=<n>
 *   rtw88: PASS
 *
 * The block-read line is the round's first exit.  It is reported only when the
 * card actually offered a receive window (the driver's own receive interrupt
 * ran and read REG_SDIO_RX0_REQ_LEN non-zero), because a block read issued
 * against an empty RXFF is a premise failure rather than a transfer result
 * (runs/20261030-firefly-v2-sdio-mci-port/evidence/block-read-gap-20261102.md).
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linuxkpi_impl.h"
#include "linuxkpi_cfg80211.h"
#include "lkpi_sdio_backend.h"
#include "linuxkpi_sdio.h"
#include "skbuff_pool.h"
#include "wlan_bridge.h"

#include "fsl_sdio.h"
#include "sdmmc_port.h"

#include "mci.h"

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
int lkpi_fw_rtw8821c_register(void);
int wlan_seam_adapter_init(void);

#define RTW88_ATTACH_WAIT_MS	180000u
#define RTW88_ATTACH_POLL_MS	100u

/* How long the controller's per-command anchor stays open, and how many block
 * reads it waits for inside that. Both are the evidence window's bounds, not a
 * measurement: the transfer shape shows up in a handful of windows. */
#define RTW88_TRACE_WAIT_MS	3000u
#define RTW88_TRACE_POLL_MS	100u
#define RTW88_TRACE_TRANSFERS	24u

/* the card this image is for: the board's mmc1 socket carries this part */
#define RTW88_VID		0x024cu
#define RTW88_PID		0xc821u

/*
 * Console discipline.  The controller's own per-command trace is on by default
 * (`mci.c:307`), and the driver's probe issues thousands of commands; at
 * 115200 baud that alone would keep the console busy for the whole probe and
 * push the judged anchors out of the ring.  The probe runs with the trace off
 * and the round turns it back on only around the transfer it wants to read.
 */ 
static char rtw88_fw_line[160];
static unsigned int rtw88_lkpi_noise;
static unsigned int rtw88_noise_show;

/*
 * The driver's own lines, filtered.  What the round needs from them is the
 * firmware report (the C2H evidence) and anything that names a failure; the
 * rest is passed through but counted, because probe-time warnings repeat.
 */
static void
rtw88_sink(ulog_level_t level, const char *line)
{
	(void)level;

	if (line == NULL)
		return;
	if (strstr(line, "Firmware version") != NULL) {
		strncpy(rtw88_fw_line, line, sizeof(rtw88_fw_line) - 1u);
		rtw88_fw_line[sizeof(rtw88_fw_line) - 1u] = '\0';
		CASE_ANCHOR("rtw88: fw %s\n", line);
		return;
	}
	/*
	 * The probe's sdio helpers warn per failed register access; a healthy
	 * probe reaches some speculative ones (the driver probes features a chip
	 * may not have).  Count them and print the first few, so a real failure
	 * is visible without the repeat noise filling the ring.
	 */
	if (strstr(line, "sdio read") != NULL || strstr(line, "sdio write") != NULL ||
	    strstr(line, "failed to") != NULL || strstr(line, "Failed to") != NULL) {
		rtw88_lkpi_noise++;
		if (rtw88_noise_show < 12u) {
			rtw88_noise_show++;
			CASE_INFO("lkpi: %s\n", line);
		}
		return;
	}
	CASE_INFO("lkpi: %s\n", line);
}

static int
rtw88_attached(void)
{
	struct wlan_bridge_counters c;

	wlan_bridge_stats(&c);
	return c.attached != 0;
}

/*
 * The bus shape, as the engine and the host see it. `max_req` is the host's
 * per-transfer ceiling and `io_ext` the count of extended (multi-byte)
 * transfers: a reader that only ever issued byte-mode accesses would still
 * report zero errors, so the shape is reported beside the error count and the
 * judge reads all of it.
 */
static void
rtw88_bus_anchor(void)
{
	struct lkpi_sdio_bus_info bus;
	struct lkpi_sdio_stats sdio;

	lkpi_sdio_bus_info_get(&bus);
	lkpi_sdio_stats_get(&sdio);
	CASE_ANCHOR("rtw88: sdio bus clock=%u width=%u blk=%u/%u max_req=%u "
	    "io_ext=%u\n", (unsigned)bus.clock_hz, (unsigned)bus.bus_width,
	    (unsigned)bus.blk_size, (unsigned)bus.blk_max,
	    (unsigned)bus.max_req_size, (unsigned)sdio.io_extended);
}

/*
 * The engine's transfer account: how many card accesses were direct register
 * io and how many were extended transfers, and how many of either failed.
 *
 * The distinction matters for the round's verdict: `errors=0` alone would pass
 * a read that was split into byte-mode transfers, so the caller also reads
 * `io_ext` above and the controller's own event word below.
 */
static unsigned int
rtw88_sdio_stats_anchor(void)
{
	struct lkpi_sdio_stats stats;

	memset(&stats, 0, sizeof(stats));
	lkpi_sdio_stats_get(&stats);
	CASE_ANCHOR("rtw88: sdio stats cards=%u drivers=%u probes=%u "
	    "io_direct=%u io_ext=%u errors=%u\n", (unsigned)stats.cards,
	    (unsigned)stats.drivers, (unsigned)stats.probes,
	    (unsigned)stats.io_direct, (unsigned)stats.io_extended,
	    (unsigned)stats.errors);
	return stats.errors;
}

/*
 * The controller's own snapshot: the command counts and, more to the point,
 * the event word of the last transfer. DTO (bit 3) says the data half
 * completed and RXDR (bit 5) that the FIFO handed bytes over; an error there
 * is a transfer that ended without its data.
 *
 * Printed on both paths: a probe that fails after the card came up is
 * diagnosed from the same line a passing one is recorded with.
 */
static void
rtw88_mci_anchor(const struct mci_host *hc)
{
	struct mci_state st;

	if (hc == NULL)
		return;
	mci_state_get(hc, &st);
	CASE_ANCHOR("rtw88: mci transfers=%u errors=%u cmd52=%u cmd53=%u "
	    "data_stages=%u idmac_stages=%u fifo_stages=%u "
	    "last_cmd=%u last_err=%d data_ev=%08x cmd_ev=%08x dmac=%08x\n",
	    (unsigned)st.transfers, (unsigned)st.errors,
	    (unsigned)st.cmd52, (unsigned)st.cmd53,
	    (unsigned)st.data_stages, (unsigned)st.idmac_stages,
	    (unsigned)st.fifo_stages, (unsigned)st.last_cmd,
	    st.last_err, (unsigned)st.data_ev, (unsigned)st.cmd_ev,
	    (unsigned)st.dmac);
}

/* Everything the verdict is written from, on either path. */
static void
rtw88_report(const struct mci_host *hc)
{
	(void)rtw88_sdio_stats_anchor();
	rtw88_mci_anchor(hc);
}

/*
 * The block-read evidence window.
 *
 * The controller's per-command anchor is off by default (it would carry the
 * console instead of the run), so the round opens it for exactly the slice it
 * wants to read and closes it again: the receive path is live at this point,
 * and every window the card offers is one multi-block CMD53 - waiting for a
 * couple of dozen of them is enough to see the transfer shape, and it keeps
 * the scan that follows quiet.
 *
 * Bounded on both ends: `RTW88_TRACE_WAIT_MS` is the ceiling, and a card that
 * never offers anything leaves the trace on no longer than that.
 */
static void
rtw88_trace_window(const struct mci_host *hc)
{
	struct mci_state st;
	unsigned int waited = 0u;
	uint32_t base;

	if (hc == NULL)
		return;
	mci_state_get(hc, &st);
	base = st.cmd53;
	mci_trace_set(1);
	while (waited < RTW88_TRACE_WAIT_MS) {
		osDelay(RTW88_TRACE_POLL_MS);
		waited += RTW88_TRACE_POLL_MS;
		mci_state_get(hc, &st);
		if (st.cmd53 - base >= RTW88_TRACE_TRANSFERS)
			break;
	}
	mci_trace_set(0);
	CASE_INFO("rtw88: trace window %u ms, %u transfers\n",
	    (unsigned)waited, (unsigned)(st.cmd53 - base));
}

int
case_rtw88_run(void)
{
	struct lkpi_sdio_be_card_info info;
	struct wlan_bridge_counters counters;
	struct mci_host *hc;
	uint8_t mac[6];
	unsigned int waited = 0u;
	unsigned int errors;
	int st;

	lkpi_log_sink_register(rtw88_sink);
	lkpi_core_init();
	(void)wlan_seam_adapter_init();
	if (lkpi_fw_rtw8821c_register() != 0) {
		CASE_ANCHOR("rtw88: FAIL firmware blob registration\n");
		return 1;
	}

	/* --- the card on the bus ------------------------------------------ */
	if (!lkpi_sdio_be_ready())
		lkpi_sdio_fsl_install();
	memset(&info, 0, sizeof(info));
	st = lkpi_sdio_be_card_init(&info);
	if (st != 0) {
		CASE_ANCHOR("rtw88: FAIL sdio card init st=%d\n", st);
		return 1;
	}

	CASE_ANCHOR("rtw88: sdio up card=%04x:%04x funcs=%u cccr=%u clock=%u "
	    "width=%u hs=%u\n", (unsigned)info.vendor, (unsigned)info.device,
	    (unsigned)info.func_count, (unsigned)info.cccr_version,
	    (unsigned)info.clock_hz, (unsigned)info.bus_width,
	    (unsigned)info.high_speed);

	if (info.vendor != RTW88_VID || info.device != RTW88_PID) {
		CASE_ANCHOR("rtw88: FAIL card id %04x:%04x want %04x:%04x\n",
		    (unsigned)info.vendor, (unsigned)info.device,
		    (unsigned)RTW88_VID, (unsigned)RTW88_PID);
		return 1;
	}

	/*
	 * The probe's trace budget.  The controller's per-command anchor is on
	 * by default and the driver's power-on/firmware sequence is thousands of
	 * commands; off for the probe, back on for the round's own transfers.
	 * The controller instance only exists after the card is up, which is why
	 * this is here and not at the top.
	 */
	hc = sdmmc_port_mci_host();
	if (hc != NULL)
		mci_trace_set(0);

	/* --- the driver's registration, probe, firmware, MAC -------------- */
	lkpi_sdio_start();

	while (waited < RTW88_ATTACH_WAIT_MS && !rtw88_attached()) {
		osDelay(RTW88_ATTACH_POLL_MS);
		waited += RTW88_ATTACH_POLL_MS;
	}
	if (!rtw88_attached()) {
		CASE_ANCHOR("rtw88: FAIL no attach after %u ms "
		    "(power/firmware chain - read the lkpi lines above)\n",
		    (unsigned)waited);
		goto fail_stats;
	}

	if (wlan_bridge_hwaddr(mac) != 0) {
		CASE_ANCHOR("rtw88: FAIL hardware address unavailable\n");
		goto fail_stats;
	}
	CASE_ANCHOR("rtw88: attach ok after %u ms mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
	    (unsigned)waited, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	if (rtw88_fw_line[0] == '\0') {
		CASE_ANCHOR("rtw88: FAIL no firmware version line (probe did not "
		    "reach the firmware report)\n");
		goto fail_stats;
	}

	/* --- the receive path on ------------------------------------------ */
	if (wlan_bridge_up() != 0) {
		CASE_ANCHOR("rtw88: FAIL driver start chain refused\n");
		goto fail_stats;
	}
	wlan_bridge_stats(&counters);
	CASE_ANCHOR("rtw88: start ok attached=%u rx=%u\n",
	    (unsigned)counters.attached, counters.rx_frames);

	/* --- the round's transfer evidence --------------------------------- */
	rtw88_trace_window(hc);
	rtw88_bus_anchor();
	errors = rtw88_sdio_stats_anchor();
	rtw88_mci_anchor(hc);
	if (errors != 0u) {
		CASE_ANCHOR("rtw88: FAIL sdio errors=%u\n", errors);
		return 1;
	}
	{
		struct lkpi_skb_pool_stats skb;

		lkpi_skb_pool_stats(&skb);
		CASE_INFO("rtw88: pool skb=%u/%u data=%u/%u heap_live=%u "
		    "ovf=%u/%u fail=%u\n", skb.skb_used, skb.skb_high,
		    skb.data_used, skb.data_high, skb.dyalloc_live,
		    skb.slot_overflow, skb.data_overflow, skb.alloc_fail);
	}
	if (rtw88_lkpi_noise != 0u)
		CASE_INFO("rtw88: lkpi noise=%u lines\n", rtw88_lkpi_noise);

	CASE_ANCHOR("rtw88: PASS\n");
	return 0;

fail_stats:
	rtw88_report(hc);
	return 1;
}
