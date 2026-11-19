/*
 * @file   lkpi_sdio_fsl.c
 * @brief  The SDIO layer's backend over the imported SDIO stack: the seven seam
 *         operations mapped onto the card service, with the host's length
 *         discipline absorbed here.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 freewificam contributors
 *
 * The seam (components/linuxkpi/sdio/lkpi_sdio_backend.h) deliberately knows
 * nothing about this host. This file is where the two meet, and it owns exactly
 * two jobs:
 *
 *   1. Translation. Every seam operation becomes one call into the card
 *      service, and every status the imported stack returns becomes a code from
 *      the seam's own family. The family's values are the stack's own numbers,
 *      so the mapping is a relabelling and not a re-invention.
 *
 *   2. Length discipline. This host's data engine takes word addresses and word
 *      lengths, and the extended transfer's byte count is nine bits wide, so a
 *      caller's request is turned into the transfers the host can actually
 *      perform:
 *
 *        byte mode, count multiple of 4   one extended transfer per 508 bytes
 *        byte mode, count not a multiple   the aligned prefix as above, then
 *                                          one direct transfer per trailing byte
 *        block mode                        one extended transfer of count blocks
 *
 *      Nothing is ever over-transferred: a two-byte write is two direct
 *      transfers, never one four-byte one. The rule lives here and nowhere
 *      else, which is what lets a second backend ignore it completely.
 *
 * @design ports & adapters: the adapter side of the seam. The engine is the
 * adapter-neutral core; this file plus the card service under it are the
 * adapter. Shape reference for the layering: the USB line's adapter
 * (components/linuxkpi/rtos/usb/lkpi_usb_cherryusb.c) implements its seam the same way -
 * translation plus the backend's own awkwardness, nothing else.
 */

/*
 * Include order matters here and is deliberate: the seam header brings in the
 * linuxkpi face, which defines MIN/MAX/ARRAY_SIZE unconditionally, while the
 * imported stack's shadow of fsl_common.h defines the same three names behind
 * "if not already defined" guards. Seam first therefore keeps one definition of
 * each, and the imported units do not use any of the three in the first place.
 */
#include "lkpi_sdio_backend.h"
/* the engine's public face: the card-interrupt callback below wakes the pump
 * through it (the port calling the component's own API is this stack's normal
 * direction of dependency) */
#include "linuxkpi_sdio.h"

#include "fsl_common.h"
#include "fsl_sdmmc.h"

#include "sdmmc_port.h"
#include "sdmmc_lock_diag.h"

#ifdef CONFIG_LKPI_CENSUS
#include "lkpi_census.h"
#endif

/* The extended transfer's byte count is a nine-bit field, so the largest single
 * byte-mode transfer the host accepts is 511 bytes - and it must also be a
 * multiple of four for the engine, which makes 508 the useful chunk. */
#define SDMMC_FSL_BYTE_CHUNK_MAX	508U

/* --- status translation --------------------------------------------------- */

/*
 * The imported stack's status codes into the seam's family. The family's values
 * were chosen to be the stack's own numbers, so the named cases are identity
 * mappings; everything the family does not name falls back to the generic
 * failure, and the caller sees which one it was on the console line above it.
 */
static int sdmmc_fsl_err(status_t st)
{
	switch (st) {
	case kStatus_Success:
		return 0;
	case kStatus_Fail:
	case kStatus_OutOfRange:
		return -LKPI_SDIO_BE_ERR_FAIL;
	case kStatus_InvalidArgument:
	case kStatus_SDMMC_SDIO_InvalidArgument:
		return -LKPI_SDIO_BE_ERR_INVAL;
	case kStatus_Timeout:
		return -LKPI_SDIO_BE_ERR_TIMEOUT;
	case kStatus_SDMMC_TransferFailed:
		return -LKPI_SDIO_BE_ERR_XFER;
	case kStatus_SDMMC_SetCardBlockSizeFailed:
		return -LKPI_SDIO_BE_ERR_BLKSIZE;
	case kStatus_SDMMC_HostNotSupport:
		return -LKPI_SDIO_BE_ERR_HOST;
	case kStatus_SDMMC_CardNotSupport:
		return -LKPI_SDIO_BE_ERR_CARD;
	case kStatus_SDMMC_SDIO_ResponseError:
		return -LKPI_SDIO_BE_ERR_RESP;
	case kStatus_SDMMC_SDIO_ReadCISFail:
		return -LKPI_SDIO_BE_ERR_CIS;
	case kStatus_SDMMC_SDIO_InvalidCard:
		return -LKPI_SDIO_BE_ERR_NOTSDIO;
	case kStatus_SDMMC_HostNotReady:
	case kStatus_SDMMC_CardDetectFailed:
		return -LKPI_SDIO_BE_ERR_NOCARD;
	default:
		return -LKPI_SDIO_BE_ERR_FAIL;
	}
}

/* --- census --------------------------------------------------------------- */

/*
 * The census this backend contributes: what the engine's requests became at the
 * host's own limits (chunk counts, block sizes, bytes per direction) and how
 * long a request took end to end. The split counts are the splitter's arithmetic
 * made visible: a byte-mode request larger than the host's chunk ceiling shows
 * up as several ops, and any trailing direct transfer shows up in tail_ops at
 * one per byte - the host's word-length rule leaving the remainder.
 */
