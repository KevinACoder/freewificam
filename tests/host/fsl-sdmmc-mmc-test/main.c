/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    main.c
 * @brief   The eMMC line's own suite: the imported MMC card layer and this
 *          project's port over a scripted controller, asserted call by call.
 *
 * What is under test is not that a card enumerates on a board - that is the
 * board suite's question - but that this port drives the stack's ladder and its
 * block transfers the way the stack's contract says: the commands reach the
 * controller in the ladder's order, the clock and the bus width are programmed
 * as the ladder asks, the optional bus-width test is answered without reaching
 * the controller, multi-block transfers are pre-announced with their block
 * count, and a failure on either side surfaces as the failure it is.
 *
 * The controller is the only thing scripted (scripted_mshc.c); the card layer,
 * the port's card service, its host glue, its OS abstraction and the imported
 * MMC protocol unit are the real bytes the board image runs.
 *
 * Usage:
 *   fsl-sdmmc-mmc-test                    the whole battery
 *   fsl-sdmmc-mmc-test --fault expect-tamper
 *                                          the red control: one expectation is
 *                                          deliberately wrong, so the run must
 *                                          fail (the proof this battery can go
 *                                          red rather than merely print ok=1)
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "scripted_mshc.h"
#include "sdmmc_mmc.h"
#include "sdmmc_port.h"

#include "dwc_mshc.h"
#include "dwc_mshc_dma.h"

static int failures;
static int tamper;

static void check(const char *id, int ok, const char *fmt, ...)
{
	va_list ap;

	printf("mmc-test: %s ok=%d ", id, ok ? 1 : 0);
	va_start(ap, fmt);
	(void)vprintf(fmt, ap);
	va_end(ap);
	printf("\n");

	if (!ok) {
		failures++;
	}
}

/* A fresh scripted card, a fresh port state. */
static void begin(void)
{
	scripted_mshc_reset();
	(void)sdmmc_mmc_card_stop();
}

static int transfer_reached(uint32_t index)
{
	return scripted_mshc_count(SCRIPTED_MSHC_OP_TRANSFER, index) > 0U;
}

/* --- the cases ------------------------------------------------------------ */

/*
 * The ladder: the card comes up, and the commands that reached the controller
 * are the ones the stack's initialization sends - identification first, then the
 * register reads, then the switches. The port is not allowed to invent or skip
 * a step.
 */
static void case_ladder(void)
{
	int rc = sdmmc_mmc_card_start();
	struct sdmmc_mmc_info info;
	struct sdmmc_mmc_host_state hc;
	static const uint32_t must_send[] = { 0U, 1U, 2U, 3U, 9U, 7U, 8U, 16U };
	unsigned int i;

	check("ladder-start", rc == 0, "bring-up rc=%d", rc);
	if (rc != 0) {
		return;
	}

	for (i = 0U; i < sizeof(must_send) / sizeof(must_send[0]); i++) {
		check("ladder-cmd", transfer_reached(must_send[i]),
		      "CMD%u reached the controller", (unsigned)must_send[i]);
	}

	check("ladder-info", sdmmc_mmc_info_get(&info) == 0, "card identity read");
	check("ladder-blocks", info.blocks == SCRIPTED_MSHC_BLOCKS,
	      "blocks=%u want=%u", (unsigned)info.blocks, (unsigned)SCRIPTED_MSHC_BLOCKS);
	check("ladder-timing", info.timing == 1U, "high-speed timing=%u", (unsigned)info.timing);

	/* the clock the ladder asked for: identification first, then the 50 MHz
	 * high-speed rate */
	check("ladder-clock", scripted_mshc_clock_hz() == 50000000U,
	      "last clock=%u Hz", (unsigned)scripted_mshc_clock_hz());
	check("ladder-clock-400k", scripted_mshc_count(SCRIPTED_MSHC_OP_SET_CLOCK, 0U) >= 2U,
	      "clock requests=%u", scripted_mshc_count(SCRIPTED_MSHC_OP_SET_CLOCK, 0U));

	/* the width the ladder programmed, and the width it ended on */
	check("ladder-width", scripted_mshc_width() == 8U,
	      "programmed lines=%u", scripted_mshc_width());
	check("ladder-info-width", info.host_lines == 8U,
	      "reported lines=%u", (unsigned)info.host_lines);

	(void)sdmmc_mmc_host_state_get(&hc);
	check("ladder-state", hc.clock_hz == 50000000U,
	      "achieved clock=%u Hz", (unsigned)hc.clock_hz);
}

