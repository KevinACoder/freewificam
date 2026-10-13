/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   dwc_pcie.c
 * @brief  Board PCIe backend: the flat device list and config access the
 *         endpoint drivers consume, over the DesignWare root-complex core.
 *
 * This is the binding layer between two things that already exist: the board
 * coordinates in board_conf.h and the register-level root-complex core in
 * pcie/pcie_dw.c. It owns no register sequence of its own - every access goes
 * through pcie_dw_conf_read/write, and the device record is a projection of
 * pcie_dw_claim_func's claim record, so there is no second enumeration.
 *
 * The endpoint drivers (nvme/dwc_nvme.c, pcie/dwc_msix.c) see only
 * dwc_pcie.h: a function list, config read/write, and the Requester ID that is
 * also the ITS DeviceID. Nothing above this layer names an iATU, a BAR window
 * or a controller register.
 *
 * @author zhugengyu
 * @date   12.10.2026
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "board_conf.h"
#include "Driver_Common.h"
#include "dwc_pcie.h"
#include "log.h"
#include "pcie_dw.h"

#define DWC_PCIE_CTRL_COUNT \
	(sizeof(dwc_pcie_ctrls) / sizeof(dwc_pcie_ctrls[0]))

/* Functions kept from the scan. One controller carries one endpoint on this
 * board (a single-device link), so this is headroom, not a target. */
#define DWC_PCIE_DEV_MAX 4

/* Board coordinates as the root-complex core wants them. The inbound window
 * spans from the image's RAM up to and including the MSI doorbell page, so an
 * endpoint can reach both its descriptors and the ITS translater. */
static const struct pcie_dw_controller dwc_pcie_ctrls[] = {
	{
		.apb_base	= BOARD_PCIE2X1_APB_BASE,
		.dbi_base	= BOARD_PCIE2X1_DBI_BASE,
		.cfg_base	= BOARD_PCIE2X1_CFG_BASE,
		.cfg_size	= BOARD_PCIE_CFG_SIZE,
		.mem_base	= BOARD_PCIE2X1_MEM_BASE,
		.mem_size	= BOARD_PCIE2X1_MEM_SIZE,
		.dma_base	= BOARD_MMU_IMAGE_RAM_BASE,
		.dma_size	= BOARD_PCIE_DOORBELL_BASE +
				  BOARD_PCIE_DOORBELL_SIZE -
				  BOARD_MMU_IMAGE_RAM_BASE,
		.doorbell_base	= BOARD_PCIE_DOORBELL_BASE,
		.doorbell_size	= BOARD_PCIE_DOORBELL_SIZE,
		.cfg_atu_idx	= 1,
		.mem_atu_idx	= 0,
	},
	{
		.apb_base	= BOARD_PCIE3X2_APB_BASE,
		.dbi_base	= BOARD_PCIE3X2_DBI_BASE,
		.cfg_base	= BOARD_PCIE3X2_CFG_BASE,
		.cfg_size	= BOARD_PCIE_CFG_SIZE,
		.mem_base	= BOARD_PCIE3X2_MEM_BASE,
		.mem_size	= BOARD_PCIE3X2_MEM_SIZE,
		.dma_base	= BOARD_MMU_IMAGE_RAM_BASE,
		.dma_size	= BOARD_PCIE_DOORBELL_BASE +
				  BOARD_PCIE_DOORBELL_SIZE -
				  BOARD_MMU_IMAGE_RAM_BASE,
		.doorbell_base	= BOARD_PCIE_DOORBELL_BASE,
		.doorbell_size	= BOARD_PCIE_DOORBELL_SIZE,
		.cfg_atu_idx	= 1,
		.mem_atu_idx	= 0,
	},
};

static struct pcie_dw_softc dwc_pcie_sc[DWC_PCIE_CTRL_COUNT];
static struct dwc_pcie_dev dwc_pcie_devs[DWC_PCIE_DEV_MAX];
static uint32_t dwc_pcie_ndevs;
static uint32_t dwc_pcie_nctrl;
static bool dwc_pcie_inited;

/* The controller that answers config accesses for a bus: the one whose
 * downstream bus this is. Bus 0 is the first controller's own root port, whose
 * config space is its DBI. */
static struct pcie_dw_softc *dwc_pcie_sc_for_bus(uint32_t bus)
{
	uint32_t i;

	for (i = 0; i < DWC_PCIE_CTRL_COUNT; i++) {
		if (dwc_pcie_sc[i].link_up &&
		    bus == dwc_pcie_sc[i].secondary_bus) {
			return &dwc_pcie_sc[i];
		}
	}

	return dwc_pcie_sc[0].link_up ? &dwc_pcie_sc[0] : NULL;
}

