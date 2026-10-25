/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   pcie_bench.c
 * @brief  Case pcie_bench: the NVMe link measured against the same disk on
 *         Linux - 4K/64K/1M, QD1/QD8, read and write, three repetitions each.
 *
 * The B1 question is not "how fast is the board" but "is the board's PCIe +
 * NVMe path within 10% of Linux on the same disk, at the same block size and
 * queue depth".  So the case uses the raw namespace (no filesystem, no exFAT)
 * in a scratch window at the end of the device, exactly the shape the Linux
 * leg's fio uses, and it keeps up to eight commands in flight through the
 * driver's asynchronous submit/reap pair.
 *
 * What is measured, and how:
 *   - wall time per cell from the AArch64 counter (`rtos_clock_us`), bytes
 *     fixed at 64 MiB per repetition;
 *   - the per-command cost (`cmd_us`) and the slope/intercept of
 *     `us_per_command = C + bs / BW` over the three block sizes: C is the
 *     fixed cost per command, BW the asymptotic bandwidth;
 *   - the message-interrupt delta and the completions consumed;
 *   - `cpu_us`, the sum of the clock deltas around the submit/reap calls.  The
 *     wait is a bounded busy poll (no tick sleep), so cpu_us is expected to be
 *     close to the wall time at QD1 - that is the honest reading of "how much
 *     CPU the number cost", not a claim that Linux would spend the same.
 *
 * Anchors (the judge checks the PASS and the medians):
 *   pcie-bench: window lba_start=<n> lba_end=<n> slice_blocks=<n>
 *   pcie-bench: bs=<n> qd=<n> dir=<read|write> rep=<n> bytes=<n> us=<n>
 *               mib_s=<n> cmd_us=<n> irqs=<n> cpu_us=<n>
 *   pcie-bench: median bs=<n> qd=<n> dir=<...> mib_s=<n> us=<n>
 *   pcie-bench: fit qd=<n> dir=<...> c_us=<n> bw_mib_s=<n> r2_permille=<n>
 *   pcie-bench: PASS
 *
 * The bench never prints per command: at 4K QD1 one line per command would be
 * 196k lines of UART and the measurement would be the console.
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "dwc_nvme.h"
#include "dwc_nvme_regs.h"
#include "nvme.h"

#include "pcie_cases.h"

#define BENCH_BS_COUNT		3U
#define BENCH_QD_COUNT		2U
#define BENCH_REPS		3U
#define BENCH_BYTES		(64U * 1024U * 1024U)
#define BENCH_WINDOW_BYTES	(1024ULL * 1024ULL * 1024ULL)
#define BENCH_QD_MAX		8U
#define BENCH_TIMEOUT_MS	2000U
#define BENCH_ALIGN_BYTES	(1024U * 1024U)

static const uint32_t bench_bs[BENCH_BS_COUNT] = { 4096U, 65536U, 1048576U };
static const uint32_t bench_qd[BENCH_QD_COUNT] = { 1U, 8U };

static uint8_t bench_buf[BENCH_QD_MAX][1024U * 1024U]
	__attribute__((aligned(4096)));

struct bench_cell {
	uint32_t bs;
	uint32_t cmd_bytes;
	uint32_t qd;
	uint8_t write;
	uint32_t us[BENCH_REPS];
	uint32_t mib_s[BENCH_REPS];
	uint32_t cmd_us[BENCH_REPS];
	uint32_t irqs[BENCH_REPS];
	uint64_t cpu_us[BENCH_REPS];
	uint32_t median_us;
	uint32_t median_mib_s;
};

static uint32_t bench_mib_s(uint32_t bytes, uint32_t us)
{
	if (us == 0U) {
		return 0U;
	}
	return (uint32_t)(((uint64_t)bytes * 1000000ULL) /
			  ((uint64_t)us * 1048576ULL));
}

