/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   pcie_ecam.h
 * @brief  First-party ECAM PCIe bus driver (firefly family): probe-only
 *         enumeration of the firmware-trained tree, plus the flat bus face
 *         the endpoint drivers (NVMe today) and MSI-X programming need.
 *
 * The firefly root complex is ECAM (PCI Express Base Specification, "Enhanced
 * Configuration Access Mechanism"): the whole hierarchy's config space is
 * memory-mapped at BOARD_PCIE_ECAM_BASE, one function per 4KiB page, and the
 * firmware (U-Boot) has already trained the links, assigned the bus numbers
 * and programmed the BARs.
 *
 * TWO DISCIPLINES, ONE FILE:
 *
 *   - ENUMERATION STAYS READ-ONLY. The walk records the tree the firmware
 *     left: no bus renumbering, no BAR sizing or programming, nothing. The
 *     rounds that own resource assignment extend this file's write path for
 *     that; the enumeration core never writes.
 *
 *   - THE WRITE PATH EXISTS FOR ENDPOINT DRIVERS ONLY. pcie_ecam_cfg_write
 *     exists because a function driver legitimately owns a few of its own
 *     config registers (the command enable, the MSI/MSI-X control words).
 *     A write to any bus-assignment or BAR register through this face is a
 *     layering violation: refuse it in review, not at runtime.
 *
 * @design exemplars:
 *   - Linux drivers/pci/ecam.c (the ECAM address layout: bus << 20 |
 *     dev << 15 | fn << 12, 32-bit accesses only);
 *   - Linux drivers/pci/probe.c (the scan shape: a function answers with a
 *     vendor id other than 0xffff, type-1 headers carry the secondary bus,
 *     the walk recurses into it - pci_scan_child_bus_extend);
 *   - NetBSD sys/dev/pci/pci.c (probe-only reuse of firmware assignment).
 * The flat bus face (device list + sized config access + requester id) is the
 * shape the project's first carrier line established for its DesignWare
 * controller, so endpoint drivers and MSI-X programming bind identically on
 * both bus flavours.
 */

#ifndef FREEWIFICAM_PCIE_ECAM_H
#define FREEWIFICAM_PCIE_ECAM_H

#include <stdbool.h>
#include <stdint.h>

#include "Driver_Common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One recorded base-address register, as the firmware assigned it. The size
 * is NOT recovered (sizing needs the write-all-ones dance, which this driver
 * refuses to do to a live assignment); the address and the flags are read
 * back verbatim. */
struct pcie_ecam_bar {
	uint64_t	base;		/* assigned base address (0 = unassigned) */
	uint32_t	flags;		/* raw lower dword of the BAR */
	bool		is_64;		/* this BAR is the low half of a 64-bit one */
	bool		present;	/* a nonzero assignment the firmware made */
};

#define PCIE_ECAM_BARS		6

/* One claimed function of the tree. */
struct pcie_ecam_func {
	uint8_t		bus;
	uint8_t		dev;
	uint8_t		fn;
	uint16_t	vendor_id;
	uint16_t	device_id;
	uint32_t	class_code;	/* 24-bit class/subclass/prog-if */
	uint8_t		header_type;	/* raw, bits 6:0 = layout, bit 7 = multi-fn */
	bool		is_bridge;
	uint8_t		secondary_bus;	/* bridges: the bus behind (0 = none) */
	uint16_t	command;	/* raw command register as firmware left it */
	uint16_t	msi_cap;	/* config offset of the MSI capability, 0 = none */
	uint16_t	msix_cap;	/* config offset of the MSI-X capability, 0 = none */
	struct pcie_ecam_bar bars[PCIE_ECAM_BARS];
};

/* Walk the firmware's tree (probe-only, idempotent). Returns the number of
 * claimed functions, or a negative ARM_DRIVER_ERROR*. The result stays
 * queryable through the accessors below until the next call. */
int32_t pcie_ecam_init(void);

/* Functions the last walk claimed, in discovery order. */
uint32_t pcie_ecam_dev_count(void);
const struct pcie_ecam_func *pcie_ecam_dev(uint32_t index);

/* First function at or after `from` whose class_code & mask equals class &
 * mask (mask 0xffffff matches the whole class, 0xffff00 ignores prog-if).
 * NULL when none is left - the NVMe round's class 0108h lookup. */
const struct pcie_ecam_func *pcie_ecam_find_class(uint32_t from,
						  uint32_t class_code,
						  uint32_t mask);

/* Config space access over the ECAM window. `where` must be naturally
 * aligned to `size` (1, 2 or 4; `value` right-justified). Reads are the
 * enumeration primitive; writes are for a function's OWN registers (command
 * enable, MSI/MSI-X control) - see the file header's second discipline.
 * Returns ARM_DRIVER_OK or a negative ARM_DRIVER_ERROR*. */
int32_t pcie_ecam_cfg_read(uint8_t bus, uint8_t dev, uint8_t fn,
			   uint16_t where, uint32_t size, uint32_t *value);
int32_t pcie_ecam_cfg_write(uint8_t bus, uint8_t dev, uint8_t fn,
			    uint16_t where, uint32_t size, uint32_t value);

/* The function's PCIe Requester ID (bus << 8 | dev << 3 | fn): the GICv3
 * ITS DeviceID used for its message interrupts. */
uint32_t pcie_ecam_requester_id(const struct pcie_ecam_func *f);

/* One line per claimed function, one summary line: the transcript the board
 * round files against the lab's U-Boot enumeration. */
void pcie_ecam_dump(void);

/* --- config register offsets this layer and pcie_msix.c use --------------- */

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
#define PCI_CAP_LIST_NEXT	0x01	/* next-pointer byte, cap-relative */

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

/* MSI capability layout (the non-X form), relative to the capability offset. */
#define PCI_MSI_FLAGS		0x02
#define PCI_MSI_ADDR_LOW	0x04
#define PCI_MSI_ADDR_HIGH	0x08
#define PCI_MSI_DATA_32		0x08
#define PCI_MSI_DATA_64		0x0c
#define PCI_MSI_MASK_32		0x0c
#define PCI_MSI_MASK_64		0x10
#define PCI_MSI_FLAGS_ENABLE	(1u << 0)
#define PCI_MSI_FLAGS_64BIT	(1u << 7)
#define PCI_MSI_FLAGS_MASK	(1u << 8)
#define PCI_MSI_MME_SHIFT	4
#define PCI_MSI_MME_MASK	0x7u

#ifdef __cplusplus
}
#endif

#endif /* FREEWIFICAM_PCIE_ECAM_H */
