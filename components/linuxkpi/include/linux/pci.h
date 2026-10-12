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

/* [LNX] include/linux/pci_ids.h:2696: the vendor id the compiled driver's device
 * table matches. */
#define	PCI_VENDOR_ID_INTEL	0x8086

/* [LNX] include/linux/pci.h:63-64: the standard BAR count. */
#define	PCI_STD_NUM_BARS	6

/* [LNX] include/linux/pci.h:197-201: the config-space capability ids this face
 * names (a driver walks the list with pci_find_capability). */
#define	PCI_CAP_ID_PM		0x01
#define	PCI_CAP_ID_MSI		0x05
#define	PCI_CAP_ID_PCIX		0x07
#define	PCI_CAP_ID_EXP		0x10
#define	PCI_CAP_ID_MSIX		0x11

/* [LNX] include/uapi/linux/pci_regs.h:726: the extended-capability id a driver's
 * error-dump walk tests against. */
#define	PCI_EXT_CAP_ID_ERR	0x01	/* Advanced Error Reporting */

/* [LNX] include/linux/pci.h:100-103: the two command bits the enable and
 * bus-master calls maintain. */
#define	PCI_COMMAND		0x04
#define	PCI_COMMAND_MEMORY	0x2
#define	PCI_COMMAND_MASTER	0x4
/* [LNX] include/uapi/linux/pci_regs.h:51 */
#define	PCI_COMMAND_INTX_DISABLE	0x400

/* [LNX] include/uapi/linux/pci_regs.h:96: the first base-address register a
 * driver's error dump reads. */
#define	PCI_BASE_ADDRESS_0	0x10	/* 32 bits */
/* [LNX] include/uapi/linux/pci_regs.h:108: the base-address memory-type field. */
#define	PCI_BASE_ADDRESS_MEM_TYPE_64	0x04	/* 64 bit address */

/* [LNX] include/uapi/linux/pci_regs.h:564-566: the PCIe link-control register and
 * its ASPM field. */
#define	PCI_EXP_LNKCTL		0x10	/* Link Control */
#define	PCI_EXP_LNKCTL_ASPM_L0S	0x0001	/* L0s Enable */
#define	PCI_EXP_LNKCTL_ASPM_L1	0x0002	/* L1 Enable */
/* [LNX] include/uapi/linux/pci_regs.h:581: the current-link-speed field value. */
#define	PCI_EXP_LNKSTA_CLS_8_0GB	0x0003	/* Current Link Speed 8.0GT/s */
/* [LNX] include/uapi/linux/pci_regs.h:679,687: the PCIe device-control-2 register
 * and its LTR-enable bit. */
#define	PCI_EXP_DEVCTL2		0x28	/* Device Control 2 */
#define	PCI_EXP_DEVCTL2_LTR_EN	0x0400	/* Enable LTR mechanism */

/* [LNX] include/linux/ioport.h:24-25: the resource flags a BAR carries. */
#define	IORESOURCE_IO		0x00000100
#define	IORESOURCE_MEM		0x00000200

/* [LNX] include/uapi/linux/pci_regs.h:822,826,835: the advanced-error-reporting
 * root-capability registers a driver's error dump walks. */
#define	PCI_ERR_ROOT_COMMAND	0x2c	/* Root Error Command */
#define	PCI_ERR_ROOT_STATUS	0x30
#define	PCI_ERR_ROOT_ERR_SRC	0x34	/* Error Source Identification */

/* [LNX] include/linux/pci.h:185-191: the device power states a driver's
 * suspend/resume path names. */
typedef int			pci_power_t;
#define	PCI_D0			((pci_power_t)0)
#define	PCI_D1			((pci_power_t)1)
#define	PCI_D2			((pci_power_t)2)
#define	PCI_D3hot		((pci_power_t)3)
#define	PCI_D3cold		((pci_power_t)4)

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
 * the device node, the bus number and the bridge device a driver's error dump
 * walks up through. */