static uint32_t bench_median(uint32_t a, uint32_t b, uint32_t c)
{
	uint32_t t;

	if (a > b) {
		t = a; a = b; b = t;
	}
	if (b > c) {
		b = c;
	}
	if (a > b) {
		t = a; a = b; b = t;
	}
	return b;
}

/* One cell: reps × 64 MiB through the asynchronous pair.  Returns 0 on
 * success; a timeout or a failed command is reported and aborts the case. */
static int bench_run_cell(struct bench_cell *cell, uint64_t lba_start,
			  uint32_t slice_blocks, uint32_t sector,
			  uint32_t cell_index)
{
	uint32_t cmds = BENCH_BYTES / cell->cmd_bytes;
	uint32_t rep;

	if (cmds == 0U || cmds * cell->cmd_bytes != BENCH_BYTES ||
	    cell->cmd_bytes % sector != 0U) {
		CASE_ANCHOR("pcie-bench: FAIL bad cell bs=%u\n", cell->bs);
		return 1;
	}

	for (rep = 0U; rep < BENCH_REPS; rep++) {
		uint32_t slices = (uint32_t)(BENCH_WINDOW_BYTES /
					     ((uint64_t)slice_blocks * sector));
		uint32_t slice = (cell_index * BENCH_REPS + rep) % slices;
		uint64_t lba = lba_start +
			       (uint64_t)slice * slice_blocks;
		uint32_t inflight = 0U;
		uint32_t i;
		uint32_t irqs0 = dwc_nvme_irq_count(0U);
		uint64_t t0;
		uint64_t t1;
		uint64_t cpu = 0U;

		t0 = rtos_clock_us();
		for (i = 0U; i < cmds; i++) {
			uint8_t *buf = bench_buf[i % BENCH_QD_MAX];
			uint16_t cid;
			uint64_t a;
			uint64_t b;
			int32_t rc;

			while (inflight >= cell->qd) {
				a = rtos_clock_us();
				rc = dwc_nvme_reap(0U, &cid, BENCH_TIMEOUT_MS);
				b = rtos_clock_us();
				cpu += b - a;
				if (rc < 0) {
					CASE_ANCHOR("pcie-bench: FAIL reap rc=%d\n",
						    (int)rc);
					return 1;
				}
				if (rc == 0) {
					CASE_ANCHOR("pcie-bench: FAIL reap timeout\n");
					return 1;
				}
				inflight--;
			}

			a = rtos_clock_us();
			rc = dwc_nvme_submit_async(0U,
						   cell->write ? NVME_OPC_IO_WRITE
							       : NVME_OPC_IO_READ,
						   lba + (uint64_t)i *
							 (cell->cmd_bytes / sector),
						   buf, cell->cmd_bytes, &cid);
			b = rtos_clock_us();
			cpu += b - a;
			if (rc != ARM_DRIVER_OK) {
				CASE_ANCHOR("pcie-bench: FAIL submit rc=%d\n",
					    (int)rc);
				return 1;
			}
			inflight++;
		}

		while (inflight > 0U) {
			uint16_t cid;
			uint64_t a = rtos_clock_us();
			int32_t rc = dwc_nvme_reap(0U, &cid, BENCH_TIMEOUT_MS);
			uint64_t b = rtos_clock_us();

			cpu += b - a;
			if (rc != 1) {
				CASE_ANCHOR("pcie-bench: FAIL drain rc=%d inflight=%u\n",
					    (int)rc, (unsigned)inflight);
				return 1;
			}
			inflight--;
		}

		t1 = rtos_clock_us();
		cell->us[rep] = (uint32_t)(t1 - t0);
		cell->cpu_us[rep] = cpu;
		cell->mib_s[rep] = bench_mib_s(BENCH_BYTES, cell->us[rep]);
		cell->cmd_us[rep] = (uint32_t)((t1 - t0) / cmds);
		cell->irqs[rep] = dwc_nvme_irq_count(0U) - irqs0;

		CASE_ANCHOR("pcie-bench: bs=%u cmd_bytes=%u qd=%u dir=%s rep=%u "
			    "bytes=%u us=%u mib_s=%u cmd_us=%u irqs=%u cpu_us=%llu\n",
			    (unsigned)cell->bs, (unsigned)cell->cmd_bytes,
			    (unsigned)cell->qd,
			    cell->write ? "write" : "read", (unsigned)rep,
			    (unsigned)BENCH_BYTES, (unsigned)cell->us[rep],
			    (unsigned)cell->mib_s[rep],
			    (unsigned)cell->cmd_us[rep],
			    (unsigned)cell->irqs[rep],
			    (unsigned long long)cell->cpu_us[rep]);
	}

	cell->median_us = bench_median(cell->us[0], cell->us[1], cell->us[2]);
	cell->median_mib_s = bench_mib_s(BENCH_BYTES, cell->median_us);
	CASE_ANCHOR("pcie-bench: median bs=%u cmd_bytes=%u qd=%u dir=%s "
		    "mib_s=%u us=%u\n",
		    (unsigned)cell->bs, (unsigned)cell->cmd_bytes,
		    (unsigned)cell->qd,
		    cell->write ? "write" : "read",
		    (unsigned)cell->median_mib_s, (unsigned)cell->median_us);
	return 0;
}

