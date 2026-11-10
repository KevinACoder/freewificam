/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   dwc_msix.c
 * @brief  MSI-X table programming: mask discipline, function arming, and the
 *         hand-off to the board's message-interrupt domain.
 *
 * Two things here are worth reading before changing anything:
 *
 *   - The MSI-X table lives inside one of the function's BARs, and this code
 *     addresses it as a physical address: the project's MMU identity-maps the
 *     endpoint memory windows as Device, so the table needs no mapping call.
 *     A BAR that firmware left outside the controller's MEM window would be
 *     unreachable, which is why the address is reported when MSI-X is enabled.
 *   - Nothing here is cached. The table is Device memory, so the ordering
 *     calls are `dsb` and there is deliberately no dcache maintenance: adding
 *     a flush would be cargo cult, removing a dsb would not be.
 *
 * @author zhugengyu
 * @date   16.09.2026
 */

#include <stdint.h>
#include <string.h>

#include "Driver_Common.h"
#include "arch_asm.h"
#include "log.h"
#include "msi.h"
#include "regs.h"

#include "dwc_msix.h"

static int32_t msix_ctrl_read(const struct dwc_pcie_dev *dev, uint16_t *value)
{
	uint32_t tmp;
	int32_t ret;

	ret = dwc_pcie_cfg_read(dev->busn, dev->devfn,
				(uint32_t)dev->msix_cap + PCI_MSIX_FLAGS, 2, &tmp);
	if (ret != ARM_DRIVER_OK) {
		return ret;
	}

	*value = (uint16_t)tmp;
	return ARM_DRIVER_OK;
}

static void msix_ctrl_write(const struct dwc_pcie_dev *dev, uint16_t value)
{
	(void)dwc_pcie_cfg_write(dev->busn, dev->devfn,
				 (uint32_t)dev->msix_cap + PCI_MSIX_FLAGS, 2, value);
}

/* The table's physical address: BAR `bir` plus the offset the capability
 * carries. A 64-bit BAR needs its upper half read as well; the MEM window on
 * this board is below 4GiB, but a BAR fetched from firmware config space is
 * the wrong thing to assume about. */
static int32_t msix_table_addr(const struct dwc_pcie_dev *dev, uint32_t *bir,
			       uintptr_t *table, uint32_t *entries)
{
	uint16_t control;
	uint32_t tbl;
	uint64_t base;
	int32_t ret;

	ret = msix_ctrl_read(dev, &control);
	if (ret != ARM_DRIVER_OK) {
		return ret;
	}
	*entries = (control & PCI_MSIX_FLAGS_QSIZE) + 1u;

	ret = dwc_pcie_cfg_read(dev->busn, dev->devfn,
				(uint32_t)dev->msix_cap + PCI_MSIX_TABLE, 4, &tbl);
	if (ret != ARM_DRIVER_OK) {
		return ret;
	}

	*bir = tbl & PCI_MSIX_TABLE_BIR;
	if (*bir >= 6u) {
		return ARM_DRIVER_ERROR;
	}

	base = dev->bar[*bir] & ~0xfULL;
	if ((dev->bar[*bir] & 0x6u) == 0x4u && *bir < 5u) {	/* 64-bit BAR */
		base |= (uint64_t)dev->bar[*bir + 1u] << 32;
	}

	*table = (uintptr_t)(base + (tbl & PCI_MSIX_TABLE_OFFSET));

	return ARM_DRIVER_OK;
}

/* Write one table entry: mask, order, address/data triple, order, restore the
 * previous mask state. */
static void msix_write_entry(uintptr_t entry, const MSI_VECTOR *vec)
{
	uint32_t ctrl;

	ctrl = reg_rd32(entry + PCI_MSIX_ENTRY_VECTOR_CTRL);
	reg_wr32(entry + PCI_MSIX_ENTRY_VECTOR_CTRL,
		 ctrl | PCI_MSIX_ENTRY_CTRL_MASKBIT);
	ARCH_DSB();

	reg_wr32(entry + PCI_MSIX_ENTRY_LOWER_ADDR, (uint32_t)vec->address);
	reg_wr32(entry + PCI_MSIX_ENTRY_UPPER_ADDR,
		 (uint32_t)(vec->address >> 32));
	reg_wr32(entry + PCI_MSIX_ENTRY_DATA, vec->data);
	ARCH_DSB();

	reg_wr32(entry + PCI_MSIX_ENTRY_VECTOR_CTRL, ctrl);
	ARCH_DSB();
}

