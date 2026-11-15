/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   f429_sdram.c
 * @brief  F429 case f429_sdram: the SDRAM bring-up DMA arm - can the OTG_HS host's DMA
 *         reach the core board's 32 MB FMC SDRAM?
 *
 * main() already brought the SDRAM up and ran the bounded pattern test (the
 * `f429: sdram init/test` anchors), so this case only answers the one question
 * the wireless architecture depends on: the 8188EUS's receive path allocates
 * its skbs from the kernel heap, and the SDRAM bring-up plan puts that heap in SDRAM.  The
 * DWC2 host DMA must therefore be able to read and write SDRAM.
 *
 * The experiment is a U disk, not the wireless driver: the imported MSC class
 * hands the caller's buffer straight to the bulk-IN URB, so the same 4 KiB
 * sector range is read into internal SRAM and into SDRAM and the two buffers
 * are compared; then 1 MiB is read twice into two SDRAM windows and the two
 * checksums are compared.  A wrong-bus DMA shows up as a compare failure with
 * an internal-vs-SDRAM split, not as a mysterious receiver stall three layers
 * up.  If no mass-storage instance appears the arm is skipped (the disk is a
 * bench prerequisite, not part of the device under test) and the run record
 * marks it unverified.
 *
 * Anchors:
 *   f429: sdram case buffers internal=... sdram=...
 *   f429: sdram dma compare ok bytes=4096
 *   f429: sdram dma bulk ok blocks=2048 chunk=128 sum=...
 *   f429: sdram dma PASS
 *   f429: sdram dma SKIP no-msc hprt=... irq=...
 *   f429: sdram dma FAIL <reason>
 *
 * What this is not: a storage test (the usb_dwc2 profile owns the write
 * roundtrip) and not a memory-performance test; it answers reachability only.
 */

#include <stdint.h>
#include <string.h>

#include "board.h"
#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "usb_glue.h"
#include "usbh_core.h"
#include "usbh_msc.h"

#include "msc_app.h"

#include "fmc_sdram.h"

#define SDRAM_CASE_BLOCK_BYTES	512U
#define SDRAM_CASE_READ_BLOCKS	8U			/* 4 KiB */
#define SDRAM_CASE_READ_BYTES	(SDRAM_CASE_BLOCK_BYTES * SDRAM_CASE_READ_BLOCKS)
#define SDRAM_CASE_WAIT_MS	30000U
#define SDRAM_CASE_WAIT_STEP_MS	100U

/* Bulk arm: 1 MiB in 64 KiB chunks (a DWC2 channel transfer is bounded and the
 * chunk size is the port's own safe size, not a guess at the HCD's splitting).
 * The three windows are 16/32/48 MiB into the part, far above the 4 MiB heap
 * the wireless profiles place at the base. */
#define SDRAM_CASE_BULK_BLOCKS	2048U
#define SDRAM_CASE_CHUNK_BLOCKS	128U
#define SDRAM_CASE_CHUNK_BYTES	(SDRAM_CASE_BLOCK_BYTES * SDRAM_CASE_CHUNK_BLOCKS)
#define SDRAM_CASE_WINDOW_A	(F429_SDRAM_BASE + 0x02000000UL)
#define SDRAM_CASE_WINDOW_B	(F429_SDRAM_BASE + 0x03000000UL)

static uint8_t case_ref[SDRAM_CASE_READ_BYTES] __attribute__((aligned(32)));
static uint8_t case_sdram[SDRAM_CASE_READ_BYTES] __attribute__((aligned(32)));

static uint32_t
sum32(const uint8_t *p, uint32_t len)
{
	uint32_t s = 0U;
	uint32_t i;

	for (i = 0U; i + 4U <= len; i += 4U) {
		uint32_t w = (uint32_t)p[i] |
			     ((uint32_t)p[i + 1U] << 8) |
			     ((uint32_t)p[i + 2U] << 16) |
			     ((uint32_t)p[i + 3U] << 24);

		s = (s * 31U) + w;
	}
	return s;
}

static int
msc_wait(struct usbh_msc **out, unsigned int wait_ms)
{
	for (unsigned int waited = 0; waited < wait_ms;
	     waited += SDRAM_CASE_WAIT_STEP_MS) {
		*out = usb_msc_instance();
		if (*out != NULL)
			return 0;
		osDelay(SDRAM_CASE_WAIT_STEP_MS);
	}
	return -1;
}

static int
read_sdram(struct usbh_msc *msc, uint32_t lba, uint8_t *dst, uint32_t blocks)
{
	uint32_t done = 0U;

	while (done < blocks) {
		uint32_t n = blocks - done;

		if (n > SDRAM_CASE_CHUNK_BLOCKS)
			n = SDRAM_CASE_CHUNK_BLOCKS;
		if (usbh_msc_scsi_read10(msc, lba + done, dst, n) != 0)
			return -1;
		dst += n * SDRAM_CASE_BLOCK_BYTES;
		done += n;
	}
	return 0;
}