#ifdef CONFIG_LKPI_CENSUS
struct lkpi_fsl_census {
	uint32_t	ext_calls;
	uint32_t	blk_ops;
	uint32_t	byt_ops;
	uint32_t	byt_tail_ops;
	uint32_t	errs;
	uint64_t	rx_bytes;
	uint64_t	tx_bytes;
	struct lkpi_census_histo	lat;
};

static struct lkpi_fsl_census lkpi_fsl_census_state;

void
lkpi_fsl_census_reset(void)
{

	(void)memset(&lkpi_fsl_census_state, 0, sizeof(lkpi_fsl_census_state));
}

void
lkpi_fsl_census_print(void)
{
	struct lkpi_fsl_census *c = &lkpi_fsl_census_state;

	lkpi_printf(ULOG_INFO_LEVEL,
	    "perf census: A fsl ext=%u blk=%u byt=%u tail=%u errs=%u rx_bytes=%llu tx_bytes=%llu\n",
	    c->ext_calls, c->blk_ops, c->byt_ops, c->byt_tail_ops, c->errs,
	    c->rx_bytes, c->tx_bytes);
	lkpi_census_histo_print("perf census: A fsl_lat", &c->lat);
}
#endif	/* CONFIG_LKPI_CENSUS */

/* --- card bring-up -------------------------------------------------------- */

/* Defined in the interrupts section below; registered with the card here. */
static void lkpi_sdio_fsl_card_irq(void *arg);

static int lkpi_sdio_fsl_card_init(void *ctx, struct lkpi_sdio_be_card_info *info)
{
	sdio_card_t *card;
	status_t st;
	unsigned int i;

	(void)ctx;

	if (info == NULL) {
		return -LKPI_SDIO_BE_ERR_INVAL;
	}

	(void)memset(info, 0, sizeof(*info));

	/* Register the card-interrupt upcall before the bring-up runs: the
	 * stack reads the card's user parameter during SDIO_HostInit and hands
	 * the record to the host. The line itself stays masked until the
	 * engine asks for it through card_irq_mode - a boot that never asks is
	 * the polling boot, bit for bit. */
	sdmmc_card_int_set(lkpi_sdio_fsl_card_irq, NULL);

	st = sdmmc_card_start();
	if (st != kStatus_Success) {
		/* the raw status is what a bring-up failure is diagnosed from, so it
		 * goes out before the translation loses it */
		SDMMC_LOG("sdio: card bring-up failed (status=%d)\n", (int)st);
		return sdmmc_fsl_err(st);
	}

	card = sdmmc_card();
	if (card == NULL) {
		return -LKPI_SDIO_BE_ERR_NOCARD;
	}

	info->vendor = card->commonCIS.mID;
	info->device = card->commonCIS.mInfo;
	info->fn0_max_block = card->commonCIS.fn0MaxBlkSize;
	info->func_count = card->ioTotalNumber;
	info->cccr_version = card->cccrVersioin;
	info->sdio_version = card->sdioVersion;
	info->ocr = card->ocr;
	info->clock_hz = card->busClock_Hz;
	/* The controller's own ceilings, which the host glue recorded at init: the
	 * engine's transfer splitter bounds a byte-mode chunk and a block count by
	 * them, so a request the controller cannot perform in one transfer is split
	 * before it reaches this seam. */
	info->host_max_blk_size = card->host->maxBlockSize;
	info->host_max_blk_count = card->host->maxBlockCount;
	/* The controller's largest single request is this stack's staging buffer
	 * (config.maxTransSize, sdmmc_card.c:164), which is what the host glue
	 * derived its block count from in the first place. Reading it back from
	 * the host record would be a second derivation of the same number; the
	 * card's own figure is the one the stack will actually stage. */
	info->host_max_req_size = (u32)card->internalBufferSize;
	info->bus_width = (u8)card->host->currBusWidth;
	info->memory_present = card->memPresentFlag;
	info->high_speed = (card->currentTiming == kSD_TimingSDR25HighSpeedMode);
	info->multi_block = (card->cccrflags & (uint32_t)kSDIO_CCCRSupportMultiBlock) != 0U;

	/* The per-function block-size ceilings come from each function's CIS, which
	 * the enumeration reads for function 0 only; a function whose ceiling was
	 * never read keeps zero, and the stack then skips its ceiling check. */
	for (i = 0U; i < (unsigned int)LKPI_SDIO_BE_MAX_FUNCS; i++) {
		info->fn_max_block[i] = card->funcCIS[i].ioMaxBlockSize;
	}

	return 0;
}

static void lkpi_sdio_fsl_card_deinit(void *ctx)
{
	sdio_card_t *card = sdmmc_card();

	(void)ctx;

	/* the interrupt line comes down before the card does, so the ISR
	 * cannot run against a card being torn down */
	if (card != NULL) {
		FDWMMCHOST_CardIntEnable(card->host, false);
	}
	sdmmc_card_stop();
}

/* --- direct transfers ----------------------------------------------------- */

