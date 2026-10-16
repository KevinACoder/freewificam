/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    main.c
 * @brief   The SDIO adapter's own suite: the six seam operations against a
 *          scripted stack, asserted call by call.
 *
 * What is being tested is not that a card enumerates - that is the board's
 * question - but that this adapter turns a caller's request into the transfers
 * the host can actually perform. The stub records every entry point it is
 * called through, with the arguments, so each case asserts the shape of a
 * transfer: how many calls a request became, at which addresses, with which
 * flags, and in which order.
 *
 * The two rules worth naming, because they are the ones a plausible rewrite
 * would break silently:
 *
 *   - a two-byte write is two direct transfers and never one four-byte one.
 *     The host's engine takes word lengths, and widening the request would
 *     store two bytes the caller never gave.
 *   - a byte count past the extended command's nine-bit field is split into
 *     chunks, and every chunk is a whole number of words.
 *
 * Usage:
 *   fsl-sdmmc-adapter-test                 the whole battery
 *   fsl-sdmmc-adapter-test --fault expect-tamper
 *                                          the red control: one expectation is
 *                                          deliberately wrong, so the run must
 *                                          fail (the proof this battery can go
 *                                          red rather than merely print ok=1)
 */

/* The seam header leads the includes on purpose: it brings in the linuxkpi face,
 * whose MIN/MAX/ARRAY_SIZE definitions are unguarded, while the imported stack's
 * shadow of fsl_common.h guards its own. This order keeps one definition of
 * each; the adapter's own translation unit does the same. */
#include "lkpi_sdio_backend.h"

#include "fsl_stub.h"
#include "sdmmc_port.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int failures;
static int tamper;

static void check(const char *id, int ok, const char *fmt, ...)
{
	va_list ap;

	printf("adapter-test: %s ok=%d ", id, ok ? 1 : 0);
	va_start(ap, fmt);
	(void)vprintf(fmt, ap);
	va_end(ap);
	printf("\n");

	if (!ok) {
		failures++;
	}
}

/* start a case with a fresh record and a known read pattern */
static void begin(void)
{
	fsl_stub_reset();
	fsl_stub_set_rx_pattern(0x10U);
}

static int is_extended(const struct fsl_stub_call *c)
{
	return (c != NULL) && (c->op == FSL_STUB_OP_EXTENDED);
}

static int is_direct(const struct fsl_stub_call *c)
{
	return (c != NULL) && (c->op == FSL_STUB_OP_DIRECT);
}

/* --- the cases ------------------------------------------------------------ */

/* Before a backend is installed every entry is refused with the seam's own
 * no-device code, which is what a caller sees when the bring-up has not run. */
static void case_no_backend(void)
{
	struct lkpi_sdio_be_card_info info;

	(void)memset(&info, 0, sizeof(info));

	check("no-backend",
	      (lkpi_sdio_be_ready() == false) &&
		      (lkpi_sdio_be_card_init(&info) == -LKPI_SDIO_BE_ERR_NOCARD) &&
		      (lkpi_sdio_be_io_direct(0U, 0U, false, NULL) ==
		       -LKPI_SDIO_BE_ERR_NOCARD),
	      "ready=%d", (int)lkpi_sdio_be_ready());
}

static void case_card_init(void)
{
	struct lkpi_sdio_be_card_info info;
	const struct fsl_stub_call *c;
	const struct fsl_stub_call *ci;
	int st;

	(void)memset(&info, 0, sizeof(info));
	begin();

	st = lkpi_sdio_be_card_init(&info);
	ci = fsl_stub_call(0U);
	c = fsl_stub_call(1U);

	/* two calls, in order: the bring-up registers the card-interrupt upcall
	 * (non-NULL callback) before it starts the card, so the stack can hand
	 * the record to the host during SDIO_HostInit */
	check("card-init",
	      (st == 0) && (fsl_stub_count() == 2U) &&
		      (ci != NULL) && (ci->op == FSL_STUB_OP_CARD_INT_SET) &&
		      (ci->flags == 1U) &&
		      (c != NULL) && (c->op == FSL_STUB_OP_CARD_START) &&
		      (info.vendor == 0x024cU) && (info.device == 0xf179U) &&
		      (info.func_count == 1U) && (info.cccr_version == 2U) &&
		      (info.sdio_version == 3U) && (info.clock_hz == 50000000U) &&
		      (info.bus_width == 4U) && (info.fn0_max_block == 512U) &&
		      (info.multi_block) && (info.high_speed),
	      "st=%d vendor=%#x device=%#x funcs=%u cccr=%u clock=%u width=%u",
	      st, info.vendor, info.device, info.func_count, info.cccr_version,
	      info.clock_hz, info.bus_width);
}