/* Least squares over the three block sizes for one (qd, direction):
 *   us_per_command = C + bs / BW
 * in integer arithmetic (no FP state on this image).  y is scaled by 1000 so
 * the slope keeps sub-µs precision; r2 is per-mille. */
static void bench_fit(const struct bench_cell *cells, uint32_t count,
		      uint32_t qd, uint8_t write)
{
	int64_t x[3];
	int64_t y[3];
	int64_t sum_x = 0;
	int64_t sum_y = 0;
	int64_t sum_xx = 0;
	int64_t sum_xy = 0;
	int64_t den;
	int64_t slope;	/* 1000 * µs per byte */
	int64_t intercept;	/* 1000 * µs per command */
	int64_t ss_tot = 0;
	int64_t ss_res = 0;
	int64_t y_mean;
	int64_t c_us;
	int64_t bw_mib_s;
	int64_t r2_permille;
	uint32_t n = 0U;
	uint32_t i;

	for (i = 0U; i < count && n < 3U; i++) {
		const struct bench_cell *cell = &cells[i];

		if (cell->qd != qd || cell->write != write) {
			continue;
		}
		x[n] = (int64_t)cell->cmd_bytes;
		/* µs per command, scaled by 1000 */
		y[n] = (int64_t)cell->median_us * 1000 /
		       (int64_t)(BENCH_BYTES / cell->cmd_bytes);
		sum_x += x[n];
		sum_y += y[n];
		sum_xx += x[n] * x[n];
		sum_xy += x[n] * y[n];
		n++;
	}
	if (n < 2U) {
		return;
	}

	den = (int64_t)n * sum_xx - sum_x * sum_x;
	if (den == 0) {
		return;
	}
	slope = ((int64_t)n * sum_xy - sum_x * sum_y) / den;
	intercept = (sum_y - slope * sum_x) / (int64_t)n;
	if (slope <= 0 || intercept < 0) {
		return;
	}

	y_mean = sum_y / (int64_t)n;
	for (i = 0U; i < n; i++) {
		int64_t fit = intercept + slope * x[i];

		ss_tot += (y[i] - y_mean) * (y[i] - y_mean);
		ss_res += (y[i] - fit) * (y[i] - fit);
	}
	r2_permille = (ss_tot > 0) ? (1000 - (ss_res * 1000) / ss_tot) : 0;
	if (r2_permille < 0) {
		r2_permille = 0;
	}

	c_us = intercept / 1000;
	/* slope is 1000*µs/byte -> bytes/µs = 1000/slope; MiB/s = bytes/µs *
	 * 1e6 / 1048576. */
	bw_mib_s = (slope > 0) ? (int64_t)((1000LL * 1000000LL) /
					   (slope * 1048576LL)) : 0;

	CASE_ANCHOR("pcie-bench: fit qd=%u dir=%s c_us=%d bw_mib_s=%d "
		    "r2_permille=%d\n",
		    (unsigned)qd, write ? "write" : "read",
		    (int)c_us, (int)bw_mib_s, (int)r2_permille);
}