static int lkpi_sdio_fsl_io_direct(void *ctx, unsigned int func, unsigned int addr,
				   bool write, u8 *value)
{
	sdio_card_t *card = sdmmc_card();
	status_t st;

	(void)ctx;

	if ((card == NULL) || (value == NULL)) {
		return -LKPI_SDIO_BE_ERR_INVAL;
	}
	if (func > (unsigned int)LKPI_SDIO_BE_MAX_FUNCS) {
		return -LKPI_SDIO_BE_ERR_INVAL;
	}

	lkpi_sdio_site_set("fsl.c52");
	(void)lkpi_sdio_claim_check(0);
	{
		static unsigned int win_logs;

		if ((func == 1u) && (win_logs < 40u) &&
		    ((addr == 0x1000Au) || (addr == 0x1000Bu) ||
		     (addr == 0x1000Cu) || (addr == 0x1000Eu))) {
			win_logs++;
			lkpi_printf(ULOG_INFO_LEVEL,
			    "fsl: c52 %s %05x=%02x\n",
			    write ? "wr" : "rd", addr,
			    (unsigned int)*value);
		}
	}
	if (write) {
		st = SDIO_IO_Write_Direct(card, (sdio_func_num_t)func, addr, value, false);
	} else {
		st = SDIO_IO_Read_Direct(card, (sdio_func_num_t)func, addr, value);
	}
	lkpi_sdio_site_set("fsl.done");

	return sdmmc_fsl_err(st);
}

/* --- extended transfers --------------------------------------------------- */

/* Byte mode: the caller's byte count, delivered as word-multiple transfers with
 * the remainder as single-byte direct transfers. */
static int sdmmc_fsl_byte_transfer(sdio_card_t *card, unsigned int func,
				   unsigned int addr, uint8_t *buf,
				   unsigned int count, bool write, bool incr)
{
	uint32_t flags = incr ? (uint32_t)SDIO_EXTEND_CMD_OP_CODE_MASK : 0U;
	unsigned int done = 0U;
	unsigned int full = count & ~3U;

	while (full > 0U) {
		unsigned int n = (full > SDMMC_FSL_BYTE_CHUNK_MAX) ?
					 SDMMC_FSL_BYTE_CHUNK_MAX : full;
		status_t st;

		if (write) {
			st = SDIO_IO_Write_Extended(card, (sdio_func_num_t)func, addr,
						    buf + done, n, flags);
		} else {
			st = SDIO_IO_Read_Extended(card, (sdio_func_num_t)func, addr,
						   buf + done, n, flags);
		}
		if (st != kStatus_Success) {
			return sdmmc_fsl_err(st);
		}

		done += n;
		full -= n;
#ifdef CONFIG_LKPI_CENSUS
		lkpi_fsl_census_state.byt_ops++;
		if (write)
			lkpi_fsl_census_state.tx_bytes += n;
		else
			lkpi_fsl_census_state.rx_bytes += n;
#endif
		if (incr) {
			addr += n;
		}
	}

	/* The remainder. A two-byte request is exactly these two transfers - the
	 * host would otherwise be asked to move four bytes and would write two
	 * bytes it was never given. */
	for (; done < count; done++) {
		status_t st;

		if (write) {
			st = SDIO_IO_Write_Direct(card, (sdio_func_num_t)func, addr,
						  &buf[done], false);
		} else {
			st = SDIO_IO_Read_Direct(card, (sdio_func_num_t)func, addr,
						 &buf[done]);
		}
		if (st != kStatus_Success) {
			return sdmmc_fsl_err(st);
		}
#ifdef CONFIG_LKPI_CENSUS
		lkpi_fsl_census_state.byt_tail_ops++;
		if (write)
			lkpi_fsl_census_state.tx_bytes += 1U;
		else
			lkpi_fsl_census_state.rx_bytes += 1U;
#endif

		if (incr) {
			addr += 1U;
		}
	}

	return 0;
}

/*
 * The AP6181 f1 write-shape probe (CONFIG_F429_AP6181_F1_PROBE).
 *
 * The brcmfmac firmware download's first f1 block write is the one transfer
 * the whole C3 gate turns on: after it the card answers every f1 CMD52/53 with
 * R5 0x2800.  The arms already spent on the second command, the timing and the
 * R5 flags did not move it, so this probe asks the card itself what it accepts,
 * at the exact point and in the exact chip state the driver reaches:
 *
 *   S0  the chip's core wrappers (ARM CM3 and SOCRAM, at 0x18103000/0x18104000
 *       plus the 0x408/0x800 AI offsets) - is the ARM held in reset and the
 *       RAM core out of it, which is what brcmf_chip_cm3_set_passive() is
 *       supposed to leave behind;
 *   S1  the card's own f1 state: FBR block size, CCCR, CHIPCLKCSR;
 *   S2  a staircase of write shapes at the download's own window/address, in
 *       the order the question needs: 1 / 2 / 8 / 32 blocks of 64 B at
 *       0x8000, then the second chunk's address 0x8800, then the byte-mode
 *       shapes (4 / 8 / 48 / 496 B) that a fallback would use.
 *
 * Every stage is written with a known pattern and read back with single-block
 * CMD53 reads (the one read shape already proven on this card), and after each
 * one the CHIPCLKCSR read says whether f1 still answers.  A stage that writes
 * but does not read back is recorded as `bad=N` and the staircase continues
 * (the card still answers; the mismatch is a finding about that shape, not a
 * reason to skip the shapes behind it).  Only a failed write or a dead f1 stops
 * the staircase; the abort and card-reset writes then say whether the wedged
 * state can be cleared without a power cycle.
 *
 * The probe writes only the first 2 KB of the firmware area (the real download
 * overwrites it) and never edits the imported driver tree.
 */