static void case_card_init_failure(void)
{
	struct lkpi_sdio_be_card_info info;
	int st;

	(void)memset(&info, 0, sizeof(info));
	begin();
	fsl_stub_fail_with(kStatus_SDMMC_SDIO_InvalidCard);

	st = lkpi_sdio_be_card_init(&info);

	check("card-init-failure", st == -LKPI_SDIO_BE_ERR_NOTSDIO, "st=%d", st);
}

static void case_direct(void)
{
	const struct fsl_stub_call *r;
	const struct fsl_stub_call *w;
	uint8_t value = 0U;
	uint8_t out = 0x5aU;
	int a;
	int b;

	begin();

	a = lkpi_sdio_be_io_direct(1U, 0x109U, false, &value);
	b = lkpi_sdio_be_io_direct(1U, 0x109U, true, &out);

	r = fsl_stub_call(0U);
	w = fsl_stub_call(1U);

	check("direct",
	      (a == 0) && (b == 0) && (fsl_stub_count() == 2U) &&
		      is_direct(r) && !r->write && (r->func == 1U) && (r->addr == 0x109U) &&
		      is_direct(w) && w->write && (w->addr == 0x109U) &&
		      (value == (uint8_t)(0x10U + 0x09U)),
	      "a=%d b=%d value=%#x calls=%u", a, b, value, fsl_stub_count());
}

/* A request whose length is a whole number of words is one extended transfer. */
static void case_extended_write_aligned(void)
{
	uint8_t buf[8] = { 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U };
	const struct fsl_stub_call *c;
	int st;

	begin();

	st = lkpi_sdio_be_io_extended(1U, 0x100U, buf, 4U,
				      LKPI_SDIO_BE_F_WRITE | LKPI_SDIO_BE_F_INCR);
	c = fsl_stub_call(0U);

	check("extended-write-aligned",
	      (st == 0) && (fsl_stub_count() == 1U) && is_extended(c) && c->write &&
		      (c->count == 4U) && (c->addr == 0x100U) &&
		      ((c->flags & SDIO_EXTEND_CMD_OP_CODE_MASK) != 0U) &&
		      ((c->flags & SDIO_EXTEND_CMD_BLOCK_MODE_MASK) == 0U),
	      "st=%d calls=%u count=%u", st, fsl_stub_count(),
	      (c != NULL) ? c->count : 0U);
}

/* The rule the board round paid for: two bytes are two direct transfers, never
 * one four-byte extended one. */
static void case_extended_write_two_bytes(void)
{
	uint8_t two[2] = { 0xaaU, 0xbbU };
	const struct fsl_stub_call *c0;
	const struct fsl_stub_call *c1;
	unsigned int want = tamper ? 1U : 2U;
	int st;

	begin();

	st = lkpi_sdio_be_io_extended(1U, 0x10U, two, 2U,
				      LKPI_SDIO_BE_F_WRITE | LKPI_SDIO_BE_F_INCR);
	c0 = fsl_stub_call(0U);
	c1 = fsl_stub_call(1U);

	check("extended-write-two-bytes",
	      (st == 0) && (fsl_stub_count() == want) &&
		      is_direct(c0) && c0->write && (c0->addr == 0x10U) &&
		      is_direct(c1) && c1->write && (c1->addr == 0x11U),
	      "st=%d calls=%u (want %u)", st, fsl_stub_count(), want);
}

