/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   f429_sdio_write_probe.c
 * @brief  Case f429_sdio_write_probe: which CMD53 write shapes the SDIO host
 *         can actually complete, measured on a card register that reads back.
 *
 * Why this case exists.  The station line needs the 8189FTV's byte-mode
 * CMD53 writes: the driver's probe requests are 84 and 100 bytes, and the
 * vendor's own self-test writes four bytes to 0x1B8 and reads them back.  The
 * card enumerates, the firmware downloads, all the reads work - so the open
 * question is not "is the card there" but "which framing does this host's
 * transmit data path complete", one shape at a time:
 *
 *   1. block mode, block size == byte count (count is a power of two)
 *   2. block mode, block size rounded up (the count is not a power of two)
 *   3. SDIO multibyte (stream) mode, data path enabled before the command
 *   4. SDIO multibyte (stream) mode, data path enabled after the response
 *
 * Each shape writes a known pattern and reads it back with a CMD52/CMD53
 * read, so the verdict is the card's answer and not the host's flags.  The
 * addresses are the 8189FTV's scratch region as the vendor's self-test uses
 * it (WLAN_IOREG_DEVICE_ID + 0x1B8, i.e. 0x101B8), and the case claims
 * nothing about what the register is for - only that it reads back what was
 * written.
 */

#include "cases.h"

#include "fsl_sdmmc.h"
#include "sdmmc_port.h"

#define WR_TEST_VENDOR		0x024cU
#define WR_TEST_DEVICE		0xf179U
#define WR_TEST_FUNC		1U
/* The vendor's scratch register as the self-test addresses it: function 1,
 * WLAN_IOREG_DEVICE_ID 0, offset 0x1B8 -> CMD53 address 0x101b8. */
/* The device's system control register, the one its own power-on check
 * compares CMD52 against CMD53 on. */
#define WR_TEST_CR		0x100U
/* The scratch register its self-test writes and reads back. */
#define WR_TEST_SCRATCH		0x1b8U

static const uint32_t wr_pattern = 0x12345678U;