#if defined(CONFIG_F429_AP6181_F1_PROBE) && CONFIG_F429_AP6181_F1_PROBE

#define F1P_BLOCK	((uint32_t)SDIO_EXTEND_CMD_BLOCK_MODE_MASK | \
			 (uint32_t)SDIO_EXTEND_CMD_OP_CODE_MASK)
#define F1P_INCR	((uint32_t)SDIO_EXTEND_CMD_OP_CODE_MASK)
#define F1P_CLKCSR	0x1000Eu
#define F1P_DEVCTL	0x10009u
#define F1P_SBADDRLOW	0x1000Au
/* The AI wrapper window: the vendor's core base plus WRAPPER_REGISTER_OFFSET
 * (0x100000), 32 KiB window base 0x18100000; the wrapper registers themselves
 * are byte-wide and read with CMD52 at the window offset (no 0x8000 flag). */
#define F1P_CORE_WIN	0x18100000u
#define F1P_STAGES	9u

static uint8_t f1p_pat[2048];
static uint8_t f1p_rd[2048];
static bool f1p_done;


/* The window registers hold the backplane base's bits [31:8], low byte first. */
static int
f1p_win_read(sdio_card_t *card, uint32_t *v)
{
	uint8_t b[3];
	uint32_t i;

	*v = 0u;
	for (i = 0u; i < 3u; i++) {
		status_t st = SDIO_IO_Read_Direct(card, kSDIO_FunctionNum1,
						  F1P_SBADDRLOW + i, &b[i]);

		if (st != kStatus_Success)
			return -1;
		*v |= (uint32_t)b[i] << (8u * i);
	}
	return 0;
}

static int
f1p_win_write(sdio_card_t *card, uint32_t v)
{
	uint32_t i;

	for (i = 0u; i < 3u; i++) {
		uint8_t b = (uint8_t)((v >> (8u * i)) & 0xFFu);

		if (SDIO_IO_Write_Direct(card, kSDIO_FunctionNum1,
					 F1P_SBADDRLOW + i, &b, false) !=
		    kStatus_Success)
			return -1;
	}
	return 0;
}

static int
f1p_alive(sdio_card_t *card, uint8_t *clk)
{
	if (SDIO_IO_Read_Direct(card, kSDIO_FunctionNum1, F1P_CLKCSR, clk) !=
	    kStatus_Success)
		return -1;
	return 0;
}

/* S0: the two core wrappers, read behind the driver's cached window and put
 * back exactly.  The AI wrapper registers are byte-wide in the backplane (the
 * module vendor reads and writes them with single-byte accesses, no 0x8000
 * flag), so they are read with CMD52 at the window offset, not with a 32-bit
 * CMD53.  A failed window round-trip is itself a finding (the driver's next
 * access would use a window it did not set), so it is reported. */
static void
f1p_core_state(sdio_card_t *card)
{
	uint32_t saved = 0u, back = 0u;
	uint8_t arm_ioctl = 0u, arm_reset = 0u;
	uint8_t soc_ioctl = 0u, soc_reset = 0u;
	int ok = 0;
	int restored = 0;

	if (f1p_win_read(card, &saved) == 0 &&
	    f1p_win_write(card, F1P_CORE_WIN) == 0) {
		if (SDIO_IO_Read_Direct(card, kSDIO_FunctionNum1, 0x3408u,
					&arm_ioctl) == kStatus_Success &&
		    SDIO_IO_Read_Direct(card, kSDIO_FunctionNum1, 0x3800u,
					&arm_reset) == kStatus_Success &&
		    SDIO_IO_Read_Direct(card, kSDIO_FunctionNum1, 0x4408u,
					&soc_ioctl) == kStatus_Success &&
		    SDIO_IO_Read_Direct(card, kSDIO_FunctionNum1, 0x4800u,
					&soc_reset) == kStatus_Success)
			ok = 1;
	}
	if (f1p_win_write(card, saved) == 0 && f1p_win_read(card, &back) == 0 &&
	    back == saved)
		restored = 1;
	lkpi_printf(ULOG_INFO_LEVEL,
	    "f1probe: core arm=%02x/%02x soc=%02x/%02x ok=%d win=%d\n",
	    (unsigned)arm_ioctl, (unsigned)arm_reset,
	    (unsigned)soc_ioctl, (unsigned)soc_reset, ok, restored);
}