static void case_extended_read_three_bytes(void)
{
	uint8_t three[3] = { 0U, 0U, 0U };
	const struct fsl_stub_call *c0;
	const struct fsl_stub_call *c1;
	const struct fsl_stub_call *c2;
	int st;

	begin();

	st = lkpi_sdio_be_io_extended(1U, 0x05U, three, 3U, LKPI_SDIO_BE_F_INCR);
	c0 = fsl_stub_call(0U);
	c1 = fsl_stub_call(1U);
	c2 = fsl_stub_call(2U);

	check("extended-read-three-bytes",
	      (st == 0) && (fsl_stub_count() == 3U) &&
		      is_direct(c0) && !c0->write && (c0->addr == 0x05U) &&
		      is_direct(c1) && (c1->addr == 0x06U) &&
		      is_direct(c2) && (c2->addr == 0x07U),
	      "st=%d calls=%u", st, fsl_stub_count());
}

/* Six bytes: a four-byte prefix as one extended transfer, then the two trailing
 * bytes one at a time. */
static void case_extended_read_mixed(void)
{
	uint8_t six[6];
	const struct fsl_stub_call *c0;
	const struct fsl_stub_call *c1;
	const struct fsl_stub_call *c2;
	int st;

	(void)memset(six, 0, sizeof(six));
	begin();

	st = lkpi_sdio_be_io_extended(1U, 0x20U, six, 6U, LKPI_SDIO_BE_F_INCR);
	c0 = fsl_stub_call(0U);
	c1 = fsl_stub_call(1U);
	c2 = fsl_stub_call(2U);

	check("extended-read-mixed",
	      (st == 0) && (fsl_stub_count() == 3U) &&
		      is_extended(c0) && !c0->write && (c0->count == 4U) &&
		      (c0->addr == 0x20U) && is_direct(c1) && (c1->addr == 0x24U) &&
		      is_direct(c2) && (c2->addr == 0x25U) &&
		      (six[0] == 0x10U) && (six[3] == 0x13U),
	      "st=%d calls=%u prefix=%u", st, fsl_stub_count(),
	      (c0 != NULL) ? c0->count : 0U);
}

/* A byte count past the extended command's nine-bit field is split, and every
 * chunk is a whole number of words. */
static void case_extended_read_chunking(void)
{
	uint8_t big[600];
	const struct fsl_stub_call *c0;
	const struct fsl_stub_call *c1;
	int st;

	(void)memset(big, 0, sizeof(big));
	begin();

	st = lkpi_sdio_be_io_extended(1U, 0x00U, big, 600U, LKPI_SDIO_BE_F_INCR);
	c0 = fsl_stub_call(0U);
	c1 = fsl_stub_call(1U);

	check("extended-read-chunking",
	      (st == 0) && (fsl_stub_count() == 2U) &&
		      is_extended(c0) && (c0->count == 508U) && (c0->addr == 0x00U) &&
		      is_extended(c1) && (c1->count == 92U) && (c1->addr == 508U),
	      "st=%d calls=%u first=%u second=%u", st, fsl_stub_count(),
	      (c0 != NULL) ? c0->count : 0U, (c1 != NULL) ? c1->count : 0U);
}

/* Without the increment flag every transfer addresses the same place, and the
 * trailing direct transfers repeat it too. */
static void case_extended_fifo(void)
{
	uint8_t fifo[6];
	const struct fsl_stub_call *c0;
	const struct fsl_stub_call *c1;
	const struct fsl_stub_call *c2;
	int st;

	(void)memset(fifo, 0, sizeof(fifo));
	begin();

	st = lkpi_sdio_be_io_extended(1U, 0x40U, fifo, 6U, 0U);
	c0 = fsl_stub_call(0U);
	c1 = fsl_stub_call(1U);
	c2 = fsl_stub_call(2U);

	check("extended-fifo",
	      (st == 0) && (fsl_stub_count() == 3U) &&
		      is_extended(c0) && (c0->count == 4U) && (c0->addr == 0x40U) &&
		      ((c0->flags & SDIO_EXTEND_CMD_OP_CODE_MASK) == 0U) &&
		      is_direct(c1) && (c1->addr == 0x40U) &&
		      is_direct(c2) && (c2->addr == 0x40U),
	      "st=%d calls=%u", st, fsl_stub_count());
}

static uint8_t block_buf[4096] __attribute__((aligned(512)));

/* A block transfer is one extended transfer whose count is a block count and
 * whose size comes from the function's block-size register. */
