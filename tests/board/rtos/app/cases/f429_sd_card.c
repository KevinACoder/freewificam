/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   f429_sd_card.c
 * @brief  Case f429_sd_card: the F429's TF slot as a raw SD block device -
 *         identified, read at LBA 0, written and read back in the card's last
 *         1 MiB window, then restored and compared.
 *
 * What is claimed as evidence:
 *   sdcard: cardsize         the stack's geometry and mode (blocks, 512-byte
 *                            block size, clock, 4-bit width, timing, version)
 *   sdcard: cid/csd/scr      the identification the imported stack decoded
 *   sdcard: read             one 512-byte block at LBA 0, with its CRC32
 *   sdcard: rw               one 512-byte block written to the tail window,
 *                            read back, CRC equal
 *   sdcard: restore          the original block written back and compared
 *   sdcard: PASS SDCARD      all of the above
 *
 * The write window is the last 1 MiB of the card, and the case restores the
 * original 512 bytes on its success path; the LBA is printed so a failure can
 * be traced.  The bench still uses a scratch card: a power cut between the
 * write and the restore leaves the pattern in that one block.
 *
 * Single-block transfers only.  The imported stack's multi-block path is the
 * next round; the host glue's completion wait accepts DBCKEND, which a
 * multi-block write can raise before the last block, so a multi-block arm must
 * fix that wait first rather than testing the card through it.
 */

#include "cases.h"

#include "sdmmc_sd.h"

/* The tail window: 2048 blocks is 1 MiB, the same window the USB MSC case
 * uses on a disk, so the two storage lines touch the same shape of address. */
#define SD_CARD_TAIL_BLOCKS	2048u

static uint8_t sd_read_buf[SDMMC_SD_SECTOR_SIZE] __attribute__((aligned(4096)));
static uint8_t sd_write_buf[SDMMC_SD_SECTOR_SIZE] __attribute__((aligned(4096)));
static uint8_t sd_original[SDMMC_SD_SECTOR_SIZE] __attribute__((aligned(4096)));

/* --- CRC32, table driven ----------------------------------------------------- */

static uint32_t crc32_table[256];
static int crc32_table_ready;

static void
crc32_init(void)
{
	uint32_t i;

	if (crc32_table_ready != 0) {
		return;
	}
	for (i = 0u; i < 256u; i++) {
		uint32_t c = i;
		unsigned bit;

		for (bit = 0u; bit < 8u; bit++) {
			c = (c & 1u) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
		}
		crc32_table[i] = c;
	}
	crc32_table_ready = 1;
}

static uint32_t
crc32_of(const uint8_t *data, uint32_t len)
{
	uint32_t crc = 0xffffffffu;
	uint32_t i;

	crc32_init();
	for (i = 0u; i < len; i++) {
		crc = crc32_table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
	}
	return crc ^ 0xffffffffu;
}

/* An 8-byte-step pattern: the value stays a per-byte function of the position,
 * so a shifted or torn transfer still fails the compare. */
static void
fill_pattern(uint8_t *buf, uint32_t len, uint32_t seed)
{
	uint32_t i;

	for (i = 0u; i + 8u <= len; i += 8u) {
		uint64_t v = ((uint64_t)seed << 32) | i;

		v ^= v << 13;
		v ^= v >> 7;
		v ^= v << 17;
		(void)memcpy(&buf[i], &v, sizeof(v));
	}
	for (; i < len; i++) {
		buf[i] = (uint8_t)((seed * 1103515245u + i * 12345u) >> 8);
	}
}