/* S1: the card's own idea of the function's block size and state. */
static void
f1p_card_state(sdio_card_t *card)
{
	uint8_t fbr[2] = {0u, 0u};
	uint8_t fbr0[2] = {0u, 0u};
	uint8_t cccr[6] = {0u};
	uint8_t clk = 0u, devctl = 0u;
	uint8_t set = 0u;
	uint32_t i;

	for (i = 0u; i < 2u; i++) {
		(void)SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0,
					  0x110u + i, &fbr[i]);
		(void)SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0,
					  0x010u + i, &fbr0[i]);
	}
	for (i = 0u; i < 6u; i++)
		(void)SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0, i, &cccr[i]);
	/*
	 * The module vendor's bring-up writes the card's function-0 block size
	 * register to 64 before the download and waits until it reads back 64.
	 * This port's engine sets F1 and F2 but never F0, so whether the card's
	 * common block size is still the CIS's 32 is an open question for the
	 * block-mode data phase - measure the write here instead of assuming.
	 */
	set = 64u;
	(void)SDIO_IO_Write_Direct(card, kSDIO_FunctionNum0, 0x010u, &set,
				   false);
	set = 0u;
	(void)SDIO_IO_Write_Direct(card, kSDIO_FunctionNum0, 0x011u, &set,
				   false);
	for (i = 0u; i < 2u; i++)
		(void)SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0,
					  0x010u + i, &fbr0[i]);
	for (i = 0u; i < 2u; i++)
		(void)SDIO_IO_Read_Direct(card, kSDIO_FunctionNum0,
					  0x110u + i, &fbr[i]);
	(void)f1p_alive(card, &clk);
	(void)SDIO_IO_Read_Direct(card, kSDIO_FunctionNum1, F1P_DEVCTL,
				  &devctl);
	lkpi_printf(ULOG_INFO_LEVEL,
	    "f1probe: card fbr1=%02x%02x fbr0=%02x%02x "
	    "cccr=%02x%02x%02x%02x%02x%02x clk=%02x devctl=%02x\n",
	    (unsigned)fbr[1], (unsigned)fbr[0],
	    (unsigned)fbr0[1], (unsigned)fbr0[0],
	    (unsigned)cccr[0], (unsigned)cccr[1], (unsigned)cccr[2],
	    (unsigned)cccr[3], (unsigned)cccr[4], (unsigned)cccr[5],
	    (unsigned)clk, (unsigned)devctl);
}

static int
f1p_write(sdio_card_t *card, uint32_t addr, uint32_t bytes, uint32_t blocks,
	  uint32_t blksz)
{
	status_t st;

	if (blocks != 0u) {
		st = SDIO_IO_Write_Extended(card, kSDIO_FunctionNum1, addr,
					    f1p_pat, blocks, F1P_BLOCK);
	} else {
		st = SDIO_IO_Write_Extended(card, kSDIO_FunctionNum1, addr,
					    f1p_pat, bytes, F1P_INCR);
	}
	(void)blksz;
	return (st == kStatus_Success) ? 0 : -1;
}

/* Read back with single 64-byte block reads: the one read shape this card has
 * already answered on two cold boots, so a read failure cannot be mistaken
 * for the write's verdict. */
static int
f1p_readback(sdio_card_t *card, uint32_t addr, uint32_t bytes)
{
	uint32_t total = (bytes + 63u) & ~(uint32_t)63u;
	uint32_t off;

	for (off = 0u; off < total; off += 64u) {
		status_t st = SDIO_IO_Read_Extended(card, kSDIO_FunctionNum1,
						    addr + off, f1p_rd + off,
						    1u, F1P_BLOCK);

		if (st != kStatus_Success)
			return -1;
	}
	return 0;
}

static uint32_t
f1p_bad(uint32_t bytes)
{
	uint32_t i;
	uint32_t bad = 0u;

	for (i = 0u; i < bytes; i++) {
		if (f1p_rd[i] != f1p_pat[i])
			bad++;
	}
	return bad;
}

static int
f1p_alive_value(sdio_card_t *card)
{
	uint8_t clk = 0u;

	return (f1p_alive(card, &clk) == 0) ? 1 : 0;
}

/*
 * Clear a wedged f1 after a failed shape.  The first probe boot showed both
 * CCCR writes the card accepts: the per-function I/O abort (CCCR 0x06 = 1) and
 * the card reset (0x08).  The abort alone is tried first because it leaves the
 * function's block-size programming alone; only when it does not bring f1
 * back does the card reset run, followed by re-enabling f1 and its block size.
 */
static void
f1p_recover(sdio_card_t *card, uint32_t blk)
{
	int abort_ok = 0, abort_alive = 0, reset_ok = 0, reset_alive = 0;

	if (f1p_alive_value(card) != 0)
		return;
	abort_ok = (SDIO_AbortIO(card, kSDIO_FunctionNum1) ==
		    kStatus_Success) ? 1 : 0;
	abort_alive = f1p_alive_value(card);
	if (abort_alive == 0) {
		reset_ok = (SDIO_CardReset(card) == kStatus_Success) ? 1 : 0;
		if (reset_ok != 0) {
			(void)SDIO_EnableIO(card, kSDIO_FunctionNum1, true);
			(void)SDIO_SetBlockSize(card, kSDIO_FunctionNum1, blk);
		}
		reset_alive = f1p_alive_value(card);
	}
	lkpi_printf(ULOG_INFO_LEVEL,
	    "f1probe: recover abort=%d/%d cardreset=%d/%d\n",
	    abort_ok, abort_alive, reset_ok, reset_alive);
}

/*
 * One block-mode write shape: program the card's f1 block size and the
 * controller's SDIOEN mode for it, write a known pattern, read it back with
 * single-block reads, and report.  A dead f1 is recovered so the next shape
 * is measured on a live card.
 */
