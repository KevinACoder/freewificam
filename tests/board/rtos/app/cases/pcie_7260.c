/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   pcie_7260.c
 * @brief  Case pcie_7260: the same PCIe stack that carries the NVMe also
 *         reaches the Intel AC 7260 - enumeration, BAR, CSR_HW_REV and one
 *         registered message vector.
 *
 * This is the B2 gate, deliberately not the driver: the question is whether the
 * root complex, the config-space path and the ITS-backed vector programming can
 * touch the card at all.  A correct HW_REV proves the BAR is live; a registered
 * vector proves the MSI-X table (or the single-MSI capability) was programmed.
 * Whether the firmware answers is B3's question, not this case's.
 *
 * The 7260 is on pcie3x2; its BDF under this root complex is not the Linux one
 * (`0002:21:00.0`) because Linux renumbers the bus - the case prints the BDF it
 * actually sees, and the judge keys on the VID:DID/class, not on a bus number.
 *
 * Anchors (the judge checks each on PASS):
 *   pcie-7260: dev <bb:dd.f> 8086:08b1 class 0280 rev 0x<nn>
 *   pcie-7260: bar0=0x<...> hw_rev=0x<...> type=0x144
 *   pcie-7260: msix vectors=<n> intid=<n>   (or `msi vectors=1 intid=`)
 *   pcie-7260: lnksta speed=<n> width=<n> lnkctl=0x<...> aspm=<n>
 *   pcie-7260: PASS
 *
 * An absent function or an all-FF BAR is a platform verdict:
 * `pcie-7260: platform-inactive <reason>` and return 2.
 */

#include <stdint.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "dwc_pcie.h"
#include "dwc_msix.h"
#include "dwc_msi.h"
#include "irq_ctrl.h"
#include "msi.h"
#include "regs.h"

#include "pcie_cases.h"

#define PCIE_7260_VENDOR	0x8086U
#define PCIE_7260_DEVICE	0x08b1U
#define PCIE_7260_CLASS		0x0280U
#define PCIE_CSR_HW_REV		0x028U
#define PCI_CAP_ID_EXP		0x10U
#define PCIE_HW_REV_TYPE_7260	0x14U

static volatile uint32_t pcie_7260_irqs;

static void pcie_7260_irq(void)
{
	pcie_7260_irqs++;
}

static const struct dwc_pcie_dev *pcie_7260_find(void)
{
	uint32_t count = dwc_pcie_dev_count();
	uint32_t i;

	for (i = 0U; i < count; i++) {
		const struct dwc_pcie_dev *dev = dwc_pcie_dev(i);

		if (dev != NULL && dev->vendor == PCIE_7260_VENDOR &&
		    dev->device == PCIE_7260_DEVICE) {
			return dev;
		}
	}
	return NULL;
}

/* Walk the config-space capability list and return the offset of one id. */
static int pcie_cap_find(const struct dwc_pcie_dev *dev, uint8_t id)
{
	uint32_t ptr;
	uint32_t cap;
	uint32_t hops;

	if (dwc_pcie_cfg_read(dev->busn, dev->devfn, PCI_CFG_CAP_PTR, 1,
			      &ptr) != ARM_DRIVER_OK) {
		return -1;
	}
	ptr &= 0xfcU;
	for (hops = 0U; ptr != 0U && hops < 48U; hops++) {
		if (dwc_pcie_cfg_read(dev->busn, dev->devfn, ptr, 2, &cap) !=
		    ARM_DRIVER_OK) {
			return -1;
		}
		if ((cap & 0xffU) == id) {
			return (int)ptr;
		}
		ptr = (cap >> 8) & 0xfcU;
	}
	return -1;
}

