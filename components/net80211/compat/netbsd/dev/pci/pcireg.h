/*
 * @file
 * @brief PCI configuration-space registers used by the imported
 * drivers; values follow NetBSD dev/pci/pcireg.h.
 */

#ifndef _COMPAT_DEV_PCI_PCIREG_H_
#define _COMPAT_DEV_PCI_PCIREG_H_

#include <sys/cdefs.h>
#include <stdint.h>

typedef uint32_t pcireg_t;

#define PCI_ID_REG 0x00
#define PCI_COMMAND_STATUS_REG 0x04
#define PCI_COMMAND_MASTER_ENABLE __BIT(2)
#define PCI_COMMAND_INTERRUPT_DISABLE __BIT(10)
#define PCI_STATUS_INTERRUPT __BIT(3)

#define PCI_MAPREG_START 0x10

#define PCI_CAPLISTPTR_REG 0x34
#define PCI_CAPLIST_PTR(cr) ((cr) & 0xfffc)
#define PCI_CAPLIST_CAP(cr) ((cr) & 0xff)
#define PCI_CAP_PCIEXPRESS 0x10

#define PCI_VENDOR_ID_MASK __BITS(15, 0)
#define PCI_PRODUCT_ID_MASK __BITS(31, 16)

#define PCI_VENDOR(reg) ((reg) & 0xffff)
#define PCI_PRODUCT(reg) (((reg) >> 16) & 0xffff)

#define PCI_VENDOR_INTEL 0x8086

/* PCIe link control/status register inside the PCIe capability */
#define PCIE_LCSR 0x12
#define PCIE_LCSR_ASPM_L1 __BIT(3)

#endif /* _COMPAT_DEV_PCI_PCIREG_H_ */
