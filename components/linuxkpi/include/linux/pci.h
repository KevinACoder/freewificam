/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/pci.h
 * @brief   The PCI record, the driver record and the config / resource /
 *          interrupt calls a PCI driver names.
 *
 * @design ports & adapters, the driver-facing side. This face is what a driver
 *         compiles against; the engine behind it (components/linuxkpi/pci/
 *         linuxkpi_pci.c) reaches the host only through the narrow seam in
 *         components/linuxkpi/pci/lkpi_pcie_backend.h, and the board adapter is
 *         the only party that knows a register or an address window. Members and
 *         signatures are transcribed from the interface (include/linux/pci.h,
 *         include/linux/mod_devicetable.h); the face grows per driver - a member
 *         arrives when a compiled consumer names it, never from memory.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_PCI_H_
#define _LINUXKPI_LINUX_PCI_H_

#include "../../linuxkpi_core.h"
#include <linux/device/driver.h>
#include <linux/device-id/pci.h>

/* [LNX] include/linux/pci.h:38: the "match anything" id. */
#define	PCI_ANY_ID		(~0U)

/* [LNX] include/linux/pci.h:63-64: the standard BAR count. */
#define	PCI_STD_NUM_BARS	6

/* [LNX] include/linux/pci.h:197-201: the config-space capability ids this face
 * names (a driver walks the list with pci_find_capability). */
#define	PCI_CAP_ID_PM		0x01
#define	PCI_CAP_ID_MSI		0x05
#define	PCI_CAP_ID_PCIX		0x07
#define	PCI_CAP_ID_EXP		0x10
#define	PCI_CAP_ID_MSIX		0x11

/* [LNX] include/linux/pci.h:100-103: the two command bits the enable and
 * bus-master calls maintain. */
#define	PCI_COMMAND		0x04
#define	PCI_COMMAND_MEMORY	0x2
#define	PCI_COMMAND_MASTER	0x4

/* [LNX] include/linux/ioport.h:24-25: the resource flags a BAR carries. */
#define	IORESOURCE_IO		0x00000100
#define	IORESOURCE_MEM		0x00000200

/* [LNX] include/linux/ioport.h:18: a resource address. The interface spells it
 * phys_addr_t; this face keeps the platform's 64-bit width (the board's BARs and
 * DMA windows are 64-bit even though the first consumers sit below 4 GiB). */
typedef u64			resource_size_t;

/* [LNX] include/linux/ioport.h:21-30: the resource record. The interface adds the
 * parent/sibling tree pointers and the descriptor flags; this face carries the
 * four fields a PCI driver reads (the window's bounds, its name and its type). */
struct resource {
	resource_size_t		start;
	resource_size_t		end;
	const char		*name;
	unsigned long		flags;
};

/* [LNX] include/linux/pci.h:529-536: the bus record. The interface hangs a device
 * tree, a resource list and the bridge window bookkeeping off it; this face keeps
 * the device node and the bus number a driver reads. */
struct pci_bus {
	struct device		dev;
	unsigned char		number;
};

/* [LNX] include/linux/pci.h:283-345: the function record, the members a compiled
 * consumer names. The interface is much wider (the topology links, the MSI and
 * PM state machines, the saved config space); those arrive with a consumer that
 * names them. The trailing members are this port's: the engine's own handle on
 * the backend's opaque device object and the device's slot in the engine's table.
 * Nothing above them depends on their offsets, and a driver never reads them. */
struct pci_dev {
	struct device		dev;		/* the device-model node (its name is the BDF) */
	struct pci_bus		*bus;		/* the bus this function sits on */
	unsigned int		devfn;		/* encoded device and function number */
	u16			vendor;		/* vendor id */
	u16			device;		/* device id */
	u16			subsystem_vendor;
	u16			subsystem_device;
	u8			revision;	/* revision id */
	u32			class;		/* class/subclass/prog-if, 24 bits */
	unsigned int		irq;		/* the interrupt the engine allocated, 0 if none */
	struct resource		resource[PCI_STD_NUM_BARS];
	unsigned int		msi_enabled:1;	/* pci_enable_msi succeeded and is not released */
	unsigned int		pcie_cap:1;	/* the device carries a PCIe capability */
	/* first-party tail */
	void			*be;		/* the backend's opaque handle for this function */
	u8			be_index;	/* this record's slot in the engine's device table */
	u8			be_present;	/* the slot holds a live device */
};