static void case_block_transfer(void)
{
	const struct fsl_stub_call *c0;
	int st;

	begin();
	fsl_stub_set_block_size(1U, 512U);

	st = lkpi_sdio_be_io_extended(1U, 0x00U, block_buf, 2U,
				      LKPI_SDIO_BE_F_INCR | LKPI_SDIO_BE_F_BLOCK);
	c0 = fsl_stub_call(0U);

	check("block-transfer",
	      (st == 0) && (fsl_stub_count() == 1U) && is_extended(c0) &&
		      !c0->write && (c0->count == 2U) &&
		      ((c0->flags & SDIO_EXTEND_CMD_BLOCK_MODE_MASK) != 0U) &&
		      ((c0->flags & SDIO_EXTEND_CMD_OP_CODE_MASK) != 0U),
	      "st=%d calls=%u blocks=%u", st, fsl_stub_count(),
	      (c0 != NULL) ? c0->count : 0U);
}

/* A block size the engine could not take is refused before any transfer. */
static void case_block_bad_size(void)
{
	int st;

	begin();
	fsl_stub_set_block_size(1U, 510U);

	st = lkpi_sdio_be_io_extended(1U, 0x00U, block_buf, 2U,
				      LKPI_SDIO_BE_F_INCR | LKPI_SDIO_BE_F_BLOCK);

	check("block-bad-size",
	      (st == -LKPI_SDIO_BE_ERR_BLKSIZE) && (fsl_stub_count() == 0U),
	      "st=%d calls=%u", st, fsl_stub_count());
}

/* An unaligned block transfer larger than the stack's staging buffer is
 * refused: the general answer is a bounce buffer, and that belongs above. */
static void case_block_unaligned_too_big(void)
{
	int st;

	begin();
	fsl_stub_set_block_size(1U, 512U);
	fsl_stub_set_internal_buffer_size(4608U);

	st = lkpi_sdio_be_io_extended(1U, 0x00U, block_buf + 1U, 16U,
				      LKPI_SDIO_BE_F_INCR | LKPI_SDIO_BE_F_BLOCK);

	check("block-unaligned-too-big",
	      (st == -LKPI_SDIO_BE_ERR_INVAL) && (fsl_stub_count() == 0U),
	      "st=%d calls=%u", st, fsl_stub_count());
}

static void case_set_block_size(void)
{
	const struct fsl_stub_call *c0;
	int st;

	begin();

	st = lkpi_sdio_be_set_block_size(1U, 512U);
	c0 = fsl_stub_call(0U);

	check("set-block-size",
	      (st == 0) && (fsl_stub_count() == 1U) && (c0 != NULL) &&
		      (c0->op == FSL_STUB_OP_SET_BLOCK_SIZE) && (c0->func == 1U) &&
		      (c0->count == 512U),
	      "st=%d calls=%u", st, fsl_stub_count());
}

static void case_set_block_size_rejects(void)
{
	int a;
	int b;
	int c;

	begin();

	a = lkpi_sdio_be_set_block_size(1U, 513U);	/* not a word multiple */
	b = lkpi_sdio_be_set_block_size(1U, 0U);	/* nothing to set */
	c = lkpi_sdio_be_set_block_size(1U, 4096U);	/* past the card's ceiling */

	check("set-block-size-rejects",
	      (a == -LKPI_SDIO_BE_ERR_INVAL) && (b == -LKPI_SDIO_BE_ERR_INVAL) &&
		      (c == -LKPI_SDIO_BE_ERR_INVAL) && (fsl_stub_count() == 0U),
	      "a=%d b=%d c=%d calls=%u", a, b, c, fsl_stub_count());
}

static void case_irq_pending(void)
{
	const struct fsl_stub_call *c0;
	uint8_t pending = 0U;
	int st;

	begin();
	fsl_stub_set_pending_interrupt(0x02U);

	st = lkpi_sdio_be_irq_pending(&pending);
	c0 = fsl_stub_call(0U);

	check("irq-pending",
	      (st == 0) && (pending == 0x02U) && (fsl_stub_count() == 1U) &&
		      (c0 != NULL) && (c0->op == FSL_STUB_OP_IRQ_PENDING),
	      "st=%d pending=%#x calls=%u", st, pending, fsl_stub_count());
}

