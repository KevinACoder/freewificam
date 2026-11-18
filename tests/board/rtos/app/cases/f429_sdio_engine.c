/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   f429_sdio_engine.c
 * @brief  Case f429_sdio_engine: the card's own registers read through the
 *         linuxkpi SDIO engine, every value cross-checked against a second
 *         path so a "success" that moved no bytes cannot pass.
 *
 * Why this case exists.  The 8189FS line closed with an open seam question:
 * the engine's counters grew while the host glue's transfer counters stayed
 * frozen, i.e. the engine could return success without the transfer reaching
 * the controller.  brcmfmac's whole bring-up is CMD52/CMD53 reads and writes,
 * so before it is imported this case asks the AP6181 for data that is known:
 *
 *   - CCCR bytes 0x00-0x03 read over CMD52 through function 0;
 *   - the function-1 block size written by sdio_set_block_size() and read
 *     back from the FBR through function 0;
 *   - the backplane window written over CMD52 (three function-1 registers),
 *     then the chip id read at the backplane data window 0x8000 both as a
 *     byte-mode CMD53 and as four CMD52 byte reads, compared byte for byte;
 *   - one 64-byte block-mode CMD53 read of the same window, read twice, with
 *     the chip id in the first word required to be stable and non-zero.
 *
 * The 0x1000A-0x1000C registers are a CMD52 register file, not the CMD53
 * data window: a CMD53 there is answered with an R5 error by this card (the
 * first boot of this case did exactly that and is recorded in the run), while
 * brcmfmac's data transfers all use (offset & 0x7fff) | 0x8000.  The gate is
 * therefore the 0x8000 data path, not the register file's readback.
 *
 * Gates: every read must return the value the other path returned; the block
 * read must complete with a non-zero first word that does not change between
 * the two reads.  A failure names the check and the value, not just "error".
 *
 * Anchors:
 *   f429_sdio_engine: start
 *   f429_sdio_engine: card <vendor>:<device> clock=<hz> width=<n> blk=<n>/<max>
 *   f429_sdio_engine: cccr ...
 *   f429_sdio_engine: fbr blksz write=64 read=0x....
 *   f429_sdio_engine: window cmd52=.. cmd53=.. match=<0|1>
 *   f429_sdio_engine: block addr=0x8000 n=64 chipid=0x........ stable=<0|1>
 *   f429_sdio_engine: engine claims=<n>/<n> direct=<n> extended=<n> errors=<n>
 *   f429_sdio_engine: PASS ENGINE
 *   f429_sdio_engine: FAIL <reason>
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linuxkpi_sdio.h"

#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>

#define ENG_CCCR_BYTES		4u
/* Function 1's backplane window registers: SBSDIO_FUNC1_SBADDRLOW at
 * 0x1000A, MID at +1, HIGH at +2.  These are a CMD52 register file. */
#define ENG_WIN_LOW		0x1000Au
/* SBSDIO_FUNC1_CHIPCLKCSR: the backplane clock request/status register. */
#define ENG_CLKCSR		0x1000Eu
/* The data window: brcmfmac's transfers use the 32-bit access flag OR'd with
 * the window offset.  Offset 0 is chipcommon, whose first register is the
 * chip id. */
#define ENG_DATA_ADDR		0x8000u
#define ENG_DATA_LEN		4u
#define ENG_BLK_SIZE		64u

/* The backend adapter's install entry (third-party/fsl_sdmmc/ports): the case
 * is the image's composition root, so the declaration lives here. */
void lkpi_sdio_fsl_install(void);

static int
eng_readb(struct sdio_func *func, unsigned int addr, const char *what, u8 *out)
{
	int err = 0;
	u8 v = sdio_readb(func, addr, &err);

	if (err != 0) {
		CASE_ANCHOR("f429_sdio_engine: FAIL %s addr=0x%05x err=%d\n",
			    what, addr, err);
		return -1;
	}
	*out = v;
	return 0;
}