static void
f1p_block_case(sdio_card_t *card, uint32_t addr, uint32_t bytes, uint32_t blk,
	       bool sdioen, uint32_t tag)
{
	uint32_t blocks = (blk != 0u) ? (bytes / blk) : 1u;
	uint32_t bad = 0xFFFFFFFFu;
	uint32_t i;
	uint8_t clk = 0u;
	int wr;
	int alive;

	(void)SDIO_SetBlockSize(card, kSDIO_FunctionNum1, blk);
	sdmmc_port_block_write_sdioen(sdioen);
	for (i = 0u; i < bytes; i++)
		f1p_pat[i] = (uint8_t)(0x5Au ^ (uint8_t)tag ^ (uint8_t)i);
	wr = f1p_write(card, addr, bytes, blocks, blk);
	if ((wr == 0) && (f1p_readback(card, addr, bytes) == 0))
		bad = f1p_bad(bytes);
	alive = (f1p_alive(card, &clk) == 0) ? 1 : 0;
	lkpi_printf(ULOG_INFO_LEVEL,
	    "f1probe: case%u blk=%u sdioen=%d addr=%05x n=%u wr=%d bad=%d "
	    "alive=%d clk=%02x\n",
	    (unsigned)tag, (unsigned)blk, sdioen ? 1 : 0, (unsigned)addr,
	    (unsigned)bytes, wr,
	    (bad == 0u) ? 0 : ((bad == 0xFFFFFFFFu) ? -1 : (int)bad),
	    alive, (unsigned)clk);
	if ((wr != 0) || (alive == 0))
		f1p_recover(card, blk);
}

static int
f1_write_probe(sdio_card_t *card, uint32_t blksz)
{
	static const struct {
		uint8_t		block;
		uint32_t	addr;
		uint32_t	bytes;
	} st[4] = {
		{0u, 0x8000u, 4u},
		{0u, 0x8000u, 8u},
		{0u, 0x8000u, 48u},
		{0u, 0x8000u, 496u},
	};
	uint32_t i, j;
	int mismatch = 0;

	(void)blksz;
	f1p_core_state(card);
	f1p_card_state(card);

	/*
	 * The four host variants the block-mode question can turn on: the
	 * engine's shape (64 B blocks, SDIOEN=1) first, then the memory-card
	 * data path (SDIOEN=0), then the card's CIS block size (32 B) in both
	 * modes.  The real download's first chunk follows the last case, so a
	 * variant that works is left programmed for it.
	 */
	f1p_block_case(card, 0x8000u, 64u, 64u, true, 1u);
	f1p_block_case(card, 0x8000u, 64u, 64u, false, 2u);
	f1p_block_case(card, 0x8000u, 32u, 32u, true, 3u);
	f1p_block_case(card, 0x8000u, 32u, 32u, false, 4u);

	/* The byte-mode shapes, on the engine's 64-byte / SDIOEN=1 setting. */
	(void)SDIO_SetBlockSize(card, kSDIO_FunctionNum1, 64u);
	sdmmc_port_block_write_sdioen(true);
	for (i = 0u; i < 4u; i++) {
		uint32_t bytes = st[i].bytes;
		uint32_t bad = 0xFFFFFFFFu;
		uint8_t clk = 0u;
		int wr;
		int alive;

		for (j = 0u; j < bytes; j++)
			f1p_pat[j] = (uint8_t)(0xA5u ^ (uint8_t)(i * 17u) ^
					       (uint8_t)j);
		wr = f1p_write(card, st[i].addr, bytes, 0u, 0u);
		if ((wr == 0) && (f1p_readback(card, st[i].addr, bytes) == 0))
			bad = f1p_bad(bytes);
		alive = (f1p_alive(card, &clk) == 0) ? 1 : 0;
		lkpi_printf(ULOG_INFO_LEVEL,
		    "f1probe: byt%u addr=%05x n=%u wr=%d bad=%d alive=%d "
		    "clk=%02x\n",
		    i + 1u, (unsigned)st[i].addr, (unsigned)bytes, wr,
		    (bad == 0u) ? 0 : ((bad == 0xFFFFFFFFu) ? -1 : (int)bad),
		    alive, (unsigned)clk);
		if ((wr != 0) || (alive == 0)) {
			f1p_recover(card, 64u);
			break;
		}
		if ((bad != 0u) && (bad != 0xFFFFFFFFu))
			mismatch++;
	}

	lkpi_printf(ULOG_INFO_LEVEL,
	    "f1probe: done mismatch=%d\n", mismatch);
	return 0;
}
#endif /* CONFIG_F429_AP6181_F1_PROBE */