/* [LNX] include/linux/pci.h:1261-1270: the driver record. The interface also
 * carries the err-handler, driver-managed-DMA and SR-IOV callbacks; this face
 * carries the four members a driver fills and the generic driver record it embeds
 * (whose probe/remove arrive with the bus-type unit, see linux/device/driver.h). */
struct pci_driver {
	const char			*name;
	const struct pci_device_id	*id_table;
	int	(*probe)(struct pci_dev *dev, const struct pci_device_id *id);
	void	(*remove)(struct pci_dev *dev);
	struct device_driver		driver;
};

/* [LNX] include/linux/pci.h:1327-1346: the id-table entry macros. The interface
 * spells them over PCI_ANY_ID; the shapes are transcribed so a driver's table
 * reads the same here as upstream. */
#define	PCI_DEVICE(vend, dev)						\
	.vendor = (vend), .device = (dev),				\
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID

#define	PCI_DEVICE_SUB(vend, dev, subvend, subdev)			\
	.vendor = (vend), .device = (dev),				\
	.subvendor = (subvend), .subdevice = (subdev)

#define	PCI_DEVICE_CLASS(dev_class, dev_class_mask)			\
	.class = (dev_class), .class_mask = (dev_class_mask),		\
	.vendor = PCI_ANY_ID, .device = PCI_ANY_ID,			\
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID

#define	PCI_VDEVICE(vend, dev)						\
	.vendor = PCI_VENDOR_ID_##vend, .device = (dev),		\
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID, 0, 0

/* ------------------------------------------------------------------ */
/* the engine's driver-facing entries                                  */
/* ------------------------------------------------------------------ */

int	pci_register_driver(struct pci_driver *drv);
void	pci_unregister_driver(struct pci_driver *drv);

int	pci_enable_device(struct pci_dev *dev);
void	pci_disable_device(struct pci_dev *dev);
void	pci_set_master(struct pci_dev *dev);

int	pci_read_config_byte(const struct pci_dev *dev, int where, u8 *val);
int	pci_read_config_word(const struct pci_dev *dev, int where, u16 *val);
int	pci_read_config_dword(const struct pci_dev *dev, int where, u32 *val);
int	pci_write_config_byte(const struct pci_dev *dev, int where, u8 val);
int	pci_write_config_word(const struct pci_dev *dev, int where, u16 val);
int	pci_write_config_dword(const struct pci_dev *dev, int where, u32 val);

int	pci_find_capability(struct pci_dev *dev, int cap);

resource_size_t	pci_resource_start(const struct pci_dev *dev, int bar);
resource_size_t	pci_resource_end(const struct pci_dev *dev, int bar);
resource_size_t	pci_resource_len(const struct pci_dev *dev, int bar);
unsigned long	pci_resource_flags(const struct pci_dev *dev, int bar);

void __iomem	*pci_ioremap_bar(struct pci_dev *dev, int bar);

int	pci_enable_msi(struct pci_dev *dev);
void	pci_disable_msi(struct pci_dev *dev);

const char	*pci_name(const struct pci_dev *dev);
void	pci_dev_put(struct pci_dev *dev);

/* [LNX] include/linux/pci.h:2161-2168: the driver-data accessors. */
static inline void pci_set_drvdata(struct pci_dev *pdev, void *data)
{
	pdev->dev.driver_data = data;
}

static inline void *pci_get_drvdata(const struct pci_dev *pdev)
{
	return pdev->dev.driver_data;
}

#endif /* _LINUXKPI_LINUX_PCI_H_ */