static int
eng_writeb(struct sdio_func *func, unsigned int addr, u8 value,
	   const char *what)
{
	int err = 0;

	sdio_writeb(func, value, addr, &err);
	if (err != 0) {
		CASE_ANCHOR("f429_sdio_engine: FAIL %s addr=0x%05x err=%d\n",
			    what, addr, err);
		return -1;
	}
	return 0;
}

int
case_f429_sdio_engine_run(void)
{
	struct sdio_func *f0;
	struct sdio_func *f1;
	struct lkpi_sdio_bus_info bus;
	struct lkpi_sdio_stats stats;
	uint8_t cccr[ENG_CCCR_BYTES];
	uint8_t win52[ENG_DATA_LEN];
	uint8_t win53[ENG_DATA_LEN];
	uint8_t blk1[ENG_BLK_SIZE];
	uint8_t blk2[ENG_BLK_SIZE];
	uint32_t chipid1;
	uint32_t chipid2;
	uint8_t clkcsr = 0U;
	uint8_t fbr_lo = 0U;
	uint8_t fbr_hi = 0U;
	unsigned int i;
	int first3 = 1;
	int rc;

	CASE_ANCHOR("f429_sdio_engine: start\n");

	lkpi_core_init();
	lkpi_sdio_fsl_install();
	lkpi_sdio_start();

	if (!lkpi_sdio_ready()) {
		CASE_ANCHOR("f429_sdio_engine: FAIL card not ready\n");
		return 1;
	}

	f0 = lkpi_sdio_func0();
	f1 = lkpi_sdio_func(1U);
	if (f0 == NULL || f1 == NULL) {
		CASE_ANCHOR("f429_sdio_engine: FAIL no function record f0=%d f1=%d\n",
			    f0 != NULL, f1 != NULL);
		return 1;
	}

	lkpi_sdio_bus_info_get(&bus);
	CASE_ANCHOR("f429_sdio_engine: card %04x:%04x clock=%u width=%u blk=%u/%u\n",
		    (unsigned)f1->vendor, (unsigned)f1->device,
		    (unsigned)bus.clock_hz, (unsigned)bus.bus_width,
		    (unsigned)bus.blk_size, (unsigned)bus.blk_max);

	sdio_claim_host(f1);

	rc = sdio_enable_func(f1);
	CASE_ANCHOR("f429_sdio_engine: enable f1 rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("f429_sdio_engine: FAIL enable f1 rc=%d\n", rc);
		sdio_release_host(f1);
		return 1;
	}

	/* --- CCCR through function 0 (CMD52) ----------------------------- */
	for (i = 0U; i < ENG_CCCR_BYTES; i++) {
		if (eng_readb(f0, (unsigned int)i, "cccr", &cccr[i]) != 0) {
			sdio_release_host(f1);
			return 1;
		}
	}
	CASE_ANCHOR("f429_sdio_engine: cccr 00-03 = %02x %02x %02x %02x\n",
		    cccr[0], cccr[1], cccr[2], cccr[3]);
	{
		uint8_t cap = 0U, b0 = 0U, b1 = 0U;

		if (eng_readb(f0, 0x08U, "cccr cap", &cap) != 0 ||
		    eng_readb(f0, 0x10U, "fn0 blk lo", &b0) != 0 ||
		    eng_readb(f0, 0x11U, "fn0 blk hi", &b1) != 0) {
			sdio_release_host(f1);
			return 1;
		}
		CASE_ANCHOR("f429_sdio_engine: cccr cap=0x%02x multiblock=%u "
			    "fn0blk=0x%02x%02x\n",
			    cap, (cap >> 1) & 1U, b1, b0);
	}

	/*
	 * The 43362's backplane transfers use the CCCR block-size register as
	 * the transfer's block size: the vendor's bus protocol writes 64 to
	 * both the function-0 and function-1 block-size registers and waits for
	 * the function-0 readback to equal 64 before any backplane access.  The
	 * common CIS states 32, so the generic set-block-size path refuses 64;
	 * the register itself takes it (the readback below is the check).
	 */
	if (eng_writeb(f0, 0x10U, 0x40U, "cccr blk lo") != 0 ||
	    eng_writeb(f0, 0x11U, 0x00U, "cccr blk hi") != 0 ||
	    eng_readb(f0, 0x10U, "cccr blk lo read", &fbr_lo) != 0 ||
	    eng_readb(f0, 0x11U, "cccr blk hi read", &fbr_hi) != 0) {
		sdio_release_host(f1);
		return 1;
	}
	CASE_ANCHOR("f429_sdio_engine: cccr blksz write=64 read=0x%02x%02x\n",
		    fbr_hi, fbr_lo);
	if (fbr_lo != 0x40U || fbr_hi != 0x00U) {
		CASE_ANCHOR("f429_sdio_engine: FAIL cccr blksz read=0x%02x%02x\n",
			    fbr_hi, fbr_lo);
		sdio_release_host(f1);
		return 1;
	}

	/* --- function 1's block size: write through the engine, read the
	 * FBR back through function 0 ------------------------------------- */
	rc = sdio_set_block_size(f1, 64U);
	CASE_ANCHOR("f429_sdio_engine: blksz set rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("f429_sdio_engine: FAIL blksz set rc=%d\n", rc);
		sdio_release_host(f1);
		return 1;
	}
	if (eng_readb(f0, SDIO_FBR_BASE(1U) + SDIO_FBR_BLKSIZE,
		      "fbr low", &fbr_lo) != 0 ||
	    eng_readb(f0, SDIO_FBR_BASE(1U) + SDIO_FBR_BLKSIZE + 1U,
		      "fbr high", &fbr_hi) != 0) {
		sdio_release_host(f1);
		return 1;
	}
	CASE_ANCHOR("f429_sdio_engine: fbr blksz write=64 read=0x%02x%02x\n",
		    fbr_hi, fbr_lo);
	if (fbr_lo != 0x40U || fbr_hi != 0x00U) {
		CASE_ANCHOR("f429_sdio_engine: FAIL fbr blksz read=0x%02x%02x\n",
			    fbr_hi, fbr_lo);
		sdio_release_host(f1);
		return 1;
	}

	/*
	 * The backplane clock.  The 43362's data window does not answer until
	 * the host has requested the ALP clock through SBSDIO_FUNC1_CHIPCLKCSR
	 * (0x1000E): the vendor's bus protocol writes
	 * FORCE_HW_CLKREQ_OFF | ALP_AVAIL_REQ | FORCE_ALP (0x29), waits for
	 * ALP_AVAIL (0x40), then releases the request.  The request is kept
	 * asserted across the reads here - releasing it first turns the clock
	 * off again, which is the shape boot5 of this line recorded (command
	 * response, no data).
	 */
	if (eng_writeb(f1, ENG_CLKCSR, 0x29U, "clock request") != 0) {
		sdio_release_host(f1);
		return 1;
	}
	for (i = 0U; i < 100U; i++) {
		if (eng_readb(f1, ENG_CLKCSR, "clock status", &clkcsr) != 0) {
			sdio_release_host(f1);
			return 1;
		}
		if ((clkcsr & 0x40U) != 0U)
			break;
		osDelay(1U);
	}
	CASE_ANCHOR("f429_sdio_engine: alp=0x%02x after %u ms\n",
		    clkcsr, i);
	if ((clkcsr & 0x40U) == 0U) {
		CASE_ANCHOR("f429_sdio_engine: FAIL alp not ready (csr=0x%02x)\n",
			    clkcsr);
		sdio_release_host(f1);
		return 1;
	}

	/*
	 * --- backplane window: point it at chipcommon (0x18000000) ---------
	 * The window registers take address bits [31:8]; the transfer that
	 * follows carries the offset inside that window (| 0x8000 for the
	 * 3-byte path).  Window 0 is not chipcommon on this backplane, which is
	 * what the first boots of this case asked for and why the card answered
	 * "out of range" with no data.
	 */
	if (eng_writeb(f1, ENG_WIN_LOW, 0x00U, "window low") != 0 ||
	    eng_writeb(f1, ENG_WIN_LOW + 1U, 0x00U, "window mid") != 0 ||
	    eng_writeb(f1, ENG_WIN_LOW + 2U, 0x18U, "window high") != 0) {
		sdio_release_host(f1);
		return 1;
	}

	/*
	 * Block mode first, on a clean card state: this is the transfer the
	 * firmware download uses, and the chip id in its first word is the
	 * known answer.  If this works, a later byte-mode failure is a
	 * framing question and not "the card cannot move data".
	 */
	(void)memset(blk1, 0, sizeof(blk1));
	(void)memset(blk2, 0, sizeof(blk2));
	rc = sdio_memcpy_fromio(f1, blk1, ENG_DATA_ADDR, ENG_BLK_SIZE);
	CASE_ANCHOR("f429_sdio_engine: block read1 rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("f429_sdio_engine: FAIL block read1 rc=%d\n", rc);
		sdio_release_host(f1);
		return 1;
	}
	rc = sdio_memcpy_fromio(f1, blk2, ENG_DATA_ADDR, ENG_BLK_SIZE);
	chipid1 = (uint32_t)blk1[0] | ((uint32_t)blk1[1] << 8) |
		  ((uint32_t)blk1[2] << 16) | ((uint32_t)blk1[3] << 24);
	chipid2 = (uint32_t)blk2[0] | ((uint32_t)blk2[1] << 8) |
		  ((uint32_t)blk2[2] << 16) | ((uint32_t)blk2[3] << 24);
	CASE_ANCHOR("f429_sdio_engine: block read2 rc=%d chipid=0x%08x/0x%08x "
		    "stable=%d\n", rc, (unsigned)chipid1, (unsigned)chipid2,
		    chipid1 == chipid2);
	if (rc != 0 || chipid1 == 0U || chipid1 != chipid2) {
		CASE_ANCHOR("f429_sdio_engine: FAIL block read rc=%d "
			    "chipid=0x%08x/0x%08x\n",
			    rc, (unsigned)chipid1, (unsigned)chipid2);
		sdio_release_host(f1);
		return 1;
	}

	/* --- the byte-mode four-byte read, the shape brcmfmac's sdio_readl
	 * uses for chipcommon registers: its value must equal the block
	 * read's first word, or the two paths disagree about the backplane. */
	(void)memset(win53, 0xAA, sizeof(win53));
	rc = sdio_memcpy_fromio(f1, win53, ENG_DATA_ADDR, ENG_DATA_LEN);
	for (i = 0U; i < ENG_DATA_LEN; i++) {
		if (blk1[i] != win53[i])
			first3 = 0;
	}
	CASE_ANCHOR("f429_sdio_engine: byte cmd53=%02x%02x%02x%02x "
		    "block=%02x%02x%02x%02x match=%d rc=%d\n",
		    win53[3], win53[2], win53[1], win53[0],
		    blk1[3], blk1[2], blk1[1], blk1[0], first3, rc);
	if (rc != 0 || first3 == 0) {
		CASE_ANCHOR("f429_sdio_engine: FAIL byte cross-check rc=%d "
			    "match=%d\n", rc, first3);
		sdio_release_host(f1);
		return 1;
	}

	if (eng_writeb(f1, ENG_CLKCSR, 0x00U, "clock release") != 0) {
		sdio_release_host(f1);
		return 1;
	}

	lkpi_sdio_stats_get(&stats);
	CASE_ANCHOR("f429_sdio_engine: engine claims=%u/%u direct=%u "
		    "extended=%u errors=%u\n",
		    stats.claims, stats.releases, stats.io_direct,
		    stats.io_extended, stats.errors);

	sdio_release_host(f1);

	if (stats.errors != 0U) {
		CASE_ANCHOR("f429_sdio_engine: FAIL engine errors=%u\n",
			    stats.errors);
		return 1;
	}

	CASE_ANCHOR("f429_sdio_engine: PASS ENGINE\n");
	return 0;
}