/* Every status the stack can answer with arrives as the seam code that means
 * the same thing. */
static void case_error_mapping(void)
{
	uint8_t value = 0U;
	int a;
	int b;
	int c;
	int d;

	begin();

	fsl_stub_fail_with(kStatus_SDMMC_TransferFailed);
	a = lkpi_sdio_be_io_direct(1U, 0x00U, false, &value);

	fsl_stub_fail_with(kStatus_Timeout);
	b = lkpi_sdio_be_io_direct(1U, 0x00U, false, &value);

	fsl_stub_fail_with(kStatus_SDMMC_HostNotSupport);
	c = lkpi_sdio_be_io_direct(1U, 0x00U, false, &value);

	fsl_stub_fail_with(kStatus_SDMMC_SDIO_ReadCISFail);
	d = lkpi_sdio_be_set_block_size(1U, 512U);

	check("error-mapping",
	      (a == -LKPI_SDIO_BE_ERR_XFER) && (b == -LKPI_SDIO_BE_ERR_TIMEOUT) &&
		      (c == -LKPI_SDIO_BE_ERR_HOST) && (d == -LKPI_SDIO_BE_ERR_CIS),
	      "xfer=%d timeout=%d host=%d cis=%d", a, b, c, d);
}

static void case_card_deinit(void)
{
	const struct fsl_stub_call *c0;
	const struct fsl_stub_call *c1;

	begin();

	lkpi_sdio_be_card_deinit();
	c0 = fsl_stub_call(0U);
	c1 = fsl_stub_call(1U);

	/* the interrupt line comes down before the card does */
	check("card-deinit",
	      (fsl_stub_count() == 2U) &&
		      (c0 != NULL) && (c0->op == FSL_STUB_OP_CARD_INT_ENABLE) &&
		      (c0->count == 0U) &&
		      (c1 != NULL) && (c1->op == FSL_STUB_OP_CARD_STOP),
	      "calls=%u", fsl_stub_count());
}

/* The card-interrupt mode seam: on asks the host glue to arm the line, off
 * asks it to mask; the backend maps 1:1. */
static void case_card_irq_mode(void)
{
	const struct fsl_stub_call *c0;
	const struct fsl_stub_call *c1;
	int st_on;
	int st_off;

	begin();

	st_on = lkpi_sdio_be_card_irq_mode(1U);
	st_off = lkpi_sdio_be_card_irq_mode(0U);
	c0 = fsl_stub_call(0U);
	c1 = fsl_stub_call(1U);

	check("card-irq-mode",
	      (st_on == 0) && (st_off == 0) && (fsl_stub_count() == 2U) &&
		      (c0 != NULL) && (c0->op == FSL_STUB_OP_CARD_INT_ENABLE) &&
		      (c0->count == 1U) &&
		      (c1 != NULL) && (c1->op == FSL_STUB_OP_CARD_INT_ENABLE) &&
		      (c1->count == 0U),
	      "st=%d/%d calls=%u", st_on, st_off, fsl_stub_count());
}

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--fault") == 0 && (i + 1) < argc &&
		    strcmp(argv[i + 1], "expect-tamper") == 0) {
			tamper = 1;
		}
	}

	/* the no-backend case has to run before anything is installed */
	case_no_backend();

	lkpi_sdio_fsl_install();
	check("install", lkpi_sdio_be_ready() ? 1 : 0, "ready=%d",
	      (int)lkpi_sdio_be_ready());

	case_card_init();
	case_card_init_failure();
	case_direct();
	case_extended_write_aligned();
	case_extended_write_two_bytes();
	case_extended_read_three_bytes();
	case_extended_read_mixed();
	case_extended_read_chunking();
	case_extended_fifo();
	case_block_transfer();
	case_block_bad_size();
	case_block_unaligned_too_big();
	case_set_block_size();
	case_set_block_size_rejects();
	case_irq_pending();
	case_card_irq_mode();
	case_error_mapping();
	case_card_deinit();

	if (failures == 0) {
		printf("adapter-test: PASS\n");
	} else {
		printf("adapter-test: FAIL failures=%d\n", failures);
	}

	return (failures == 0) ? 0 : 1;
}