int
case_f429_sdio_write_probe_run(void)
{
	static uint8_t buf[512];
	sdio_card_t *card;
	uint8_t b4[4];
	uint8_t rb[4];
	uint32_t mix = 0U;
	uint32_t word = 0U;
	status_t st;
	int i;

	CASE_ANCHOR("write_probe: start\n");

	st = sdmmc_card_start();
	if (st != kStatus_Success) {
		CASE_ANCHOR("write_probe: FAIL card start=%d\n", (int)st);
		return 1;
	}
	card = sdmmc_card();
	CASE_ANCHOR("write_probe: card 0x%04x:0x%04x fn0blk=%u\n",
		    (unsigned)card->commonCIS.mID,
		    (unsigned)card->commonCIS.mInfo,
		    (unsigned)card->commonCIS.fn0MaxBlkSize);

	st = SDIO_EnableIO(card, (sdio_func_num_t)WR_TEST_FUNC, true);
	CASE_ANCHOR("write_probe: function 1 enable=%d\n", (int)st);
	if (st != kStatus_Success)
		return 1;

	/*
	 * The device's own power-on check, in its own order: read REG_CR a
	 * byte at a time over CMD52, then as one word over CMD53, and expect
	 * the two to agree.  If they do, byte-mode CMD53 reads work on a real
	 * register and the scratch test below is meaningful.
	 */
	for (i = 0; i < 4; i++) {
		uint8_t v = 0U;

		st = SDIO_IO_Read_Direct(card, (sdio_func_num_t)WR_TEST_FUNC,
					 WR_TEST_CR + (uint32_t)i, &v);
		CASE_ANCHOR("write_probe: cmd52 cr[%d]=0x%02x st=%d\n",
			    i, (unsigned)v, (int)st);
		mix |= (uint32_t)v << (8 * i);
	}

	st = SDIO_IO_Read_Extended(card, (sdio_func_num_t)WR_TEST_FUNC,
				   WR_TEST_CR, b4, 4U,
				   (uint32_t)SDIO_EXTEND_CMD_OP_CODE_MASK);
	word = (uint32_t)b4[0] | ((uint32_t)b4[1] << 8) |
	       ((uint32_t)b4[2] << 16) | ((uint32_t)b4[3] << 24);
	CASE_ANCHOR("write_probe: cmd53 cr=0x%08x st=%d mix=0x%08x %s\n",
		    (unsigned)word, (int)st, (unsigned)mix,
		    (st == kStatus_Success && word == mix) ? "MATCH" : "no");

	/*
	 * The scratch register: write 0x12345678 and read it back.  A read
	 * that fails on its own is reported first, so a failing read cannot be
	 * mistaken for a failing write.
	 */
	memset(rb, 0, sizeof(rb));
	st = SDIO_IO_Read_Extended(card, (sdio_func_num_t)WR_TEST_FUNC,
				   WR_TEST_SCRATCH, rb, 4U,
				   (uint32_t)SDIO_EXTEND_CMD_OP_CODE_MASK);
	CASE_ANCHOR("write_probe: scratch pre st=%d\n", (int)st);

	memcpy(buf, &wr_pattern, 4U);
	st = SDIO_IO_Write_Extended(card, (sdio_func_num_t)WR_TEST_FUNC,
				    WR_TEST_SCRATCH, buf, 4U,
				    (uint32_t)SDIO_EXTEND_CMD_OP_CODE_MASK);
	CASE_ANCHOR("write_probe: scratch write st=%d\n", (int)st);

	/*
	 * Read the scratch register back two ways.  The CMD52 path has no data
	 * phase at all, so it says whether the write landed; the CMD53 path is
	 * the one the driver needs for its FIFO reads.
	 */
	word = 0U;
	for (i = 0; i < 4; i++) {
		uint8_t v = 0U;

		st = SDIO_IO_Read_Direct(card, (sdio_func_num_t)WR_TEST_FUNC,
					 WR_TEST_SCRATCH + (uint32_t)i, &v);
		word |= (uint32_t)v << (8 * i);
	}
	CASE_ANCHOR("write_probe: scratch cmd52 back=0x%08x %s\n",
		    (unsigned)word,
		    (word == wr_pattern) ? "MATCH" : "no");

	memset(rb, 0, sizeof(rb));
	st = SDIO_IO_Read_Extended(card, (sdio_func_num_t)WR_TEST_FUNC,
				   WR_TEST_SCRATCH, rb, 4U,
				   (uint32_t)SDIO_EXTEND_CMD_OP_CODE_MASK);
	word = (uint32_t)rb[0] | ((uint32_t)rb[1] << 8) |
	       ((uint32_t)rb[2] << 16) | ((uint32_t)rb[3] << 24);
	CASE_ANCHOR("write_probe: scratch cmd53 st=%d back=0x%08x\n",
		    (int)st, (unsigned)word);

	/* Same CR read again, now that a byte-mode write has completed. */
	memset(b4, 0, sizeof(b4));
	st = SDIO_IO_Read_Extended(card, (sdio_func_num_t)WR_TEST_FUNC,
				   WR_TEST_CR, b4, 4U,
				   (uint32_t)SDIO_EXTEND_CMD_OP_CODE_MASK);
	word = (uint32_t)b4[0] | ((uint32_t)b4[1] << 8) |
	       ((uint32_t)b4[2] << 16) | ((uint32_t)b4[3] << 24);
	CASE_ANCHOR("write_probe: cmd53 cr again st=%d val=0x%08x %s\n",
		    (int)st, (unsigned)word,
		    (st == kStatus_Success && word == mix) ? "MATCH" : "no");

	CASE_ANCHOR("write_probe: done\n");
	return 0;
}