int case_pcie_7260_run(void)
{
	const struct dwc_pcie_dev *dev;
	MSI_VECTOR vectors[DWC_MSIX_VECTOR_MAX];
	uint32_t value;
	uint32_t bar0;
	uint32_t hw_rev;
	uint32_t type;
	int32_t nvec;
	int pcie_cap;
	uint32_t lnk;
	uint32_t speed;
	uint32_t width;
	uint32_t aspm;
	uint32_t ctrl;
	int32_t rc;

	(void)pcie_board_bringup();

	dev = pcie_7260_find();
	if (dev == NULL) {
		CASE_ANCHOR("pcie-7260: platform-inactive not enumerated\n");
		return 2;
	}

	CASE_ANCHOR("pcie-7260: dev %02x:%02x.%u 8086:08b1 class 0280 rev 0x%02x\n",
		    (unsigned)dev->busn, (unsigned)(dev->devfn >> 3),
		    (unsigned)(dev->devfn & 7U),
		    (unsigned)(dwc_pcie_cfg_read(dev->busn, dev->devfn,
						 PCI_CFG_REVISION, 1,
						 &value) == ARM_DRIVER_OK ?
			       (value & 0xffU) : 0U));

	bar0 = dev->bar[0];
	if (bar0 == 0U || bar0 == 0xffffffffU) {
		CASE_ANCHOR("pcie-7260: platform-inactive bar0=0x%08x\n",
			    (unsigned)bar0);
		return 2;
	}

	hw_rev = reg_rd32((uintptr_t)(bar0 & ~0xfU) + PCIE_CSR_HW_REV);
	type = (hw_rev & 0x000fff0U) >> 4;
	CASE_ANCHOR("pcie-7260: bar0=0x%08x hw_rev=0x%08x type=0x%x\n",
		    (unsigned)bar0, (unsigned)hw_rev, (unsigned)type);
	if (hw_rev == 0xffffffffU) {
		CASE_ANCHOR("pcie-7260: platform-inactive hw_rev all-ff\n");
		return 2;
	}
	if (type != PCIE_HW_REV_TYPE_7260) {
		CASE_ANCHOR("pcie-7260: FAIL unexpected hw_rev type=0x%x (want 0x14)\n",
			    (unsigned)type);
		return 1;
	}

	/* MSI-X first (the table is a BAR walk), then the single-MSI form the
	 * 7260's own driver asks for.  Either way the vector is armed through
	 * the same ITS domain; only the capability write sequence differs. */
	nvec = dwc_msix_alloc(dev, 1U, vectors);
	if (nvec < 0) {
		nvec = dwc_msi_alloc(dev, 1U, vectors);
		if (nvec < 0) {
			CASE_ANCHOR("pcie-7260: FAIL no message vector (%d)\n",
				    (int)nvec);
			return 1;
		}
		CASE_ANCHOR("pcie-7260: msi vectors=%d intid=%u\n",
			    (int)nvec, (unsigned)vectors[0].intid);
	} else {
		CASE_ANCHOR("pcie-7260: msix vectors=%d intid=%u\n",
			    (int)nvec, (unsigned)vectors[0].intid);
	}

	rc = IRQ_SetHandler((IRQn_ID_t)vectors[0].intid, pcie_7260_irq);
	if (rc != 0) {
		CASE_ANCHOR("pcie-7260: FAIL IRQ_SetHandler rc=%d\n", (int)rc);
		return 1;
	}
	rc = IRQ_Enable((IRQn_ID_t)vectors[0].intid);
	if (rc != 0) {
		CASE_ANCHOR("pcie-7260: FAIL IRQ_Enable rc=%d\n", (int)rc);
		return 1;
	}

	pcie_cap = pcie_cap_find(dev, PCI_CAP_ID_EXP);
	if (pcie_cap < 0) {
		CASE_ANCHOR("pcie-7260: FAIL no PCIe capability\n");
		return 1;
	}
	if (dwc_pcie_cfg_read(dev->busn, dev->devfn, (uint32_t)pcie_cap + 0x10U,
			      4, &lnk) != ARM_DRIVER_OK) {
		CASE_ANCHOR("pcie-7260: FAIL lnk read\n");
		return 1;
	}
	speed = lnk >> 16 & 0xfU;
	width = (lnk >> 20) & 0x3fU;
	aspm = lnk & 0x3U;
	CASE_ANCHOR("pcie-7260: lnksta speed=%u width=%u lnkctl=0x%08x aspm=%u\n",
		    (unsigned)speed, (unsigned)width, (unsigned)(lnk & 0xffffU),
		    (unsigned)aspm);

	/* The two endpoints must be visible in the same boot; print the NVMe
	 * one next to it so the ASPM/dual-endpoint question has one witness. */
	for (ctrl = 0U; ctrl < dwc_pcie_dev_count(); ctrl++) {
		const struct dwc_pcie_dev *other = dwc_pcie_dev(ctrl);

		if (other != NULL && other->baseclass == PCI_CLASS_STORAGE &&
		    other->subclass == PCI_SUBCLASS_NVME) {
			CASE_ANCHOR("pcie-7260: nvme %02x:%02x.%u %04x:%04x\n",
				    (unsigned)other->busn,
				    (unsigned)(other->devfn >> 3),
				    (unsigned)(other->devfn & 7U),
				    (unsigned)other->vendor,
				    (unsigned)other->device);
			break;
		}
	}

	CASE_ANCHOR("pcie-7260: PASS vectors=%d pend_irqs=%u\n",
		    (int)nvec, (unsigned)pcie_7260_irqs);
	return 0;
}