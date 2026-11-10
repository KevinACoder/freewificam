/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   dwc_msi.c
 * @brief  MSI (the non-X form) programming: the message-interrupt domain's
 *         vectors written into the endpoint's capability registers.
 *
 * Read dwc_msix.c first: this is the same hand-off in the other shape. MSI-X
 * arms a table inside a BAR; plain MSI arms one address and one data word in the
 * capability itself, so there is no Device-memory walk and no `dsb` ordering -
 * every write here is a config-space cycle, which the root complex orders for us.
 *
 * The one discipline worth stating: MSI is disabled while the address and data
 * are programmed and enabled once they are in place. A function that sees ENABLE
 * rise samples what it finds, so the order is load-bearing in the same way the
 * MSI-X mask sequence is.
 *
 * @author zhugengyu
 * @date   12.10.2026
 */

#include <stdint.h>

#include "Driver_Common.h"
#include "log.h"
#include "msi.h"

#include "dwc_msi.h"

static int32_t msi_ctrl_read(const struct dwc_pcie_dev *dev, uint16_t *value)
{
	uint32_t tmp;
	int32_t ret;

	ret = dwc_pcie_cfg_read(dev->busn, dev->devfn,
				(uint32_t)dev->msi_cap + PCI_MSI_FLAGS, 2, &tmp);
	if (ret != ARM_DRIVER_OK) {
		return ret;
	}

	*value = (uint16_t)tmp;
	return ARM_DRIVER_OK;
}

static int32_t msi_ctrl_write(const struct dwc_pcie_dev *dev, uint16_t value)
{
	return dwc_pcie_cfg_write(dev->busn, dev->devfn,
				  (uint32_t)dev->msi_cap + PCI_MSI_FLAGS, 2, value);
}

/* How many messages a count of `n` means to the capability: MME encodes a power
 * of two, so anything else cannot be expressed and must be refused rather than
 * silently rounded down (the device would then raise an event id nothing mapped). */
static int32_t msi_mme_for(uint32_t n, uint32_t *mme)
{
	uint32_t e;

	for (e = 0; e < 32u; e++) {
		if ((1u << e) >= n) {
			break;
		}
	}
	if ((1u << e) != n || e > PCI_MSI_MME_MASK) {
		return ARM_DRIVER_ERROR;
	}
	*mme = e;
	return ARM_DRIVER_OK;
}

int32_t dwc_msi_alloc(const struct dwc_pcie_dev *dev, uint32_t nvec_max,
		      MSI_VECTOR *vectors)
{
	const MSI_DOMAIN *domain;
	uint32_t rid;
	uint32_t mme;
	uint16_t ctrl = 0;
	uint32_t count;
	uint32_t i;
	int32_t ret;

	if (dev == NULL || vectors == NULL || nvec_max == 0) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}
	if (dev->msi_cap == 0) {
		return ARM_DRIVER_ERROR_UNSUPPORTED;
	}

	ret = msi_ctrl_read(dev, &ctrl);
	if (ret != ARM_DRIVER_OK) {
		return ret;
	}
	if (nvec_max > DWC_MSI_VECTOR_MAX) {
		nvec_max = DWC_MSI_VECTOR_MAX;
	}

	/* The capability is left disabled while the domain maps the vectors and the
	 * address and data are programmed. */
	(void)msi_ctrl_write(dev, (uint16_t)(ctrl & ~PCI_MSI_FLAGS_ENABLE));

	domain = msi_domain_get();
	if (domain == NULL) {
		board_log("msi: no message interrupt domain available\n");
		return ARM_DRIVER_ERROR_UNSUPPORTED;
	}

	rid = dwc_pcie_requester_id(dev);
	count = (uint32_t)domain->Allocate(rid, nvec_max, vectors);
	if ((int32_t)count < 1) {
		board_log("msi: %04x:%04x got no vectors\n", dev->vendor,
			  dev->device);
		return ARM_DRIVER_ERROR;
	}

	/* The ITS event id is the number the endpoint must raise, and the domain
	 * numbers events from zero, so vector i carries event id i. A domain that
	 * numbers them differently is one this code cannot program. */
	for (i = 0; i < count; i++) {
		if (vectors[i].data != i) {
			board_log("msi: vector %u carries event id %u,"
				  " not its index\n", i, vectors[i].data);
			domain->Free(rid);
			return ARM_DRIVER_ERROR;
		}
	}

	ret = msi_mme_for(count, &mme);
	if (ret != ARM_DRIVER_OK) {
		board_log("msi: %04x:%04x asked for %u vectors,"
			  " which the capability cannot express\n",
			  dev->vendor, dev->device, count);
		domain->Free(rid);
		return ARM_DRIVER_ERROR;
	}

	/* Message address, then data, then enable: the function samples the triple
	 * when ENABLE rises. */
	ret = dwc_pcie_cfg_write(dev->busn, dev->devfn,
				 (uint32_t)dev->msi_cap + PCI_MSI_ADDR_LOW, 4,
				 (uint32_t)vectors[0].address);
	if (ret != ARM_DRIVER_OK) {
		domain->Free(rid);
		return ret;
	}
	if ((ctrl & PCI_MSI_FLAGS_64BIT) != 0) {
		ret = dwc_pcie_cfg_write(dev->busn, dev->devfn,
					 (uint32_t)dev->msi_cap + PCI_MSI_ADDR_HIGH,
					 4, (uint32_t)(vectors[0].address >> 32));
		if (ret != ARM_DRIVER_OK) {
			domain->Free(rid);
			return ret;
		}
	}
	ret = dwc_pcie_cfg_write(dev->busn, dev->devfn,
				 (uint32_t)dev->msi_cap +
				 ((ctrl & PCI_MSI_FLAGS_64BIT) != 0 ?
				  PCI_MSI_DATA_64 : PCI_MSI_DATA_32), 2,
				 (uint16_t)vectors[0].data);
	if (ret != ARM_DRIVER_OK) {
		domain->Free(rid);
		return ret;
	}

	ctrl &= (uint16_t)~(PCI_MSI_FLAGS_ENABLE |
			    (PCI_MSI_MME_MASK << PCI_MSI_MME_SHIFT));
	ctrl |= (uint16_t)(PCI_MSI_FLAGS_ENABLE |
			   (mme << PCI_MSI_MME_SHIFT));
	ret = msi_ctrl_write(dev, ctrl);
	if (ret != ARM_DRIVER_OK) {
		domain->Free(rid);
		return ret;
	}

	board_log("msi: %04x:%04x rid %04x, %u vector(s), addr %#llx data %u\n",
		  dev->vendor, dev->device, rid, count,
		  (unsigned long long)vectors[0].address, vectors[0].data);

	return (int32_t)count;
}

int32_t dwc_msi_release(const struct dwc_pcie_dev *dev)
{
	const MSI_DOMAIN *domain;
	uint16_t ctrl = 0;

	if (dev == NULL || dev->msi_cap == 0) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

	if (msi_ctrl_read(dev, &ctrl) == ARM_DRIVER_OK) {
		(void)msi_ctrl_write(dev, (uint16_t)(ctrl & ~PCI_MSI_FLAGS_ENABLE));
	}

	domain = msi_domain_get();
	if (domain != NULL) {
		domain->Free(dwc_pcie_requester_id(dev));
	}

	return ARM_DRIVER_OK;
}