/* Block mode: count blocks of the size the card's function register holds. */
static int sdmmc_fsl_block_transfer(sdio_card_t *card, unsigned int func,
				    unsigned int addr, uint8_t *buf,
				    unsigned int blocks, bool write)
{
	uint32_t flags = (uint32_t)SDIO_EXTEND_CMD_BLOCK_MODE_MASK |
			 (uint32_t)SDIO_EXTEND_CMD_OP_CODE_MASK;
	uint32_t blksz = (func == 0U) ? card->io0blockSize :
					(uint32_t)card->ioFBR[func - 1U].ioBlockSize;
	status_t st;

	if ((blksz == 0U) || ((blksz & 3U) != 0U)) {
		SDMMC_LOG("sdio: block transfer with unusable block size %u\n",
			  (unsigned int)blksz);
		return -LKPI_SDIO_BE_ERR_BLKSIZE;
	}

	/*
	 * An unaligned buffer is staged by the stack through its internal buffer,
	 * which is bounded; a request larger than that bound would run off it. The
	 * general answer to an unaligned buffer is a bounce buffer, and that is
	 * Linux's own behaviour, so it belongs to the engine above this seam and
	 * not here.
	 */
	if ((((uintptr_t)buf % blksz) != 0U) &&
	    (((uint64_t)blksz * (uint64_t)blocks) > (uint64_t)card->internalBufferSize)) {
		SDMMC_LOG("sdio: unaligned block transfer of %u bytes exceeds the "
			  "staging buffer\n", (unsigned int)(blksz * blocks));
		return -LKPI_SDIO_BE_ERR_INVAL;
	}

#if defined(CONFIG_F429_AP6181_F1_BYTE_WRITE) && CONFIG_F429_AP6181_F1_BYTE_WRITE
	/*
	 * Measured fallback for the AP6181's function-1 blocks: the card accepts
	 * the first block-mode write and then answers every f1 command with R5,
	 * while byte-mode CMD53 writes of the same bytes complete and read back
	 * (the probe's stages 1-3).  The firmware download therefore reaches the
	 * card in 496-byte byte-mode transfers - a length that is not a multiple
	 * of the card's 64-byte block, which is the shape the card's own R5
	 * out-of-range check is known to accept.  Reads keep the block path that
	 * already works; this arm exists to get the firmware in and wlan0
	 * registered, not to claim the block path is understood.
	 */
#define F1_BYTE_WRITE_CHUNK	496u

	if (write && (func == 1u)) {
		uint32_t total = (uint32_t)blksz * (uint32_t)blocks;
		uint32_t done = 0u;

		while (done < total) {
			uint32_t n = total - done;
			int ret;

			if (n > F1_BYTE_WRITE_CHUNK)
				n = F1_BYTE_WRITE_CHUNK;
			ret = sdmmc_fsl_byte_transfer(card, func, addr + done,
						      buf + done, n, true, true);
			if (ret != 0)
				return ret;
			done += n;
		}
		return 0;
	}
#endif

	/*
	 * Bounded trace of the block transfers (twelve lines), which is what a
	 * session reads the window and address sequence from.
	 */
	{
		static unsigned int blk_logs;

		if (blk_logs < 12u) {
			blk_logs++;
			lkpi_printf(ULOG_INFO_LEVEL,
			    "fsl: blk %s f%u addr=%05x n=%u\n",
			    write ? "wr" : "rd", func, addr,
			    (unsigned int)blocks);
		}
	}
#if defined(CONFIG_F429_AP6181_F1_PROBE) && CONFIG_F429_AP6181_F1_PROBE
	/*
	 * The shape probe runs before the first f1 block write the driver
	 * issues - the firmware download's first chunk - and answers the
	 * question the second-command arms could not: which write shape this
	 * card accepts, and what the chip's core state is when it is asked.
	 * A failure stops the download here, with the card's own verdict
	 * printed, instead of letting the driver hammer a wedged function.
	 */
	if (write && (func == 1u) && !f1p_done) {
		f1p_done = true;
		if (f1_write_probe(card, blksz) != 0)
			return -LKPI_SDIO_BE_ERR_CARD;
	}
#endif
	unsigned int nblocks = blocks;

	if (write) {
		st = SDIO_IO_Write_Extended(card, (sdio_func_num_t)func, addr, buf,
					    nblocks, flags);
	} else {
		st = SDIO_IO_Read_Extended(card, (sdio_func_num_t)func, addr, buf,
					   blocks, flags);
	}
	if (st != kStatus_Success)
		return sdmmc_fsl_err(st);
#ifdef CONFIG_LKPI_CENSUS
	lkpi_fsl_census_state.blk_ops++;
	if (write)
		lkpi_fsl_census_state.tx_bytes += (uint64_t)blksz * blocks;
	else
		lkpi_fsl_census_state.rx_bytes += (uint64_t)blksz * blocks;
#endif

	return sdmmc_fsl_err(kStatus_Success);
}

void
lkpi_sdio_fsl_card_lock_get(struct sdmmc_lock_diag *out)
{
	sdio_card_t *card = sdmmc_card();

	if (card == NULL) {
		if (out != NULL) {
			out->owner = "-";
			out->held_ms = 0u;
			out->waiters = 0u;
			out->valid = 0u;
		}
		return;
	}
	sdmmc_osa_lock_diag_get_one(&card->lock, out);
}