int
case_f429_sd_card_run(void)
{
	struct sdmmc_sd_info info;
	struct sdmmc_sd_id id;
	uint32_t write_lba;
	uint32_t wcrc;
	uint32_t rcrc;

	CASE_ANCHOR("sdcard: start\n");

	if (sdmmc_sd_card_start() != 0) {
		CASE_ANCHOR("sdcard: FAIL card start\n");
		return 1;
	}

	if (sdmmc_sd_info_get(&info) != 0) {
		CASE_ANCHOR("sdcard: FAIL no card info\n");
		return 1;
	}

	CASE_ANCHOR("sdcard: cardsize blocks=%lu block_size=%u clock=%lu "
		    "width=%u timing=%u version=%u flags=0x%08lx\n",
		    (unsigned long)info.blocks, (unsigned)info.block_size,
		    (unsigned long)info.clock_hz, (unsigned)info.width,
		    (unsigned)info.timing, (unsigned)info.version,
		    (unsigned long)info.flags);

	if (sdmmc_sd_id_get(&id) != 0) {
		CASE_ANCHOR("sdcard: FAIL no card id\n");
		return 1;
	}

	CASE_ANCHOR("sdcard: cid manufacturer=0x%02x oem=0x%04x product=%s "
		    "rev=0x%02x serial=0x%08lx date=0x%04x\n",
		    (unsigned)id.manufacturer, (unsigned)id.oem, id.product,
		    (unsigned)id.revision, (unsigned long)id.serial,
		    (unsigned)id.date);
	CASE_ANCHOR("sdcard: csd structure=%u size=%lu read_bl_len=%u\n",
		    (unsigned)id.csd_structure, (unsigned long)id.csd_size,
		    (unsigned)id.read_block_len);
	CASE_ANCHOR("sdcard: scr spec=%u bus_widths=0x%02x\n",
		    (unsigned)id.scr_spec, (unsigned)id.scr_bus_widths);

	/* The gate this line exists for: the card actually came up on the four
	 * data lines at the F429's ceiling, not at the 400 kHz identify clock or
	 * one bit wide. */
	if (info.width != 4U) {
		CASE_ANCHOR("sdcard: FAIL width=%u\n", (unsigned)info.width);
		return 1;
	}
	if (info.clock_hz < 20000000U) {
		CASE_ANCHOR("sdcard: FAIL clock=%lu\n",
			    (unsigned long)info.clock_hz);
		return 1;
	}
	if ((info.block_size != SDMMC_SD_SECTOR_SIZE) ||
	    (info.blocks < (SD_CARD_TAIL_BLOCKS + 64U))) {
		CASE_ANCHOR("sdcard: FAIL geometry blocks=%lu block_size=%u\n",
			    (unsigned long)info.blocks,
			    (unsigned)info.block_size);
		return 1;
	}

	/* A plain read first: it is the path that fails loudest when the host's
	 * block framing is wrong, and its CRC is the evidence the bytes moved. */
	if (sdmmc_sd_read(0U, sd_read_buf, 1U) != 0) {
		CASE_ANCHOR("sdcard: FAIL read lba=0\n");
		return 1;
	}
	CASE_ANCHOR("sdcard: read lba=0 rc=0 bytes=%u crc=0x%08lx\n",
		    (unsigned)SDMMC_SD_SECTOR_SIZE,
		    (unsigned long)crc32_of(sd_read_buf, SDMMC_SD_SECTOR_SIZE));

	write_lba = info.blocks - SD_CARD_TAIL_BLOCKS;
	if (sdmmc_sd_read(write_lba, sd_original, 1U) != 0) {
		CASE_ANCHOR("sdcard: FAIL read original lba=%lu\n",
			    (unsigned long)write_lba);
		return 1;
	}

	fill_pattern(sd_write_buf, sizeof(sd_write_buf), 0x5a5au);
	if (sdmmc_sd_write(write_lba, sd_write_buf, 1U) != 0) {
		CASE_ANCHOR("sdcard: FAIL write lba=%lu\n",
			    (unsigned long)write_lba);
		return 1;
	}
	if (sdmmc_sd_read(write_lba, sd_read_buf, 1U) != 0) {
		CASE_ANCHOR("sdcard: FAIL readback lba=%lu\n",
			    (unsigned long)write_lba);
		return 1;
	}

	wcrc = crc32_of(sd_write_buf, sizeof(sd_write_buf));
	rcrc = crc32_of(sd_read_buf, sizeof(sd_read_buf));
	CASE_ANCHOR("sdcard: rw lba=%lu crc_w=0x%08lx crc_r=0x%08lx "
		    "verify=%s\n",
		    (unsigned long)write_lba, (unsigned long)wcrc,
		    (unsigned long)rcrc, (wcrc == rcrc) ? "ok" : "mismatch");
	if (wcrc != rcrc) {
		CASE_ANCHOR("sdcard: FAIL verify lba=%lu\n",
			    (unsigned long)write_lba);
		return 1;
	}

	/* Put the original block back before the verdict; a failure here is a
	 * failed restore, not a failed write. */
	if (sdmmc_sd_write(write_lba, sd_original, 1U) != 0) {
		CASE_ANCHOR("sdcard: FAIL restore write lba=%lu\n",
			    (unsigned long)write_lba);
		return 1;
	}
	if (sdmmc_sd_read(write_lba, sd_read_buf, 1U) != 0) {
		CASE_ANCHOR("sdcard: FAIL restore read lba=%lu\n",
			    (unsigned long)write_lba);
		return 1;
	}

	CASE_ANCHOR("sdcard: restore lba=%lu verify=%s\n",
		    (unsigned long)write_lba,
		    (memcmp(sd_original, sd_read_buf, sizeof(sd_original)) == 0) ?
		    "ok" : "mismatch");
	if (memcmp(sd_original, sd_read_buf, sizeof(sd_original)) != 0) {
		CASE_ANCHOR("sdcard: FAIL restore compare lba=%lu\n",
			    (unsigned long)write_lba);
		return 1;
	}

	CASE_ANCHOR("sdcard: PASS SDCARD\n");
	return 0;
}
