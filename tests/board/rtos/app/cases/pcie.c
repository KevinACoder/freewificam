/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   pcie.c
 * @brief  Case pcie: the DesignWare root complex comes up, the endpoint list
 *         is printed, the NVMe controller is probed, its namespace geometry is
 *         read and one real I/O completion is forced through the message-
 *         interrupt path.
 *
 * This is the B0 gate of the L3 PCIe ladder.  It answers exactly three
 * questions, in order:
 *
 *   1. did the root complex inherit the firmware-trained link (both
 *      controllers: pcie2x1 for the NVMe, pcie3x2 for the 7260)?
 *   2. is a downstream function claimed, and which VID:DID/class/BAR?
 *   3. does an NVMe read complete, and did its completion arrive as an ITS
 *      message interrupt (not just a polled CQE)?
 *
 * The delivery count is the point of the last step: the device posts a
 * completion into the CQ whether or not its MSI arrived, so a polled
 * completion cannot show that the ITS path works.  A nonzero delta after the
 * reads is the evidence, and the judge refuses a PASS without it.
 *
 * Anchors (the judge checks each on PASS):
 *   pcie: ctrl <n> link up, downstream bus <n>
 *   pcie: dev <bb:dd.f> <vid:did> class <cc>
 *   nvme0: <vid:did> at 0x<bar>, ns1: <blocks> blocks of <bytes> bytes
 *   nvme0: message interrupts delivered (intid <n>, <n> so far)
 *   pcie: irqs=<before>-><after> completions=<n>
 *   pcie: PASS
 *
 * An absent endpoint or an all-FF BAR is a platform verdict, not a code
 * failure: it prints `pcie: platform-inactive <reason>` and returns 2, so the
 * round can classify the boot without mistaking it for a driver bug.
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "dwc_pcie.h"
#include "dwc_nvme.h"
#include "nvme.h"

#include "pcie_cases.h"

#define PCIE_NVME_BLOCKS	8U
#define PCIE_NVME_BYTES		(PCIE_NVME_BLOCKS * 512U)
#define PCIE_READ_ATTEMPTS	4U

static uint32_t pcie_bringup_done;
static uint32_t pcie_bringup_nvme;
static uint64_t pcie_bringup_blocks;
static uint32_t pcie_bringup_sector;

static uint8_t pcie_buf[PCIE_NVME_BYTES] __attribute__((aligned(4096)));

uint32_t pcie_board_bringup(void)
{
	if (pcie_bringup_done == 0U) {
		uint32_t n;
		NVME_CAPABILITIES caps;

		dwc_pcie_init();
		dwc_nvme_init();

		n = dwc_nvme_ctrl_count();
		pcie_bringup_nvme = n;
		if (n > 0U) {
			caps = Driver_NVME.GetCapabilities(0U);
			pcie_bringup_blocks = caps.sector_count;
			pcie_bringup_sector = caps.sector_size;
		}
		pcie_bringup_done = 1U;
	}

	return pcie_bringup_nvme;
}

uint64_t pcie_ns_blocks(void)
{
	return pcie_bringup_blocks;
}

uint32_t pcie_ns_sector_size(void)
{
	return pcie_bringup_sector;
}

static void pcie_print_devices(void)
{
	uint32_t count = dwc_pcie_dev_count();
	uint32_t i;

	for (i = 0U; i < count; i++) {
		const struct dwc_pcie_dev *dev = dwc_pcie_dev(i);

		if (dev == NULL) {
			continue;
		}
		CASE_ANCHOR("pcie: dev %02x:%02x.%u %04x:%04x class %02x%02x\n",
			    (unsigned)dev->busn,
			    (unsigned)(dev->devfn >> 3),
			    (unsigned)(dev->devfn & 7U),
			    (unsigned)dev->vendor, (unsigned)dev->device,
			    (unsigned)dev->baseclass, (unsigned)dev->subclass);
	}
}