int
case_f429_sdram_run(void)
{
	struct usbh_msc *msc = NULL;
	uint32_t sum_a = 0U;
	uint32_t sum_b = 0U;

	CASE_ANCHOR("f429: sdram case buffers internal=%p sdram=%p bulk=%p/%p\n",
		    (void *)case_ref, (void *)(F429_SDRAM_BASE + 0x01000000UL),
		    (void *)SDRAM_CASE_WINDOW_A, (void *)SDRAM_CASE_WINDOW_B);

	if (usb_board_init(0) != 0) {
		CASE_ANCHOR("f429: sdram dma SKIP no-controller\n");
		return 0;
	}
	if (usbh_initialize(0, usb_board_reg_base(0),
			    usb_board_event_handler) != 0) {
		CASE_ANCHOR("f429: sdram dma SKIP usbh_initialize failed\n");
		return 0;
	}
	usb_board_after_start(0);

	if (msc_wait(&msc, SDRAM_CASE_WAIT_MS) != 0) {
		CASE_ANCHOR("f429: sdram dma SKIP no-msc hprt=%08x irq=%u "
			    "(the DMA arm needs the bench U disk; the SDRAM "
			    "pattern test above stands on its own)\n",
			    (unsigned)usb_board_port_status(),
			    usb_board_irq_count(0));
		return 0;
	}

	/* Same LBA range into internal SRAM and into SDRAM. */
	if (usbh_msc_scsi_read10(msc, 0U, case_ref, SDRAM_CASE_READ_BLOCKS) != 0) {
		CASE_ANCHOR("f429: sdram dma FAIL internal read\n");
		return 1;
	}
	if (usbh_msc_scsi_read10(msc, 0U,
				 (uint8_t *)(F429_SDRAM_BASE + 0x01000000UL),
				 SDRAM_CASE_READ_BLOCKS) != 0) {
		CASE_ANCHOR("f429: sdram dma FAIL sdram read\n");
		return 1;
	}
	memcpy(case_sdram, (const void *)(F429_SDRAM_BASE + 0x01000000UL),
	       SDRAM_CASE_READ_BYTES);
	if (memcmp(case_ref, case_sdram, SDRAM_CASE_READ_BYTES) != 0) {
		CASE_ANCHOR("f429: sdram dma FAIL compare internal vs sdram\n");
		return 1;
	}
	CASE_ANCHOR("f429: sdram dma compare ok bytes=%u\n",
		    (unsigned)SDRAM_CASE_READ_BYTES);

	/* 1 MiB into window A, then again into window B: same disk content, two
	 * SDRAM addresses, so both the IN direction and address decode are in
	 * the path. */
	if (read_sdram(msc, 0U, (uint8_t *)SDRAM_CASE_WINDOW_A,
		       SDRAM_CASE_BULK_BLOCKS) != 0) {
		CASE_ANCHOR("f429: sdram dma FAIL bulk read A\n");
		return 1;
	}
	if (read_sdram(msc, 0U, (uint8_t *)SDRAM_CASE_WINDOW_B,
		       SDRAM_CASE_BULK_BLOCKS) != 0) {
		CASE_ANCHOR("f429: sdram dma FAIL bulk read B\n");
		return 1;
	}
	for (uint32_t off = 0U; off < SDRAM_CASE_BULK_BLOCKS * SDRAM_CASE_BLOCK_BYTES;
	     off += SDRAM_CASE_CHUNK_BYTES) {
		sum_a += sum32((const uint8_t *)(SDRAM_CASE_WINDOW_A + off),
			       SDRAM_CASE_CHUNK_BYTES);
		sum_b += sum32((const uint8_t *)(SDRAM_CASE_WINDOW_B + off),
			       SDRAM_CASE_CHUNK_BYTES);
	}
	if (sum_a != sum_b) {
		CASE_ANCHOR("f429: sdram dma FAIL bulk checksum a=%08x b=%08x\n",
			    (unsigned)sum_a, (unsigned)sum_b);
		return 1;
	}
	/* The bulk window's first sector must be the internal read's first. */
	if (memcmp(case_ref, (const void *)SDRAM_CASE_WINDOW_A,
		   SDRAM_CASE_READ_BYTES) != 0) {
		CASE_ANCHOR("f429: sdram dma FAIL bulk head differs\n");
		return 1;
	}
	CASE_ANCHOR("f429: sdram dma bulk ok blocks=%u chunk=%u sum=%08x\n",
		    (unsigned)SDRAM_CASE_BULK_BLOCKS,
		    (unsigned)SDRAM_CASE_CHUNK_BLOCKS, (unsigned)sum_a);

	CASE_ANCHOR("f429: sdram dma PASS\n");
	return 0;
}
