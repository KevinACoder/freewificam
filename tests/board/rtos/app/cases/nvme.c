/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   nvme.c
 * @brief  Case nvme (firefly v1): both controllers identify, one completion
 *         arrives through the message-interrupt path, and a fixed-offset
 *         write/read-back round trip on controller 0 compares byte for byte.
 *
 * The same-boot Linux baseline for this hardware: two Samsung SSD 980 250GB
 * controllers at 01:00.0 and 05:00.0, 64 MiB reads in the 600+ MB/s band and
 * a fixed-offset write/read-back compare equal on both disks. This case
 * carries the identity and data-integrity halves of that baseline (a
 * throughput number needs the host-side harness and is taken by dedicated
 * measurement rounds instead).
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"

#include "log.h"
#include "nvme.h"
#include "nvme_internal.h"
#include "pcie_ecam.h"

#define NVME_EXPECT_CTRLS	2U

/* One read of eight 512-byte blocks: forces a completion through the
 * message-interrupt path (a polled completion cannot show the ITS works). */
#define NVME_IRQ_BLOCKS		8U
#define NVME_IRQ_BYTES		(NVME_IRQ_BLOCKS * 512U)

/* The round-trip arm: 8 MiB written and read back at a 1 GiB offset, the
 * same landing zone the OS-side write tests use (past anything a boot
 * loader or filesystem header claims). */
#define NVME_VERIFY_MIB		8U
#define NVME_VERIFY_BYTES	(NVME_VERIFY_MIB * 1024u * 1024u)
#define NVME_VERIFY_OFF_BYTES	(1024u * 1024u * 1024u)

static uint8_t nvme_wr_buf[NVME_IRQ_BYTES] __attribute__((aligned(4096)));
static uint8_t nvme_tx_buf[NVME_VERIFY_BYTES];
static uint8_t nvme_rx_buf[NVME_VERIFY_BYTES];

/* One bring-up per boot: idempotent drivers, but the storage case runs
 * after the bus case and must not re-walk the tree. */
static int nvme_bringup_done;

static int nvme_bringup(void)
{
	if (!nvme_bringup_done) {
		(void)pcie_ecam_init();
		nvme_init();
		nvme_bringup_done = 1;
	}

	return nvme_ctrl_count() > 0u ? 0 : -1;
}

static void fill_pattern(uint8_t *buf, size_t len)
{
	uint32_t x = 0x13579bdfu;
	size_t i;

	for (i = 0u; i < len; i += sizeof(uint32_t)) {
		x = x * 1664525u + 1013904223u;
		(void)memcpy(&buf[i], &x, sizeof(uint32_t));
	}
}

int case_nvme_run(void)
{
	NVME_CAPABILITIES caps;
	char model[48];
	uint32_t ctrls;
	uint32_t i;
	uint32_t irqs;
	uint64_t lba;
	int32_t rc;

	if (nvme_bringup() != 0) {
		CASE_ANCHOR("nvme: FAIL bringup\n");
		return -1;
	}

	ctrls = nvme_ctrl_count();
	CASE_ANCHOR("nvme: ctrls=%u\n", (unsigned)ctrls);
	if (ctrls != NVME_EXPECT_CTRLS) {
		CASE_ANCHOR("nvme: FAIL ctrl count\n");
		return -1;
	}

	for (i = 0u; i < ctrls; i++) {
		if (Driver_NVME.Initialize(i, NULL) != ARM_DRIVER_OK) {
			CASE_ANCHOR("nvme: FAIL init %u\n", (unsigned)i);
			return -1;
		}
		caps = Driver_NVME.GetCapabilities(i);
		model[0] = '\0';
		if (Driver_NVME.Identify(i, model, sizeof(model)) !=
		    ARM_DRIVER_OK) {
			CASE_ANCHOR("nvme: FAIL identify %u\n", (unsigned)i);
			return -1;
		}
		if (model[0] == '\0' || caps.sector_count == 0u) {
			CASE_ANCHOR("nvme: FAIL empty identify %u\n",
				    (unsigned)i);
			return -1;
		}
		CASE_ANCHOR("nvme: nvme%u model=%s blocks=%llu sector=%u\n",
			    (unsigned)i, model,
			    (unsigned long long)caps.sector_count,
			    caps.sector_size);
	}

	/* One completion through the interrupt path on controller 0. */
	(void)memset(nvme_wr_buf, 0, sizeof(nvme_wr_buf));
	rc = Driver_NVME.ReadBlocks(0u, 0u, nvme_wr_buf, NVME_IRQ_BLOCKS);
	irqs = nvme_irq_count(0u);
	if (rc != ARM_DRIVER_OK) {
		CASE_ANCHOR("nvme: FAIL irq-path read rc=%d\n", (int)rc);
		return -1;
	}
	if (irqs < 1u) {
		CASE_ANCHOR("nvme: FAIL no message interrupt (%u)\n",
			    (unsigned)irqs);
		return -1;
	}
	CASE_ANCHOR("nvme: irq-path read ok irqs=%u\n", (unsigned)irqs);

	/* Fixed-offset round trip: fill, write, clear, read, compare. */
	fill_pattern(nvme_tx_buf, sizeof(nvme_tx_buf));
	lba = NVME_VERIFY_OFF_BYTES / 512u;

	rc = Driver_NVME.WriteBlocks(0u, lba, nvme_tx_buf,
				     NVME_VERIFY_BYTES / 512u);
	if (rc != ARM_DRIVER_OK) {
		CASE_ANCHOR("nvme: FAIL write rc=%d\n", (int)rc);
		return -1;
	}

	(void)memset(nvme_rx_buf, 0, sizeof(nvme_rx_buf));
	rc = Driver_NVME.ReadBlocks(0u, lba, nvme_rx_buf,
				     NVME_VERIFY_BYTES / 512u);
	if (rc != ARM_DRIVER_OK) {
		CASE_ANCHOR("nvme: FAIL read rc=%d\n", (int)rc);
		return -1;
	}

	if (memcmp(nvme_tx_buf, nvme_rx_buf, NVME_VERIFY_BYTES) != 0) {
		size_t off = 0u;

		while (off < NVME_VERIFY_BYTES &&
		       nvme_tx_buf[off] == nvme_rx_buf[off]) {
			off++;
		}
		CASE_ANCHOR("nvme: FAIL verify diff at +%zu\n", off);
		return -1;
	}
	CASE_ANCHOR("nvme: verify %uMiB@1GiB equal\n",
		    (unsigned)NVME_VERIFY_MIB);
	return 0;
}