int32_t dwc_msix_alloc(const struct dwc_pcie_dev *dev, uint32_t nvec_max,
		       MSI_VECTOR *vectors)
{
	const MSI_DOMAIN *domain;
	uintptr_t table;
	uint32_t bir;
	uint32_t entries;
	uint32_t i;
	uint16_t ctrl = 0;
	int32_t count;
	int32_t ret;

	if (dev == NULL || vectors == NULL || nvec_max == 0) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}
	if (dev->msix_cap == 0) {
		return ARM_DRIVER_ERROR_UNSUPPORTED;
	}

	ret = msix_table_addr(dev, &bir, &table, &entries);
	if (ret != ARM_DRIVER_OK) {
		return ret;
	}
	if (nvec_max > entries) {
		nvec_max = entries;
	}
	if (nvec_max > DWC_MSIX_VECTOR_MAX) {
		nvec_max = DWC_MSIX_VECTOR_MAX;
	}

	/* MSI-X stays disabled while the table and the ITS are programmed. */
	(void)msix_ctrl_read(dev, &ctrl);
	msix_ctrl_write(dev, (uint16_t)(ctrl & ~PCI_MSIX_FLAGS_ENABLE));

	domain = msi_domain_get();
	if (domain == NULL) {
		board_log("msix: no message interrupt domain available\n");
		return ARM_DRIVER_ERROR_UNSUPPORTED;
	}

	count = domain->Allocate(dwc_pcie_requester_id(dev), nvec_max, vectors);
	if (count < 1) {
		board_log("msix: %04x:%04x got no vectors\n", dev->vendor,
			  dev->device);
		return ARM_DRIVER_ERROR;
	}
	if ((uint32_t)count > entries) {
		/* The domain promised more than the table can carry: the extra
		 * event ids would be unmapped again on release, so refuse now
		 * rather than leave live translations behind. */
		domain->Free(dwc_pcie_requester_id(dev));
		return ARM_DRIVER_ERROR;
	}

	/* The ITS event id and the table index must be the same number: the
	 * glue maps events 0..n-1 and the payload written below has to select
	 * the entry the device will raise. A domain that numbers its events
	 * differently is a domain this code cannot program. */
	for (i = 0; i < (uint32_t)count; i++) {
		if (vectors[i].data != i) {
			board_log("msix: vector %u carries event id %u,"
				  " table index would not match\n", i, vectors[i].data);
			domain->Free(dwc_pcie_requester_id(dev));
			return ARM_DRIVER_ERROR;
		}
		msix_write_entry(table + (uintptr_t)i * PCI_MSIX_ENTRY_SIZE,
				 &vectors[i]);
	}

	/* Function masked while the entries are armed, then every entry
	 * unmasked, then ENABLE raised - all in that order. */
	(void)msix_ctrl_read(dev, &ctrl);
	msix_ctrl_write(dev, (uint16_t)((ctrl & ~PCI_MSIX_FLAGS_ENABLE) |
					PCI_MSIX_FLAGS_MASKALL));

	for (i = 0; i < (uint32_t)count; i++) {
		uintptr_t entry = table + (uintptr_t)i * PCI_MSIX_ENTRY_SIZE;

		reg_wr32(entry + PCI_MSIX_ENTRY_VECTOR_CTRL, 0);
	}
	ARCH_DSB();

	(void)msix_ctrl_read(dev, &ctrl);
	msix_ctrl_write(dev, (uint16_t)((ctrl & ~PCI_MSIX_FLAGS_MASKALL) |
					PCI_MSIX_FLAGS_ENABLE));
	ARCH_DSB();

	board_log("msix: %04x:%04x rid %04x, %d vector(s), table BAR%u+0x%lx"
		  " (%lu entries)\n",
		  dev->vendor, dev->device, dwc_pcie_requester_id(dev), count, bir,
		  (unsigned long)(table - (dev->bar[bir] & ~0xfULL)),
		  (unsigned long)entries);

	return count;
}

int32_t dwc_msix_release(const struct dwc_pcie_dev *dev)
{
	const MSI_DOMAIN *domain;
	uint16_t ctrl = 0;

	if (dev == NULL || dev->msix_cap == 0) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

	(void)msix_ctrl_read(dev, &ctrl);
	ctrl &= (uint16_t)~PCI_MSIX_FLAGS_ENABLE;
	ctrl |= PCI_MSIX_FLAGS_MASKALL;
	msix_ctrl_write(dev, ctrl);
	ARCH_DSB();

	domain = msi_domain_get();
	if (domain != NULL) {
		domain->Free(dwc_pcie_requester_id(dev));
	}

	return ARM_DRIVER_OK;
}