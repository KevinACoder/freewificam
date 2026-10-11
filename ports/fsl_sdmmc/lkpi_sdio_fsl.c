/*
 * @file   lkpi_sdio_fsl.c
 * @brief  The SDIO layer's backend over the imported SDIO stack: the six seam
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
 * (ports/cherryusb/lkpi_usb_cherryusb.c) implements its seam the same way -
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

#include "fsl_common.h"
#include "fsl_sdmmc.h"

#include "sdmmc_port.h"

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

/* --- card bring-up -------------------------------------------------------- */

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
	(void)ctx;

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

	if (write) {
		st = SDIO_IO_Write_Direct(card, (sdio_func_num_t)func, addr, value, false);
	} else {
		st = SDIO_IO_Read_Direct(card, (sdio_func_num_t)func, addr, value);
	}

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

		if (incr) {
			addr += 1U;
		}
	}

	return 0;
}

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

	if (write) {
		st = SDIO_IO_Write_Extended(card, (sdio_func_num_t)func, addr, buf,
					    blocks, flags);
	} else {
		st = SDIO_IO_Read_Extended(card, (sdio_func_num_t)func, addr, buf,
					   blocks, flags);
	}

	return sdmmc_fsl_err(st);
}

static int lkpi_sdio_fsl_io_extended(void *ctx, unsigned int func, unsigned int addr,
				     void *buf, unsigned int count, unsigned int flags)
{
	sdio_card_t *card = sdmmc_card();
	bool write = (flags & LKPI_SDIO_BE_F_WRITE) != 0U;
	bool incr = (flags & LKPI_SDIO_BE_F_INCR) != 0U;

	(void)ctx;

	if (card == NULL) {
		return -LKPI_SDIO_BE_ERR_NOCARD;
	}
	if ((buf == NULL) || (count == 0U) ||
	    (func > (unsigned int)LKPI_SDIO_BE_MAX_FUNCS)) {
		return -LKPI_SDIO_BE_ERR_INVAL;
	}

	if ((flags & LKPI_SDIO_BE_F_BLOCK) != 0U) {
		return sdmmc_fsl_block_transfer(card, func, addr, buf, count, write);
	}

	return sdmmc_fsl_byte_transfer(card, func, addr, buf, count, write, incr);
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

	/* The card's own pending word, read over a direct transfer: this port polls,
	 * so there is no interrupt line to wait on. */
	st = SDIO_GetPendingInterrupt(card, pending);

	return sdmmc_fsl_err(st);
}

/* --- installation --------------------------------------------------------- */

static const struct lkpi_sdio_be_ops lkpi_sdio_fsl_ops = {
	.card_init = lkpi_sdio_fsl_card_init,
	.card_deinit = lkpi_sdio_fsl_card_deinit,
	.io_direct = lkpi_sdio_fsl_io_direct,
	.io_extended = lkpi_sdio_fsl_io_extended,
	.set_block_size = lkpi_sdio_fsl_set_block_size,
	.irq_pending = lkpi_sdio_fsl_irq_pending,
};

void lkpi_sdio_fsl_install(void)
{
	lkpi_sdio_be_register(&lkpi_sdio_fsl_ops, NULL);
}
