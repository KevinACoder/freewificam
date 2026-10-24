/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   emmc.c
 * @brief  Case emmc: the soldered eMMC through the imported fsl_sdmmc stack -
 *         identified, round-tripped, error-pathed, and measured.
 *
 * What is claimed as evidence:
 *   emmc: cid / csd / extcsd   the card's identification as the stack parsed
 *                              it - the same fields the Linux side reports
 *                              (cid/csd hex and the ext_csd bytes)
 *   emmc: mode                 bus timing, width, clock and CMD23 state
 *   emmc: clk                  the clock chain as the controller holds it: the
 *                              CLOCK_CONTROL divider field and the HOST_CONTROL1
 *                              line bits, next to what the stack asked for
 *   emmc: rw                   four blocks written, read back, CRC equal
 *   emmc: loop                 100 write/read rounds, zero errors
 *   emmc: badlba               an address past the user area returns an error
 *                              code instead of hanging the controller
 *   emmc: sweep <dir> ...      a bounded chunk sweep (4 KiB .. 128 KiB) whose
 *                              marginal cost separates the per-command fixed
 *                              cost from the per-byte bus rate
 *   emmc: census ...           the controller's counters and wait spans across
 *                              the sweep window
 *
 * The write window is the last 4 MiB of the user area: it never touches the
 * partition table or the first megabytes a Linux image may keep there, and the
 * exact LBA range is printed so the round's operator can see what moved.
 *
 * Two measurement-hygiene rules this file keeps, because both were paid for:
 *  - the CRC is the table-driven one. The bitwise form cost ~25 cycles per byte,
 *    which is ~25 ms per 128 KiB buffer and would have been read as the card's
 *    speed had it been inside the window.
 *  - the per-command trace is switched OFF around the sweep. The board's anchor
 *    table promotes every "emmc: " line to the unfiltered level, so one line per
 *    command is ~6 ms of 115200-baud console: with the trace on, the sweep would
 *    have measured the UART.
 */

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

#include "sdmmc_mmc.h"

#define EMMC_CHUNK_BLOCKS	4u
#define EMMC_LOOPS		100u
#define EMMC_SWEEP_MAX_BLOCKS	256u	/* 128 KiB: the largest chunk swept */
#define EMMC_SWEEP_BYTES	(4u * 1024u * 1024u)	/* traffic per size */
#define EMMC_TAIL_RESERVE	8192u	/* blocks kept free below the end */

static uint8_t emmc_wbuf[EMMC_CHUNK_BLOCKS * 512u] __attribute__((aligned(4096)));
static uint8_t emmc_rbuf[EMMC_CHUNK_BLOCKS * 512u] __attribute__((aligned(4096)));
static uint8_t emmc_big[EMMC_SWEEP_MAX_BLOCKS * 512u] __attribute__((aligned(4096)));

/* --- CRC32, table driven ----------------------------------------------------- */

/* One 256-entry table, built once. The polynomial is the usual reflected
 * 0xedb88320; nothing here is timing-critical any more, but the table keeps the
 * verification pass at ~1 cycle/byte instead of ~25. */
static uint32_t crc32_table[256];
static int crc32_table_ready;

static void crc32_init(void)
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

static uint32_t crc32_of(const uint8_t *data, uint32_t len)
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
 * so a shifted or torn transfer still fails the compare, and the fill is fast
 * enough not to matter next to a transfer. */
static void fill_pattern(uint8_t *buf, uint32_t len, uint32_t seed)
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

/* --- the chunk sweep --------------------------------------------------------- */

/* The sizes: 4 KiB .. 128 KiB in powers of two. Each size moves the same 4 MiB,
 * so every point has the same statistical weight and a large block is not
 * favoured by a shorter window. */
static const uint32_t emmc_sweep_blocks[] = { 8u, 16u, 32u, 64u, 128u, 256u };

#define EMMC_SWEEP_SIZES	\
	(sizeof(emmc_sweep_blocks) / sizeof(emmc_sweep_blocks[0]))

/* One direction of the sweep. The data is whatever `emmc_big` holds; the write
 * direction therefore writes the same content every time, which is the honest
 * shape for a rate measurement (the card does not get to skip a rewrite of
 * identical bytes: eMMC has no such shortcut). */
