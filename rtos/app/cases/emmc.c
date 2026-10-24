/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   emmc.c
 * @brief  Case emmc: the soldered eMMC through the imported fsl_sdmmc stack,
 *         identified, round-tripped and error-pathed.
 *
 * What is claimed as evidence:
 *   emmc: cid / csd / extcsd   the card's identification as the stack parsed
 *                              it - the same fields the Linux side reports
 *                              (cid/csd hex and the ext_csd bytes)
 *   emmc: mode                 bus timing, width, clock and CMD23 state
 *   emmc: rw                   four blocks written, read back, CRC equal
 *   emmc: loop                 100 write/read rounds, zero errors
 *   emmc: badlba               an address past the user area returns an error
 *                              code instead of hanging the controller
 *   emmc: bw                   a bounded read/write rate sample (baseline only)
 *
 * The write window is the last 4 MiB of the user area: it never touches the
 * partition table or the first megabytes a Linux image may keep there, and the
 * exact LBA range is printed so the round's operator can see what moved.
 */

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

#include "sdmmc_mmc.h"

#define EMMC_CHUNK_BLOCKS	4u
#define EMMC_LOOPS		100u
#define EMMC_BW_BLOCKS		256u
#define EMMC_TAIL_RESERVE	8192u	/* blocks kept free below the end */

static uint8_t emmc_wbuf[EMMC_CHUNK_BLOCKS * 512u] __attribute__((aligned(4096)));
static uint8_t emmc_rbuf[EMMC_CHUNK_BLOCKS * 512u] __attribute__((aligned(4096)));
static uint8_t emmc_big[EMMC_BW_BLOCKS * 512u] __attribute__((aligned(4096)));

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
	uint32_t i;
	unsigned bit;

	for (i = 0u; i < len; i++) {
		crc ^= data[i];
		for (bit = 0u; bit < 8u; bit++) {
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
		}
	}
	return crc;
}

static uint32_t crc32_of(const uint8_t *data, uint32_t len)
{
	return crc32_update(0xffffffffu, data, len) ^ 0xffffffffu;
}

static void fill_pattern(uint8_t *buf, uint32_t len, uint32_t seed)
{
	uint32_t i;

	for (i = 0u; i < len; i++) {
		buf[i] = (uint8_t)((seed * 1103515245u + i * 12345u) >> 8);
	}
}