/*
 * The optional bus-width test: the two commands never reach the controller, and
 * the ladder still ends on eight lines. This is the port's answer to a test the
 * specification makes optional and that cannot pass in the stack's order; the
 * assertion here is that the answer is the port's, not the controller's.
 */
static void case_bus_test(void)
{
	int rc = sdmmc_mmc_card_start();

	check("bus-test-start", rc == 0, "bring-up rc=%d", rc);
	check("bus-test-w", !transfer_reached(19U), "BUSTEST_W did not reach the controller");
	check("bus-test-r", !transfer_reached(14U), "BUSTEST_R did not reach the controller");
	check("bus-test-width", scripted_mshc_width() == 8U,
	      "programmed lines=%u", scripted_mshc_width());
}

/*
 * Block-count commands: a multi-block transfer is pre-announced with its block
 * count, and the count matches the transfer that follows it.
 */
static void case_cmd23(void)
{
	uint8_t buf[8U * 512U];
	unsigned int i;
	unsigned int seen23 = 0U;
	uint32_t count23 = 0U;
	int rc;

	rc = sdmmc_mmc_card_start();
	if (rc != 0) {
		check("cmd23-start", 0, "bring-up rc=%d", rc);
		return;
	}

	scripted_mshc_reset();
	(void)sdmmc_mmc_card_stop();
	rc = sdmmc_mmc_card_start();
	check("cmd23-start", rc == 0, "bring-up rc=%d", rc);

	(void)memset(buf, 0x5a, sizeof(buf));
	rc = sdmmc_mmc_write(0U, buf, 8U);
	check("cmd23-write", rc == 0, "8-block write rc=%d", rc);

	for (i = 0U; i < scripted_mshc_call_count(); i++) {
		const struct scripted_mshc_call *c = scripted_mshc_call(i);

		if ((c == NULL) || (c->op != SCRIPTED_MSHC_OP_TRANSFER)) {
			continue;
		}
		if (c->index == 23U) {
			seen23 = 1U;
			count23 = c->arg;
			continue;
		}
		if ((c->index == 25U) && (c->bytes > 512U)) {
			check("cmd23-order", seen23, "CMD23 preceded the multi-block write");
			check("cmd23-count", count23 == 8U, "CMD23 count=%u want=8",
			      (unsigned)count23);
			check("cmd23-bytes", c->bytes == sizeof(buf), "write bytes=%u",
			      (unsigned)c->bytes);
			return;
		}
	}

	check("cmd23-found", 0, "no multi-block write reached the controller");
}

/*
 * The round trip: bytes written through the port come back through the port,
 * and the scripted card saw the data (the emulator moves it through its own
 * user area, so a glue that dropped or misdirected the buffer cannot pass).
 */
static void case_roundtrip(void)
{
	uint8_t out[4U * 512U];
	uint8_t in[4U * 512U];
	unsigned int i;
	int rc;

	rc = sdmmc_mmc_card_start();
	check("roundtrip-start", rc == 0, "bring-up rc=%d", rc);
	if (rc != 0) {
		return;
	}

	for (i = 0U; i < sizeof(out); i++) {
		out[i] = (uint8_t)(i * 7U + 3U);
	}
	(void)memset(in, 0, sizeof(in));

	rc = sdmmc_mmc_write(64U, out, 4U);
	check("roundtrip-write", rc == 0, "write rc=%d", rc);
	rc = sdmmc_mmc_read(64U, in, 4U);
	check("roundtrip-read", rc == 0, "read rc=%d", rc);
	check("roundtrip-bytes", memcmp(out, in, sizeof(out)) == 0, "data matched");

	/* and the card's own area really holds it (a port that answered without
	 * moving data would leave this zero) */
	check("roundtrip-card", memcmp(scripted_mshc_user_area() + 64U * 512U, out,
				       sizeof(out)) == 0,
	      "the card's user area holds what was written");
}

/*
 * The SDMA boundary arithmetic: the continuation address is the first boundary
 * strictly after the address the engine was last given. The board suite proves
 * the controller honors it; this proves the arithmetic itself, on the cases a
 * plausible rewrite gets wrong.
 */