static int lkpi_sdio_fsl_io_extended(void *ctx, unsigned int func, unsigned int addr,
				     void *buf, unsigned int count, unsigned int flags)
{
	sdio_card_t *card = sdmmc_card();
	bool write = (flags & LKPI_SDIO_BE_F_WRITE) != 0U;
	bool incr = (flags & LKPI_SDIO_BE_F_INCR) != 0U;
	int ret;
#ifdef CONFIG_LKPI_CENSUS
	uint64_t t0 = lkpi_census_now_ns();
#endif

	(void)ctx;

	if (card == NULL) {
		return -LKPI_SDIO_BE_ERR_NOCARD;
	}
	if ((buf == NULL) || (count == 0U) ||
	    (func > (unsigned int)LKPI_SDIO_BE_MAX_FUNCS)) {
		return -LKPI_SDIO_BE_ERR_INVAL;
	}

	lkpi_sdio_site_set("fsl.xfer");
	(void)lkpi_sdio_claim_check(1);
	/*
	 * The block flag says the caller's count is a byte count.  A request
	 * shorter than one block (the 472-byte C2H read at the end of a scan is
	 * the one that exposed it) cannot use the block path: passing 472 to the
	 * stack's block count turns it into 472 blocks of 512 bytes and the
	 * transfer never completes.  Fall back to the byte path for the tail.
	 */
	if ((flags & LKPI_SDIO_BE_F_BLOCK) != 0U) {
		uint32_t blksz = (func == 0U) ? card->io0blockSize :
						(uint32_t)card->ioFBR[func - 1U].ioBlockSize;


		/*
		 * With the block flag, `count` is a BLOCK COUNT, not a byte
		 * count (the seam says so, and the engine's splitter hands it
		 * over that way).  Comparing it to the block size - the check
		 * this file used to make - turned every one-block request (the
		 * common case: a 64-byte block at a 64-byte function) into a
		 * one-byte transfer, which returned success and moved one byte.
		 */
		if ((blksz != 0U) && (count != 0U)) {
			ret = sdmmc_fsl_block_transfer(card, func, addr, buf, count, write);
		} else {
			ret = sdmmc_fsl_byte_transfer(card, func, addr, buf, count, write, incr);
		}
	} else {
		ret = sdmmc_fsl_byte_transfer(card, func, addr, buf, count, write, incr);
	}
	lkpi_sdio_site_set("fsl.done");

#ifdef CONFIG_LKPI_CENSUS
	lkpi_fsl_census_state.ext_calls++;
	if (ret != 0)
		lkpi_fsl_census_state.errs++;
	lkpi_census_histo_add(&lkpi_fsl_census_state.lat, lkpi_census_now_ns() - t0);
#endif

	return ret;
}

/* --- block size and interrupts -------------------------------------------- */

static int lkpi_sdio_fsl_set_block_size(void *ctx, unsigned int func, unsigned int size)
{
	sdio_card_t *card = sdmmc_card();
	status_t st;

	(void)ctx;

	if (card == NULL) {
		return -LKPI_SDIO_BE_ERR_NOCARD;
	}
	if ((size == 0U) || (size > (unsigned int)SDIO_MAX_BLOCK_SIZE) ||
	    ((size & 3U) != 0U) || (func > (unsigned int)LKPI_SDIO_BE_MAX_FUNCS)) {
		return -LKPI_SDIO_BE_ERR_INVAL;
	}

	st = SDIO_SetBlockSize(card, (sdio_func_num_t)func, size);

	return sdmmc_fsl_err(st);
}

static int lkpi_sdio_fsl_irq_pending(void *ctx, u8 *pending)
{
	sdio_card_t *card = sdmmc_card();
	status_t st;

	(void)ctx;

	if ((card == NULL) || (pending == NULL)) {
		return -LKPI_SDIO_BE_ERR_INVAL;
	}

	/* The card's own pending word, read over a direct transfer: the card
	 * interrupt only says "look", the word says which function wants what,
	 * and this read happens under the pump's claim either way. */
	st = SDIO_GetPendingInterrupt(card, pending);

	return sdmmc_fsl_err(st);
}

/*
 * The card interrupt (DAT1), interrupt context: the controller's ISR reaches
 * this through the host glue and the stack's card-interrupt record, and its
 * whole contract is "wake the engine's pump". The pump then reads the pending
 * word above and runs the driver's handler under the claim; after the handler
 * has cleared the card's interrupt status (which is what releases DAT1), the
 * pump re-arms the line through the card_irq_mode op below.
 */
static void lkpi_sdio_fsl_card_irq(void *arg)
{
	(void)arg;

	lkpi_sdio_pump_notify();
}

static int lkpi_sdio_fsl_card_irq_mode(void *ctx, unsigned int enable)
{
	sdio_card_t *card = sdmmc_card();

	(void)ctx;

	if (card == NULL) {
		return -LKPI_SDIO_BE_ERR_NOCARD;
	}

	FDWMMCHOST_CardIntEnable(card->host, enable != 0U);

	return 0;
}

/* --- installation --------------------------------------------------------- */

static const struct lkpi_sdio_be_ops lkpi_sdio_fsl_ops = {
	.card_init = lkpi_sdio_fsl_card_init,
	.card_deinit = lkpi_sdio_fsl_card_deinit,
	.io_direct = lkpi_sdio_fsl_io_direct,
	.io_extended = lkpi_sdio_fsl_io_extended,
	.set_block_size = lkpi_sdio_fsl_set_block_size,
	.irq_pending = lkpi_sdio_fsl_irq_pending,
	.card_irq_mode = lkpi_sdio_fsl_card_irq_mode,
};

void lkpi_sdio_fsl_install(void)
{
	lkpi_sdio_be_register(&lkpi_sdio_fsl_ops, NULL);
}