static int emmc_sweep(uint32_t write_lba, int write)
{
	unsigned s;

	for (s = 0u; s < (unsigned)EMMC_SWEEP_SIZES; s++) {
		uint32_t blocks = emmc_sweep_blocks[s];
		uint32_t bytes = blocks * 512u;
		unsigned xfers = EMMC_SWEEP_BYTES / bytes;
		uint64_t t0;
		uint64_t us;
		unsigned i;

		if (xfers < 4u) {
			xfers = 4u;
		}

		t0 = rtos_clock_us();
		for (i = 0u; i < xfers; i++) {
			int rc = (write != 0) ?
				sdmmc_mmc_write(write_lba, emmc_big, blocks) :
				sdmmc_mmc_read(write_lba, emmc_big, blocks);

			if (rc != 0) {
				CASE_ANCHOR("emmc: FAIL sweep %s chunk=%u rc=%d\n",
					    (write != 0) ? "write" : "read",
					    (unsigned)(bytes / 1024u), rc);
				return -1;
			}
		}
		us = rtos_clock_us() - t0;

		CASE_ANCHOR("emmc: sweep %s chunk=%u KiB xfers=%u bytes=%lu "
			    "us=%lu kibs=%lu\n",
			    (write != 0) ? "write" : "read",
			    (unsigned)(bytes / 1024u), xfers,
			    (unsigned long)((uint64_t)bytes * xfers), us,
			    (unsigned long)((us != 0u) ?
				(((uint64_t)bytes * xfers * 1000u) / us) : 0u));
	}

	return 0;
}

/* --- the controller's wait account ------------------------------------------ */

struct emmc_census {
	uint32_t transfers;
	uint32_t boundaries;
	uint64_t polls;
	uint64_t cmd_us;
	uint64_t data_us;
};

static void emmc_census_take(struct emmc_census *c)
{
	struct sdmmc_mmc_host_state host;

	(void)memset(c, 0, sizeof(*c));
	if (sdmmc_mmc_host_state_get(&host) != 0) {
		return;
	}
	c->transfers = host.transfers;
	c->boundaries = host.boundary_events;
	c->polls = host.data_polls;
	c->cmd_us = host.cmd_us;
	c->data_us = host.data_us;
}

static void emmc_census_report(const char *tag, const struct emmc_census *a,
			       const struct emmc_census *b, unsigned long blocks)
{
	CASE_ANCHOR("emmc: census %s transfers=%lu blocks=%lu boundaries=%lu "
		    "data_polls=%lu cmd_us=%lu data_us=%lu\n",
		    tag,
		    (unsigned long)(b->transfers - a->transfers),
		    blocks,
		    (unsigned long)(b->boundaries - a->boundaries),
		    (unsigned long)(b->polls - a->polls),
		    (unsigned long)(b->cmd_us - a->cmd_us),
		    (unsigned long)(b->data_us - a->data_us));
}

/* --- the case --------------------------------------------------------------- */

int case_emmc_run(void)
{
	struct sdmmc_mmc_info info;
	struct sdmmc_mmc_id id;
	struct sdmmc_mmc_host_state host;
	struct emmc_census census_before;
	struct emmc_census census_after;
	uint32_t write_lba;
	uint32_t loop;
	unsigned long swept_blocks = 0ul;
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

	/* The clock chain and the line count as the registers hold them. The
	 * information line above reports what the stack asked for; this one is
	 * what the controller was programmed with, and it is the one that can
	 * catch a divider or a width that never took effect. */
	if (sdmmc_mmc_host_state_get(&host) == 0) {
		CASE_ANCHOR("emmc: clk asked=%luHz achieved=%luHz src_sel=%lu "
			    "clk_ctrl=0x%04lx host_ctrl1=0x%02lx lines=%u "
			    "extcsd_bus=%u\n",
			    (unsigned long)host.clock_asked_hz,
			    (unsigned long)host.clock_hz,
			    (unsigned long)host.cclk_sel,
			    (unsigned long)host.clk_ctrl,
			    (unsigned long)host.host_ctrl1, host.width,
			    id.bus_width);
	}

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

	/* 100 rounds: write a fresh pattern, read it back, compare. The per-command
	 * trace is on here - a hundred commands is 0.6 s of console, and this
	 * section's evidence is the command sequence. */
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

	/* The sweep: quiet window, controller census around it. */
	fill_pattern(emmc_big, sizeof(emmc_big), 0x77u);
	emmc_census_take(&census_before);
	sdmmc_mmc_trace_set(0);
	if (emmc_sweep(write_lba, 1) != 0) {
		ok = 0;
	}
	if (emmc_sweep(write_lba, 0) != 0) {
		ok = 0;
	}
	sdmmc_mmc_trace_set(1);
	emmc_census_take(&census_after);

	for (loop = 0u; loop < (uint32_t)EMMC_SWEEP_SIZES; loop++) {
		swept_blocks += ((unsigned long)EMMC_SWEEP_BYTES /
				 (emmc_sweep_blocks[loop] * 512u)) *
				emmc_sweep_blocks[loop] * 2u;
	}
	emmc_census_report("sweep", &census_before, &census_after, swept_blocks);

	CASE_ANCHOR("emmc: %s\n", ok ? "PASS" : "FAIL");

	return ok ? 0 : -1;
}