void dwc_pcie_init(void)
{
	uint32_t i;

	if (dwc_pcie_inited) {
		return;
	}
	dwc_pcie_inited = true;

	for (i = 0; i < DWC_PCIE_CTRL_COUNT; i++) {
		struct pcie_dw_func func;
		struct dwc_pcie_dev *d;
		uint32_t cls;

		if (pcie_dw_attach(&dwc_pcie_sc[i], &dwc_pcie_ctrls[i]) != 0) {
			board_log("pcie: ctrl %u link down, skipped\n", i);
			continue;
		}

		dwc_pcie_nctrl++;
		board_log("pcie: ctrl %u link up, downstream bus %u\n",
			  i, dwc_pcie_sc[i].secondary_bus);

		if (dwc_pcie_ndevs >= DWC_PCIE_DEV_MAX) {
			continue;
		}
		if (pcie_dw_claim_func(&dwc_pcie_sc[i], &func) != 0) {
			board_log("pcie: ctrl %u no downstream function\n", i);
			continue;
		}

		d = &dwc_pcie_devs[dwc_pcie_ndevs];
		memset(d, 0, sizeof(*d));
		d->busn = func.bus;
		d->devfn = ((uint32_t)func.device << 3) | func.function;
		d->vendor = func.vendor_id;
		d->device = func.device_id;
		d->msix_cap = (uint8_t)func.msix_cap;
		d->msi_cap = (uint8_t)func.msi_cap;

		for (uint32_t b = 0; b < func.bar_count && b < 6; b++) {
			d->bar[b] = func.bars[b].phys_addr;
			d->bar_size[b] = func.bars[b].size;
			d->bar_flags[b] = func.bars[b].flags;
		}

		/* Class is not in the claim record: byte 0x0b is the base class
		 * and 0x0a the subclass, so the 32-bit read at 0x08 carries
		 * revision[7:0], prog-if[15:8], subclass[23:16], class[31:24]. */
		cls = pcie_dw_conf_read(&dwc_pcie_sc[i], func.bus, func.device,
		    func.function, PCI_CFG_REVISION);
		d->baseclass = (uint8_t)(cls >> 24);
		d->subclass = (uint8_t)(cls >> 16);

		board_log("pcie: dev %02x:%02x.%x %04x:%04x class %02x%02x\n",
			  d->busn, func.device, func.function, d->vendor,
			  d->device, d->baseclass, d->subclass);
		dwc_pcie_ndevs++;
	}
}

uint32_t dwc_pcie_ctrl_count(void)
{
	return dwc_pcie_nctrl;
}

uint32_t dwc_pcie_dev_count(void)
{
	return dwc_pcie_ndevs;
}

const struct dwc_pcie_dev *dwc_pcie_dev(uint32_t index)
{
	return index < dwc_pcie_ndevs ? &dwc_pcie_devs[index] : NULL;
}

int32_t dwc_pcie_cfg_read(uint32_t bus, uint32_t devfn, uint32_t where,
			  uint32_t size, uint32_t *value)
{
	struct pcie_dw_softc *sc = dwc_pcie_sc_for_bus(bus);
	uint32_t word;
	uint32_t shift = (where & 3u) * 8u;

	if (sc == NULL || value == NULL) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}
	if (size != 1 && size != 2 && size != 4) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

	word = pcie_dw_conf_read(sc, bus, devfn >> 3, devfn & 7u, where);

	if (size == 4) {
		*value = word;
	} else {
		*value = (word >> shift) & (size == 1 ? 0xffu : 0xffffu);
	}

	return ARM_DRIVER_OK;
}

int32_t dwc_pcie_cfg_write(uint32_t bus, uint32_t devfn, uint32_t where,
			   uint32_t size, uint32_t value)
{
	struct pcie_dw_softc *sc = dwc_pcie_sc_for_bus(bus);
	uint32_t shift = (where & 3u) * 8u;
	uint32_t word;
	uint32_t mask;

	if (sc == NULL) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}
	if (size != 1 && size != 2 && size != 4) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

	if (size == 4) {
		word = value;
	} else {
		/* Sub-word writes are read-modify-write on the aligned word;
		 * the core exposes only word-granular config accesses. */
		mask = (size == 1) ? 0xffu : 0xffffu;
		word = pcie_dw_conf_read(sc, bus, devfn >> 3, devfn & 7u,
		    where);
		word = (word & ~(mask << shift)) | ((value & mask) << shift);
	}

	pcie_dw_conf_write(sc, bus, devfn >> 3, devfn & 7u, where, word);

	return ARM_DRIVER_OK;
}

uint32_t dwc_pcie_requester_id(const struct dwc_pcie_dev *dev)
{
	return (dev->busn << 8) | dev->devfn;
}