int case_pcie_bench_run(void)
{
	struct bench_cell cells[BENCH_BS_COUNT * BENCH_QD_COUNT * 2U];
	uint64_t ns_blocks;
	uint64_t window_blocks;
	uint64_t lba_start;
	uint32_t sector;
	uint32_t slice_blocks;
	uint32_t max_xfer;
	uint32_t cell_index = 0U;
	uint32_t bi;
	uint32_t qi;
	uint32_t dir;
	uint32_t i;

	if (pcie_board_bringup() == 0U) {
		CASE_ANCHOR("pcie-bench: platform-inactive no nvme\n");
		return 2;
	}

	ns_blocks = pcie_ns_blocks();
	sector = pcie_ns_sector_size();
	if (ns_blocks == 0U || sector == 0U) {
		CASE_ANCHOR("pcie-bench: FAIL empty geometry\n");
		return 1;
	}

	/* The scratch window is the last 1 GiB of the namespace, aligned down
	 * to a 1 MiB boundary so the slice arithmetic is exact.  The Linux leg
	 * uses the same window (its run README carries the LBA range). */
	window_blocks = BENCH_WINDOW_BYTES / sector;
	if (ns_blocks <= window_blocks + 8U) {
		CASE_ANCHOR("pcie-bench: FAIL namespace too small (%llu blocks)\n",
			    (unsigned long long)ns_blocks);
		return 1;
	}
	lba_start = ns_blocks - window_blocks;
	lba_start &= ~((uint64_t)(BENCH_ALIGN_BYTES / sector) - 1U);
	slice_blocks = BENCH_BYTES / sector;

	max_xfer = dwc_nvme_max_transfer(0U);
	if (max_xfer == 0U || max_xfer > BENCH_BS_COUNT * 0U + 1048576U) {
		max_xfer = 1048576U;
	}
	CASE_ANCHOR("pcie-bench: id max_xfer=%u mdts_enforced=%u\n",
		    (unsigned)max_xfer, (unsigned)dwc_nvme_max_transfer(0U));

	CASE_ANCHOR("pcie-bench: window lba_start=%llu lba_end=%llu "
		    "slice_blocks=%u sector=%u\n",
		    (unsigned long long)lba_start,
		    (unsigned long long)(lba_start + window_blocks),
		    (unsigned)slice_blocks, (unsigned)sector);

	/* Fill the write buffers once, outside every timed window. */
	for (i = 0U; i < BENCH_QD_MAX; i++) {
		(void)memset(bench_buf[i], 0x5a, sizeof(bench_buf[i]));
	}

	for (bi = 0U; bi < BENCH_BS_COUNT; bi++) {
		for (qi = 0U; qi < BENCH_QD_COUNT; qi++) {
			for (dir = 0U; dir < 2U; dir++) {
				struct bench_cell *cell = &cells[cell_index];

				cell->bs = bench_bs[bi];
				cell->cmd_bytes = (cell->bs > max_xfer) ?
						  max_xfer : cell->bs;
				cell->qd = bench_qd[qi];
				cell->write = (uint8_t)dir;
				if (bench_run_cell(cell, lba_start, slice_blocks,
						   sector, cell_index) != 0) {
					return 1;
				}
				cell_index++;
			}
		}
	}

	for (qi = 0U; qi < BENCH_QD_COUNT; qi++) {
		for (dir = 0U; dir < 2U; dir++) {
			bench_fit(cells, cell_index, bench_qd[qi], (uint8_t)dir);
		}
	}

	CASE_ANCHOR("pcie-bench: PASS cells=%u\n", (unsigned)cell_index);
	return 0;
}