struct pci_dev;

struct pci_bus {
	struct device		dev;
	unsigned char		number;
	struct pci_dev		*self;	/* the bridge device, NULL on the root bus */
	struct pci_bus		*parent;	/* the bus this one hangs off */
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

/* [LNX] include/linux/pci.h: the MSI-X vector/entry pair a driver asks for. */
struct msix_entry {
	u32	vector;		/* Kernel uses to write allocated vector */
	u16	entry;		/* Driver uses to specify entry, OS writes */
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

/* ------------------------------------------------------------------ */
/* The remaining PCI capability and link-state constants the compiled set names,
 * each line names the interface header and line it was transcribed from. */
/* ------------------------------------------------------------------ */

	/* [LNX] linux/pci.h:1945 */
	#define	PCIE_LINK_STATE_CLKPM	BIT(7)
	/* [LNX] linux/pci.h:1933 */
	#define	PCIE_LINK_STATE_L0S	(BIT(0) | BIT(1)) /* Upstr/dwnstr L0s */
	/* [LNX] linux/pci.h:1934 */
	#define	PCIE_LINK_STATE_L1	BIT(2)	/* L1 state */
	/* [LNX] uapi/linux/pci_regs.h:577 */
	#define	PCI_EXP_LNKSTA	0x12	/* Link Status */
	/* [LNX] uapi/linux/pci_regs.h:578 */
	#define	PCI_EXP_LNKSTA_CLS	0x000f	/* Current Link Speed */

/* ------------------------------------------------------------------ */
/* The remaining PCI entry points the compiled set names. Each is the */
/* interface's own signature; the bodies belong to the PCI core and   */
/* have no counterpart on this port yet.                              */
/* ------------------------------------------------------------------ */

/* [LNX] include/linux/pci.h:1494 */
int __must_check pci_assign_resource(struct pci_dev *dev, int i);
/* [LNX] include/linux/pci.h:1275,1279 */
struct pci_dev *pci_dev_get(struct pci_dev *dev);
void pci_stop_and_remove_bus_device(struct pci_dev *dev);
/* [LNX] include/linux/pci.h:1950 */
int pci_disable_link_state(struct pci_dev *pdev, int state);
/* [LNX] include/linux/pci.h:1774 */
int pci_enable_msix_range(struct pci_dev *dev, struct msix_entry *entries,
			  int minvec, int maxvec);
/* [LNX] include/linux/pci.h:1300 */
u16 pci_find_ext_capability(struct pci_dev *dev, int cap);
/* [LNX] include/linux/pci.h:615,1315 */
struct pci_dev *pci_get_device(unsigned int vendor, unsigned int device,
			       struct pci_dev *from);
#define	for_each_pci_dev(d)	\
	while ((d = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, d)) != NULL)
struct pci_dev *pci_get_slot(struct pci_bus *bus, unsigned int devfn);
/* [LNX] include/linux/pci.h:1553-1555 */
void pci_lock_rescan_remove(void);
void pci_unlock_rescan_remove(void);
unsigned int pci_rescan_bus(struct pci_bus *bus);
/* [LNX] include/linux/pci.h:1724 */
const struct pci_device_id *pci_match_id(const struct pci_device_id *ids,
					 struct pci_dev *dev);
/* [LNX] include/linux/pci.h:1535 */
bool pci_pme_capable(struct pci_dev *dev, pci_power_t state);
/* [LNX] include/linux/pci.h:1485 */
int pci_reset_function(struct pci_dev *dev);

/* [LNX] include/linux/pci.h:2929,2933: the per-device log lines, over the device
 * model's own log macros. */
#define	pci_err(pdev, fmt, ...)	dev_err(&(pdev)->dev, fmt, ##__VA_ARGS__)
#define	pci_info(pdev, fmt, ...)	dev_info(&(pdev)->dev, fmt, ##__VA_ARGS__)

#endif /* _LINUXKPI_LINUX_PCI_H_ */