static const struct dwc_pcie_dev *pcie_find_nvme(void)
{
	uint32_t count = dwc_pcie_dev_count();
	uint32_t i;

	for (i = 0U; i < count; i++) {
		const struct dwc_pcie_dev *dev = dwc_pcie_dev(i);

		if (dev != NULL && dev->baseclass == PCI_CLASS_STORAGE &&
		    dev->subclass == PCI_SUBCLASS_NVME) {
			return dev;
		}
	}
	return NULL;
}

int case_pcie_run(void)
{
	const struct dwc_pcie_dev *nvme;
	uint32_t ctrls;
	uint32_t irqs_before;
	uint32_t irqs_after;
	uint32_t completions;
	uint32_t attempt;
	int32_t rc;

	(void)pcie_board_bringup();

	ctrls = dwc_pcie_ctrl_count();
	CASE_INFO("pcie: controllers=%u devices=%u nvme=%u\n",
		  (unsigned)ctrls, (unsigned)dwc_pcie_dev_count(),
		  (unsigned)dwc_nvme_ctrl_count());

	if (ctrls == 0U) {
		CASE_ANCHOR("pcie: platform-inactive no controller link\n");
		return 2;
	}

	pcie_print_devices();

	nvme = pcie_find_nvme();
	if (nvme == NULL || dwc_nvme_ctrl_count() == 0U) {
		CASE_ANCHOR("pcie: platform-inactive no nvme function\n");
		return 2;
	}

	if (nvme->bar[0] == 0U || nvme->bar[0] == 0xffffffffU) {
		CASE_ANCHOR("pcie: platform-inactive nvme bar0=0x%08x\n",
			    (unsigned)nvme->bar[0]);
		return 2;
	}

	if (pcie_bringup_blocks == 0U || pcie_bringup_sector == 0U) {
		CASE_ANCHOR("pcie: FAIL nvme geometry empty\n");
		return 1;
	}

	CASE_ANCHOR("nvme0: %04x:%04x at 0x%lx, ns1: %llu blocks of %u bytes\n",
		    (unsigned)nvme->vendor, (unsigned)nvme->device,
		    (unsigned long)nvme->bar[0],
		    (unsigned long long)pcie_bringup_blocks,
		    (unsigned)pcie_bringup_sector);

	/* One completion through the interrupt path.  The driver waits on the
	 * CQE and counts the delivery; the read is repeated a few times so a
	 * late first interrupt cannot turn a working ITS into a FAIL. */
	irqs_before = dwc_nvme_irq_count(0U);
	completions = 0U;
	rc = ARM_DRIVER_ERROR;
	for (attempt = 0U; attempt < PCIE_READ_ATTEMPTS; attempt++) {
		(void)memset(pcie_buf, 0, sizeof(pcie_buf));
		rc = Driver_NVME.ReadBlocks(0U, 0U, pcie_buf, PCIE_NVME_BLOCKS);
		completions = Driver_NVME.GetTransferStatus(0U);
		if (rc == ARM_DRIVER_OK && dwc_nvme_irq_count(0U) > irqs_before) {
			break;
		}
	}
	irqs_after = dwc_nvme_irq_count(0U);

	CASE_ANCHOR("nvme0: message interrupts delivered (intid %u, %u so far)\n",
		    (unsigned)dwc_nvme_irq_intid(0U), (unsigned)irqs_after);
	CASE_ANCHOR("pcie: irqs=%u->%u completions=%u\n",
		    (unsigned)irqs_before, (unsigned)irqs_after,
		    (unsigned)completions);

	if (rc != ARM_DRIVER_OK) {
		CASE_ANCHOR("pcie: FAIL block read did not complete rc=%d\n",
			    (int)rc);
		return 1;
	}
	if (irqs_after <= irqs_before) {
		CASE_ANCHOR("pcie: FAIL read completed without a message interrupt\n");
		return 1;
	}

	CASE_ANCHOR("pcie: PASS\n");
	return 0;
}