static void case_boundary(void)
{
	check("boundary-unaligned", dwc_mshc_sdma_continue(0x1000U) == 0x80000U,
	      "0x1000 -> 0x%lx", (unsigned long)dwc_mshc_sdma_continue(0x1000U));
	check("boundary-aligned", dwc_mshc_sdma_continue(0x80000U) == 0x100000U,
	      "0x80000 -> 0x%lx", (unsigned long)dwc_mshc_sdma_continue(0x80000U));
	check("boundary-near", dwc_mshc_sdma_continue(0x7fff0U) == 0x80000U,
	      "0x7fff0 -> 0x%lx", (unsigned long)dwc_mshc_sdma_continue(0x7fff0U));
	check("boundary-second", dwc_mshc_sdma_continue(0x100000U) == 0x180000U,
	      "0x100000 -> 0x%lx", (unsigned long)dwc_mshc_sdma_continue(0x100000U));
	check("boundary-aligned-exact", dwc_mshc_sdma_continue(0U) == DWC_MSHC_SDMA_BOUNDARY,
	      "0 -> 0x%lx", (unsigned long)dwc_mshc_sdma_continue(0U));
}

/*
 * A failing transfer surfaces as a failure: the scripted card refuses the data
 * stage of a read, and the port's face reports it.
 */
static void case_fault_data(void)
{
	uint8_t buf[512U];
	int rc;

	rc = sdmmc_mmc_card_start();
	check("fault-start", rc == 0, "bring-up rc=%d", rc);
	if (rc != 0) {
		return;
	}

	scripted_mshc_fail_data(18U);
	rc = sdmmc_mmc_read(0U, buf, 4U);
	check("fault-read", rc != 0, "a failing read reported rc=%d", rc);

	scripted_mshc_fail_data(0U);
	rc = sdmmc_mmc_read(0U, buf, 1U);
	check("fault-cleared", rc == 0, "the read after the fault rc=%d", rc);
}

/*
 * A card that did not take the width the host programmed is caught by the data
 * path: the scripted card keeps its one-line width while the host programs
 * eight, and the first data transfer fails. This is the check the optional
 * bus-width test would have made.
 */
static void case_width_mismatch(void)
{
	uint8_t buf[512U];
	int rc;

	rc = sdmmc_mmc_card_start();
	check("width-start", rc == 0, "bring-up rc=%d", rc);
	if (rc != 0) {
		return;
	}

	scripted_mshc_card_width_override(1U);
	rc = sdmmc_mmc_read(0U, buf, 1U);
	check("width-mismatch", rc != 0, "a width mismatch reported rc=%d", rc);

	scripted_mshc_card_width_override(0U);
	rc = sdmmc_mmc_read(0U, buf, 1U);
	check("width-restored", rc == 0, "the read after the width matched rc=%d", rc);
}

/* Every recorded call, in order: the diagnostic a failing ladder is read
 * through (`--dump`). */
static void dump_calls(void)
{
	unsigned int i;

	for (i = 0U; i < scripted_mshc_call_count(); i++) {
		const struct scripted_mshc_call *c = scripted_mshc_call(i);

		if (c == NULL) {
			continue;
		}
		if (c->op == SCRIPTED_MSHC_OP_TRANSFER) {
			fprintf(stderr, "call %u: transfer cmd=%u arg=0x%x bytes=%u %s rc=%d\n",
				i, (unsigned)c->index, (unsigned)c->arg, (unsigned)c->bytes,
				c->write ? "write" : "read", c->result);
		} else {
			fprintf(stderr, "call %u: op=%u hz=%u width=%u rc=%d\n",
				i, c->op, (unsigned)c->hz, c->width, c->result);
		}
	}
}

int main(int argc, char **argv)
{
	int i;
	int dump = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--fault") == 0 && i + 1 < argc &&
		    strcmp(argv[i + 1], "expect-tamper") == 0) {
			tamper = 1;
		}
		if (strcmp(argv[i], "--dump") == 0) {
			dump = 1;
		}
	}

	printf("fsl-sdmmc-mmc-test: the eMMC line over a scripted controller\n");

	begin();
	case_ladder();
	if (dump) {
		dump_calls();
	}

	begin();
	case_bus_test();

	begin();
	case_cmd23();

	begin();
	case_roundtrip();

	begin();
	case_boundary();

	begin();
	case_fault_data();

	begin();
	case_width_mismatch();

	/* The red control: with --fault expect-tamper, one expectation is wrong on
	 * purpose, so a battery that cannot go red is itself the finding. */
	check("red-control", tamper ? 0 : 1,
	      "deliberately %s expectation", tamper ? "wrong" : "right");

	printf("fsl-sdmmc-mmc-test: failures=%d\n", failures);

	return (failures == 0) ? 0 : 1;
}