int case_emmc_run(void)
{
	struct sdmmc_mmc_info info;
	struct sdmmc_mmc_id id;
	uint32_t write_lba;
	uint32_t loop;
	unsigned errors = 0u;
	int ok = 1;
	int rc;

	if (sdmmc_mmc_card_start() != 0) {
		CASE_ANCHOR("emmc: FAIL card start\n");
		return -1;
	}

	if (sdmmc_mmc_info_get(&info) != 0) {
		CASE_ANCHOR("emmc: FAIL no card info\n");
		return -1;
	}

	CASE_ANCHOR("emmc: cardsize blocks=%lu clock=%luHz width=%lu timing=%lu cmd23=%d\n",
		    (unsigned long)info.blocks, (unsigned long)info.clock_hz,
		    (unsigned long)info.width, (unsigned long)info.timing,
		    info.cmd23);

	if (sdmmc_mmc_id_get(&id) != 0) {
		CASE_ANCHOR("emmc: FAIL no card id\n");
		return -1;
	}

	CASE_ANCHOR("emmc: cid manufacturer=0x%02x oem=0x%04x product=%s rev=0x%02x serial=0x%08lx date=0x%02x\n",
		    id.manufacturer, id.oem, id.product, id.revision,
		    (unsigned long)id.serial, id.date);
	CASE_ANCHOR("emmc: csd structure=%u spec=%u size=%lu read_bl_len=%u\n",
		    id.csd_structure, id.spec_version,
		    (unsigned long)id.device_size, id.read_block_len);
	CASE_ANCHOR("emmc: extcsd sectors=%lu rev=%u structure=%u type=0x%02x bus_width=%u hs_timing=%u\n",
		    (unsigned long)id.sectors, id.ext_csd_rev,
		    id.ext_csd_structure, id.card_type, id.bus_width,
		    id.hs_timing);

	if ((info.blocks < (EMMC_TAIL_RESERVE + 64u)) || (info.width == 0u)) {
		CASE_ANCHOR("emmc: FAIL geometry blocks=%lu width=%lu\n",
			    (unsigned long)info.blocks, (unsigned long)info.width);
		return -1;
	}

	write_lba = info.blocks - EMMC_TAIL_RESERVE;
	CASE_ANCHOR("emmc: window lba=%lu blocks=%u\n",
		    (unsigned long)write_lba, (unsigned)EMMC_CHUNK_BLOCKS);

	/* One round trip with a full readback compare. */
	fill_pattern(emmc_wbuf, sizeof(emmc_wbuf), 0x5a5au);
	if (sdmmc_mmc_write(write_lba, emmc_wbuf, EMMC_CHUNK_BLOCKS) != 0) {
		CASE_ANCHOR("emmc: FAIL write\n");
		return -1;
	}
	(void)sdmmc_mmc_flush();
	if (sdmmc_mmc_read(write_lba, emmc_rbuf, EMMC_CHUNK_BLOCKS) != 0) {
		CASE_ANCHOR("emmc: FAIL read\n");
		return -1;
	}

	{
		uint32_t wcrc = crc32_of(emmc_wbuf, sizeof(emmc_wbuf));
		uint32_t rcrc = crc32_of(emmc_rbuf, sizeof(emmc_rbuf));

		CASE_ANCHOR("emmc: rw crc_w=0x%08lx crc_r=0x%08lx verify=%s\n",
			    (unsigned long)wcrc, (unsigned long)rcrc,
			    (wcrc == rcrc) ? "ok" : "mismatch");
		if (wcrc != rcrc) {
			ok = 0;
		}
	}

	/* 100 rounds: write a fresh pattern, read it back, compare. */
	for (loop = 0u; loop < EMMC_LOOPS; loop++) {
		uint32_t seed = 0x1000u + loop;

		fill_pattern(emmc_wbuf, sizeof(emmc_wbuf), seed);
		rc = sdmmc_mmc_write(write_lba, emmc_wbuf, EMMC_CHUNK_BLOCKS);
		if (rc == 0) {
			rc = sdmmc_mmc_read(write_lba, emmc_rbuf,
					    EMMC_CHUNK_BLOCKS);
		}
		if (rc != 0) {
			errors++;
			CASE_ANCHOR("emmc: loop first-error n=%lu rc=%d\n",
				    (unsigned long)loop, rc);
			break;
		}
		if (crc32_of(emmc_wbuf, sizeof(emmc_wbuf)) !=
		    crc32_of(emmc_rbuf, sizeof(emmc_rbuf))) {
			errors++;
			CASE_ANCHOR("emmc: loop first-mismatch n=%lu\n",
				    (unsigned long)loop);
			break;
		}
	}
	(void)sdmmc_mmc_flush();

	CASE_ANCHOR("emmc: loop n=%u errors=%u\n", (unsigned)EMMC_LOOPS, errors);
	if (errors != 0u) {
		ok = 0;
	}

	/* Error path: an address past the user area must return an error, not
	 * leave the controller spinning. */
	{
		uint32_t bad = info.blocks + 1024u;
		int wrc = sdmmc_mmc_write(bad, emmc_wbuf, 1u);
		int rrc = sdmmc_mmc_read(bad, emmc_rbuf, 1u);

		CASE_ANCHOR("emmc: badlba lba=%lu write_rc=%d read_rc=%d nohang=%s\n",
			    (unsigned long)bad, wrc, rrc,
			    ((wrc < 0) && (rrc < 0)) ? "yes" : "no");
		if ((wrc >= 0) || (rrc >= 0)) {
			ok = 0;
		}
	}

	/* Bounded rate sample, baseline only (no target in this round). */
	{
		uint64_t t0;
		uint64_t us;
		uint32_t kib;

		fill_pattern(emmc_big, sizeof(emmc_big), 0x77u);
		t0 = rtos_clock_us();
		rc = sdmmc_mmc_write(write_lba + 16u, emmc_big, EMMC_BW_BLOCKS);
		us = rtos_clock_us() - t0;
		kib = (rc == 0) ? (uint32_t)(((uint64_t)sizeof(emmc_big) * 1000u) / (us + 1u)) : 0u;
		CASE_ANCHOR("emmc: bw write_kib_s=%lu us=%lu\n",
			    (unsigned long)kib, (unsigned long)us);

		t0 = rtos_clock_us();
		rc = sdmmc_mmc_read(write_lba + 16u, emmc_big, EMMC_BW_BLOCKS);
		us = rtos_clock_us() - t0;
		kib = (rc == 0) ? (uint32_t)(((uint64_t)sizeof(emmc_big) * 1000u) / (us + 1u)) : 0u;
		CASE_ANCHOR("emmc: bw read_kib_s=%lu us=%lu\n",
			    (unsigned long)kib, (unsigned long)us);
	}

	CASE_ANCHOR("emmc: %s\n", ok ? "PASS" : "FAIL");

	return ok ? 0 : -1;
}