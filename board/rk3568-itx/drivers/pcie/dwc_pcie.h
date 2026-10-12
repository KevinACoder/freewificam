/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   dwc_pcie.h
 * @brief  Flat PCI-access shape for the endpoint drivers: a small enumerated
 *         device list plus config-space read/write, implemented over the
 *         DesignWare root-complex core (pcie/pcie_dw.c).
 *
 * Not an interface layer: the rest of the project sees the board layer, not
 * this. It exists for the two things that need more than a device list offers:
 *
 *   - dwc_msix.c, which programs an endpoint's MSI-X table and so needs the
 *     function's BARs and capability offsets, and the Requester ID (which is
 *     the GICv3 ITS DeviceID);
 *   - dwc_nvme.c, which has to find its controller by class code before it can
 *     talk to it.
 *
 * PAIRING. In this line the shape is implemented on top of the board's
 * DesignWare root-complex core (pcie_dw.h):
 *
 *   dwc_pcie_cfg_read/write  -> pcie_dw_conf_read/write
 *   the enumerated device    -> pcie_dw_claim_func (IDs, revision, bus/dev/fn,
 *                               MSI/PCIe capability offsets, the BARs, and the
 *                               memory + bus-master enable)
 *   dwc_pcie_requester_id    -> bus << 8 | dev << 3 | fn, the ITS DeviceID
 *
 * The struct below therefore carries exactly the fields that pairing can fill,
 * so the flat list is a projection of the root complex's own claim record
 * rather than a second enumeration.
 */

#ifndef FREEWIFICAM_DWC_PCIE_H
#define FREEWIFICAM_DWC_PCIE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One enumerated function. The BARs hold what the firmware assigned; this
 * layer never writes a BAR (sizing one would have to overwrite it). */
struct dwc_pcie_dev {
	uint32_t busn;		/* bus the function sits on */
	uint32_t devfn;		/* dev << 3 | fn */
	uint16_t vendor;
	uint16_t device;
	uint8_t baseclass;
	uint8_t subclass;
	uint8_t msix_cap;	/* config offset of the MSI-X capability, 0 = none */
	uint8_t msi_cap;	/* config offset of the MSI capability, 0 = none */
	uint32_t bar[6];
};

/* Bring the root complex up and scan the downstream bus. Idempotent. */
void dwc_pcie_init(void);

/* Controllers whose firmware-trained link was inherited by dwc_pcie_init(). */
uint32_t dwc_pcie_ctrl_count(void);

/* Functions found by the last dwc_pcie_init(), and one by index (NULL when the
 * index is out of range). */
uint32_t dwc_pcie_dev_count(void);
const struct dwc_pcie_dev *dwc_pcie_dev(uint32_t index);

/* Config space access. size is 1, 2 or 4; `value` is right-justified (the same
 * convention the root complex core uses). Returns ARM_DRIVER_OK or a negative
 * ARM_DRIVER_ERROR*. */
int32_t dwc_pcie_cfg_read(uint32_t bus, uint32_t devfn, uint32_t where,
			  uint32_t size, uint32_t *value);
int32_t dwc_pcie_cfg_write(uint32_t bus, uint32_t devfn, uint32_t where,
			   uint32_t size, uint32_t value);

/* The function's PCIe Requester ID (bus << 8 | dev << 3 | fn), which is the
 * ITS DeviceID used for its message interrupts. */
uint32_t dwc_pcie_requester_id(const struct dwc_pcie_dev *dev);

/* Config space register offsets this layer and dwc_msix.c use. */
#define PCI_CFG_VENDOR_ID	0x00
#define PCI_CFG_COMMAND		0x04
#define PCI_CFG_REVISION	0x08
#define PCI_CFG_CLASS		0x0b	/* base class byte; subclass at 0x0a */
#define PCI_CFG_HEADER_TYPE	0x0e
#define PCI_CFG_CAP_PTR		0x34
#define PCI_CFG_BAR0		0x10

#define PCI_CMD_MEMORY		(1u << 1)
#define PCI_CMD_MASTER		(1u << 2)

#define PCI_CLASS_STORAGE	0x01
#define PCI_SUBCLASS_NVME	0x08

#define PCI_CAP_ID_MSI		0x05
#define PCI_CAP_ID_MSIX		0x11

/* MSI-X capability layout, relative to the capability offset. */
#define PCI_MSIX_FLAGS		0x02	/* 16 bit: ENABLE, MASKALL, table size */
#define PCI_MSIX_TABLE		0x04	/* 32 bit: BIR [2:0], offset [31:3] */
#define PCI_MSIX_FLAGS_ENABLE	(1u << 15)
#define PCI_MSIX_FLAGS_MASKALL	(1u << 14)
#define PCI_MSIX_FLAGS_QSIZE	0x07ffu
#define PCI_MSIX_TABLE_BIR	0x00000007u
#define PCI_MSIX_TABLE_OFFSET	0xfffffff8u

/* One MSI-X table entry, 16 bytes. */
#define PCI_MSIX_ENTRY_SIZE		16
#define PCI_MSIX_ENTRY_LOWER_ADDR	0x00
#define PCI_MSIX_ENTRY_UPPER_ADDR	0x04
#define PCI_MSIX_ENTRY_DATA		0x08
#define PCI_MSIX_ENTRY_VECTOR_CTRL	0x0c
#define PCI_MSIX_ENTRY_CTRL_MASKBIT	0x00000001u

#ifdef __cplusplus
}
#endif

#endif /* FREEWIFICAM_DWC_PCIE_H */
