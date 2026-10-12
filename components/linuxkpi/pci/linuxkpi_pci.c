/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * linuxkpi_pci.c - the linuxkpi PCI engine: the Linux-facing records and calls
 * over the backend seam.
 *
 * The engine owns the driver registry, the device table the backend's enumeration
 * produced, the config-space calls, the BAR windows, the interrupt dispatch and
 * the DMA calls. It speaks Linux semantics to its callers and reaches the host
 * only through lkpi_pcie_backend.h, so the same bytes run against the synthetic
 * host in the Gate 1 suite and against the board's root-complex driver.
 *
 * One host, one coherent pool: the DMA calls ignore the device argument because
 * this port has a single PCI host and a single non-cacheable window, and the
 * engine's device lookup for the DMA mask compares against the host's one mask.
 * The interrupt dispatch keeps a small slot table keyed by the INTID the backend
 * handed out, so a delivery finds the handler the caller registered.
 *
 * @design ports & adapters (see lkpi_pcie_backend.h). Reference for the registry
 *         and probe shape: components/linuxkpi/usb/linuxkpi_usb.c:88-123 (the
 *         driver table) and :1896-1950 (the match-and-probe walk).
 */

#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>

#include "lkpi_pcie_backend.h"
#include "linuxkpi_pci.h"

/* the config-space offsets the engine reads and writes (the interface's
 * uapi/linux/pci_regs.h numbering) */
#define	PCI_CFG_VENDOR_ID	0x00
#define	PCI_CFG_DEVICE_ID	0x02
#define	PCI_CFG_STATUS		0x06
#define	PCI_CFG_REVISION	0x08
#define	PCI_CFG_SUBSYS_VENDOR	0x2c
#define	PCI_CFG_SUBSYS_ID	0x2e
#define	PCI_CFG_CAP_PTR		0x34
#define	PCI_STATUS_CAP_LIST	0x10
#define	PCI_CFG_CAP_ID		0x00
#define	PCI_CFG_CAP_NEXT	0x01
/* the capability walk's bound: a malformed list must not spin forever */
#define	PCI_CAP_WALK_LIMIT	48

#define	LKPI_PCI_DRIVERS_MAX	4
#define	LKPI_PCI_DEVS_MAX	8
#define	LKPI_PCI_IRQ_MAX	16

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */

static struct pci_driver *lkpi_pci_drivers[LKPI_PCI_DRIVERS_MAX];
static unsigned int lkpi_pci_driver_count;

static struct pci_dev lkpi_pci_devs[LKPI_PCI_DEVS_MAX];
static struct pci_bus lkpi_pci_buses[LKPI_PCI_DEVS_MAX];
static char lkpi_pci_dev_name[LKPI_PCI_DEVS_MAX][16];
static bool lkpi_pci_dev_claimed[LKPI_PCI_DEVS_MAX];
static unsigned int lkpi_pci_dev_count;
static bool lkpi_pci_bus_up;
static u32 lkpi_pci_host_dma_mask;
static bool lkpi_pci_started;

static struct lkpi_pci_stats lkpi_pci_stats;

/* one delivered vector: the INTID the backend returned, the function it belongs
 * to and the handler the caller registered */
struct lkpi_pci_irq_slot {
	int		intid;
	int		kind;
	struct pci_dev	*pdev;
	irq_handler_t	fn;
	void		*dev;
	bool		active;
};

static struct lkpi_pci_irq_slot lkpi_pci_irq[LKPI_PCI_IRQ_MAX];

/* the probe pass (defined with the registry below); pci_register_driver calls it
 * when a device is already enumerated */
static void lkpi_pci_probe_all(void);

/* ------------------------------------------------------------------ */
/* the interrupt dispatch                                              */
/* ------------------------------------------------------------------ */

static struct lkpi_pci_irq_slot *
lkpi_pci_irq_find(int intid)
{
	unsigned int i;

	for (i = 0; i < LKPI_PCI_IRQ_MAX; i++) {
		if (lkpi_pci_irq[i].active && lkpi_pci_irq[i].intid == intid)
			return &lkpi_pci_irq[i];
	}
	return NULL;
}

static struct lkpi_pci_irq_slot *
lkpi_pci_irq_alloc_slot(void)
{
	unsigned int i;

	for (i = 0; i < LKPI_PCI_IRQ_MAX; i++) {
		if (!lkpi_pci_irq[i].active)
			return &lkpi_pci_irq[i];
	}
	return NULL;
}

/* what the backend calls: it carries the slot, and the slot carries the handler
 * the caller registered (a vector with no handler yet is delivered and dropped) */
static void
lkpi_pci_irq_trampoline(void *arg)
{
	struct lkpi_pci_irq_slot *slot = arg;

	lkpi_pci_stats.irqs++;
	if (slot->fn != NULL)
		(void)slot->fn(slot->intid, slot->dev);
}

int
lkpi_pci_irq_setup(struct pci_dev *pdev, int kind, int *irqn)
{
	struct lkpi_pci_irq_slot *slot;
	int r, intid = 0;

	if (pdev == NULL || !pdev->be_present || irqn == NULL)
		return -LKPI_PCIE_BE_ERR_NODEV;
	slot = lkpi_pci_irq_alloc_slot();
	if (slot == NULL)
		return -ENOSPC;
	r = lkpi_pcie_be_irq_setup(pdev->be, kind, lkpi_pci_irq_trampoline,
	    slot, &intid);
	if (r != 0) {
		lkpi_pci_stats.errors++;
		return r;
	}
	slot->intid = intid;
	slot->kind = kind;
	slot->pdev = pdev;
	slot->fn = NULL;
	slot->dev = NULL;
	slot->active = true;
	*irqn = intid;
	return 0;
}

void
lkpi_pci_irq_clear(struct pci_dev *pdev, int kind, int irqn)
{
	struct lkpi_pci_irq_slot *slot = lkpi_pci_irq_find(irqn);

	(void)kind;
	if (pdev != NULL && pdev->be_present)
		lkpi_pcie_be_irq_clear(pdev->be, irqn);
	if (slot != NULL) {
		slot->active = false;
		slot->fn = NULL;
		slot->dev = NULL;
	}
}

int
request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
    const char *name, void *dev)
{
	struct lkpi_pci_irq_slot *slot = lkpi_pci_irq_find((int)irq);

	(void)flags;
	(void)name;
	if (slot == NULL)
		return -EINVAL;
	if (slot->fn != NULL)
		return -EBUSY;
	slot->fn = handler;
	slot->dev = dev;
	return 0;
}

void
free_irq(unsigned int irq, void *dev_id)
{
	struct lkpi_pci_irq_slot *slot = lkpi_pci_irq_find((int)irq);

	(void)dev_id;
	if (slot == NULL)
		return;
	slot->fn = NULL;
	slot->dev = NULL;
}

/* ------------------------------------------------------------------ */
/* config space                                                        */
/* ------------------------------------------------------------------ */

static int
lkpi_pci_cfg_read(const struct pci_dev *dev, int where, int size, u32 *val)
{
	u32 v = 0;
	int r;

	if (dev == NULL || !dev->be_present)
		return -ENODEV;
	r = lkpi_pcie_be_cfg_read(dev->be, (u16)where, size, &v);
	if (r != 0) {
		/* the interface's convention: a failed read answers all ones */
		lkpi_pci_stats.errors++;
		*val = (size == 1) ? 0xffu : (size == 2) ? 0xffffu : 0xffffffffu;
		return r;
	}
	*val = v;
	return 0;
}

static int
lkpi_pci_cfg_write(const struct pci_dev *dev, int where, int size, u32 val)
{
	int r;

	if (dev == NULL || !dev->be_present)
		return -ENODEV;
	r = lkpi_pcie_be_cfg_write(dev->be, (u16)where, size, val);
	if (r != 0)
		lkpi_pci_stats.errors++;
	return r;
}

int
pci_read_config_byte(const struct pci_dev *dev, int where, u8 *val)
{
	u32 v = 0;
	int r = lkpi_pci_cfg_read(dev, where, 1, &v);

	*val = (u8)v;
	return r;
}

int
pci_read_config_word(const struct pci_dev *dev, int where, u16 *val)
{
	u32 v = 0;
	int r = lkpi_pci_cfg_read(dev, where, 2, &v);

	*val = (u16)v;
	return r;
}

int
pci_read_config_dword(const struct pci_dev *dev, int where, u32 *val)
{
	u32 v = 0;
	int r = lkpi_pci_cfg_read(dev, where, 4, &v);

	*val = v;
	return r;
}

int
pci_write_config_byte(const struct pci_dev *dev, int where, u8 val)
{

	return lkpi_pci_cfg_write(dev, where, 1, val);
}

int
pci_write_config_word(const struct pci_dev *dev, int where, u16 val)
{

	return lkpi_pci_cfg_write(dev, where, 2, val);
}

int
pci_write_config_dword(const struct pci_dev *dev, int where, u32 val)
{

	return lkpi_pci_cfg_write(dev, where, 4, val);
}

int
pci_find_capability(struct pci_dev *dev, int cap)
{
	u16 status = 0;
	u8 pos = 0, id = 0;
	int ttl = PCI_CAP_WALK_LIMIT;

	if (pci_read_config_word(dev, PCI_CFG_STATUS, &status) != 0)
		return 0;
	if ((status & PCI_STATUS_CAP_LIST) == 0)
		return 0;
	if (pci_read_config_byte(dev, PCI_CFG_CAP_PTR, &pos) != 0)
		return 0;
	while (ttl-- > 0 && pos >= 0x40) {
		pos = (u8)(pos & ~3u);
		if (pci_read_config_byte(dev, pos + PCI_CFG_CAP_ID, &id) != 0)
			return 0;
		if (id == 0xff)
			break;
		if (id == (u8)cap)
			return pos;
		if (pci_read_config_byte(dev, pos + PCI_CFG_CAP_NEXT, &pos) != 0)
			return 0;
	}
	return 0;
}

int
pci_enable_device(struct pci_dev *dev)
{
	u16 cmd = 0;
	int r;

	r = pci_read_config_word(dev, PCI_COMMAND, &cmd);
	if (r != 0)
		return r;
	if ((cmd & PCI_COMMAND_MEMORY) != 0)
		return 0;
	return pci_write_config_word(dev, PCI_COMMAND, (u16)(cmd | PCI_COMMAND_MEMORY));
}

void
pci_disable_device(struct pci_dev *dev)
{
	u16 cmd = 0;

	if (pci_read_config_word(dev, PCI_COMMAND, &cmd) != 0)
		return;
	(void)pci_write_config_word(dev, PCI_COMMAND,
	    (u16)(cmd & ~(PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)));
}

void
pci_set_master(struct pci_dev *dev)
{
	u16 cmd = 0;

	if (pci_read_config_word(dev, PCI_COMMAND, &cmd) != 0)
		return;
	if ((cmd & PCI_COMMAND_MASTER) == 0)
		(void)pci_write_config_word(dev, PCI_COMMAND,
		    (u16)(cmd | PCI_COMMAND_MASTER));
}

/* ------------------------------------------------------------------ */
/* resources                                                           */
/* ------------------------------------------------------------------ */

resource_size_t
pci_resource_start(const struct pci_dev *dev, int bar)
{

	if (dev == NULL || bar < 0 || bar >= PCI_STD_NUM_BARS)
		return 0;
	return dev->resource[bar].start;
}

resource_size_t
pci_resource_end(const struct pci_dev *dev, int bar)
{

	if (dev == NULL || bar < 0 || bar >= PCI_STD_NUM_BARS)
		return 0;
	return dev->resource[bar].end;
}

resource_size_t
pci_resource_len(const struct pci_dev *dev, int bar)
{
	resource_size_t start, end;

	if (dev == NULL || bar < 0 || bar >= PCI_STD_NUM_BARS)
		return 0;
	start = dev->resource[bar].start;
	end = dev->resource[bar].end;
	if (start == 0 && end == 0)
		return 0;
	return end - start + 1;
}

unsigned long
pci_resource_flags(const struct pci_dev *dev, int bar)
{

	if (dev == NULL || bar < 0 || bar >= PCI_STD_NUM_BARS)
		return 0;
	return dev->resource[bar].flags;
}

void __iomem *
pci_ioremap_bar(struct pci_dev *dev, int bar)
{
	void *mmio = NULL;
	int r;

	if (dev == NULL || !dev->be_present || bar < 0 || bar >= PCI_STD_NUM_BARS)
		return NULL;
	r = lkpi_pcie_be_bar_map(dev->be, bar, &mmio);
	if (r != 0) {
		lkpi_pci_stats.errors++;
		return NULL;
	}
	/* the port identity-maps its device windows, so the address the backend
	 * hands back is already the CPU address the driver will use */
	return (void __iomem *)mmio;
}

/* ------------------------------------------------------------------ */
/* interrupts                                                          */
/* ------------------------------------------------------------------ */

int
pci_enable_msi(struct pci_dev *dev)
{
	int r, irq = 0;

	if (dev == NULL || !dev->be_present)
		return -ENODEV;
	if (dev->msi_enabled)
		return -EBUSY;
	r = lkpi_pci_irq_setup(dev, LKPI_PCIE_BE_IRQ_MSI, &irq);
	if (r != 0)
		return r;
	dev->irq = (unsigned int)irq;
	dev->msi_enabled = 1;
	lkpi_pci_stats.msi++;
	return 0;
}

void
pci_disable_msi(struct pci_dev *dev)
{

	if (dev == NULL || !dev->msi_enabled)
		return;
	lkpi_pci_irq_clear(dev, LKPI_PCIE_BE_IRQ_MSI, (int)dev->irq);
	dev->irq = 0;
	dev->msi_enabled = 0;
}

/* ------------------------------------------------------------------ */
/* DMA                                                                 */
/* ------------------------------------------------------------------ */

void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle,
    gfp_t flag)
{
	void *cpu = NULL;
	u64 bus = 0;
	int r;

	(void)dev;
	(void)flag;
	r = lkpi_pcie_be_dma_alloc(size, &cpu, &bus);
	if (r != 0) {
		lkpi_pci_stats.errors++;
		if (dma_handle != NULL)
			*dma_handle = DMA_MAPPING_ERROR;
		return NULL;
	}
	if (dma_handle != NULL)
		*dma_handle = bus;
	return cpu;
}

void
dma_free_coherent(struct device *dev, size_t size, void *cpu_addr,
    dma_addr_t dma_handle)
{

	(void)dev;
	lkpi_pcie_be_dma_free(cpu_addr, dma_handle, size);
}

dma_addr_t
dma_map_single(struct device *dev, void *ptr, size_t size,
    enum dma_data_direction dir)
{
	u64 bus = 0;
	int r;

	(void)dev;
	r = lkpi_pcie_be_dma_map(ptr, size, (int)dir, &bus);
	if (r != 0) {
		lkpi_pci_stats.errors++;
		return DMA_MAPPING_ERROR;
	}
	lkpi_pci_stats.dma_maps++;
	return bus;
}

void
dma_unmap_single(struct device *dev, dma_addr_t addr, size_t size,
    enum dma_data_direction dir)
{

	(void)dev;
	/* the port identity-maps, so the bus address the caller holds is the CPU
	 * address the seam's unmap expects */
	lkpi_pcie_be_dma_unmap((void *)(uintptr_t)addr, size, (int)dir);
}

int
dma_set_mask_and_coherent(struct device *dev, u64 mask)
{

	(void)dev;
	if (mask > (u64)lkpi_pci_host_dma_mask)
		return -LKPI_PCIE_BE_ERR_DMAMASK;
	return 0;
}

/* ------------------------------------------------------------------ */
/* the driver registry and the probe walk                              */
/* ------------------------------------------------------------------ */

int
pci_register_driver(struct pci_driver *drv)
{

	if (drv == NULL || drv->probe == NULL)
		return -EINVAL;
	if (lkpi_pci_driver_count >= LKPI_PCI_DRIVERS_MAX)
		return -ENOSPC;
	lkpi_pci_drivers[lkpi_pci_driver_count++] = drv;
	lkpi_printf(ULOG_INFO_LEVEL, "pcie: driver '%s' registered\n",
	    drv->name != NULL ? drv->name : "?");
	/* a device already enumerated is claimed now, not at the next start */
	if (lkpi_pci_bus_up)
		lkpi_pci_probe_all();
	return 0;
}

void
pci_unregister_driver(struct pci_driver *drv)
{
	unsigned int i;

	for (i = 0; i < lkpi_pci_driver_count; i++) {
		if (lkpi_pci_drivers[i] == drv)
			lkpi_pci_drivers[i] = NULL;
	}
}

static bool
lkpi_pci_id_match(const struct pci_device_id *id, const struct pci_dev *d)
{

	if (id->vendor != PCI_ANY_ID && id->vendor != d->vendor)
		return false;
	if (id->device != PCI_ANY_ID && id->device != d->device)
		return false;
	if (id->subvendor != PCI_ANY_ID && id->subvendor != d->subsystem_vendor)
		return false;
	if (id->subdevice != PCI_ANY_ID && id->subdevice != d->subsystem_device)
		return false;
	if ((d->class & id->class_mask) != (id->class & id->class_mask))
		return false;
	return true;
}

/* an id table ends at the all-zero entry (PCI_ANY_ID is all ones, so a wildcard
 * row is not the terminator) */
static bool
lkpi_pci_id_is_end(const struct pci_device_id *id)
{

	return id->vendor == 0 && id->device == 0 && id->subvendor == 0 &&
	    id->subdevice == 0 && id->class == 0 && id->class_mask == 0;
}

static void
lkpi_pci_probe_dev(struct pci_dev *d)
{
	unsigned int i;

	if (!d->be_present || lkpi_pci_dev_claimed[d->be_index])
		return;
	for (i = 0; i < lkpi_pci_driver_count; i++) {
		struct pci_driver *drv = lkpi_pci_drivers[i];
		const struct pci_device_id *id;

		if (drv == NULL || drv->probe == NULL || drv->id_table == NULL)
			continue;
		for (id = drv->id_table; !lkpi_pci_id_is_end(id); id++) {
			int r;

			if (!lkpi_pci_id_match(id, d))
				continue;
			r = drv->probe(d, id);
			if (r == 0) {
				lkpi_pci_stats.probes++;
				lkpi_pci_dev_claimed[d->be_index] = true;
				lkpi_printf(ULOG_INFO_LEVEL,
				    "pcie: '%s' claimed %04x:%04x\n",
				    drv->name != NULL ? drv->name : "?",
				    d->vendor, d->device);
				/* one driver per device, as upstream: a claimed
				 * device does not run a second driver's probe */
				return;
			}
			/* this driver's table matched but its probe failed: the
			 * device stays unbound and the next driver may claim it */
			lkpi_pci_stats.probe_errors++;
			break;
		}
	}
}

static void
lkpi_pci_probe_all(void)
{
	unsigned int i;

	for (i = 0; i < lkpi_pci_dev_count; i++)
		lkpi_pci_probe_dev(&lkpi_pci_devs[i]);
}

/* ------------------------------------------------------------------ */
/* device records                                                      */
/* ------------------------------------------------------------------ */

static void
lkpi_pci_dev_build(unsigned int index, struct lkpi_pcie_be_dev *be,
    const struct lkpi_pcie_be_desc *desc)
{
	static const char hex[] = "0123456789abcdef";
	struct pci_dev *d = &lkpi_pci_devs[index];
	char *name = lkpi_pci_dev_name[index];
	int i, n = 0;

	memset(d, 0, sizeof(*d));
	d->be = be;
	d->be_index = (u8)index;
	d->be_present = 1;
	d->vendor = desc->vendor;
	d->device = desc->device;
	d->subsystem_vendor = desc->subsys_vendor;
	d->subsystem_device = desc->subsys_device;
	d->revision = desc->revision;
	d->class = desc->class_code;
	d->bus = &lkpi_pci_buses[index];
	d->bus->number = desc->bus;
	d->devfn = (unsigned int)((desc->dev << 3) | desc->func);
	for (i = 0; i < LKPI_PCIE_BE_MAX_BARS && i < PCI_STD_NUM_BARS; i++) {
		d->resource[i].start = desc->bar[i].phys;
		d->resource[i].end = desc->bar[i].size != 0 ?
		    desc->bar[i].phys + desc->bar[i].size - 1 : 0;
		d->resource[i].flags = desc->bar[i].flags;
		d->resource[i].name = "BAR";
	}
	/* the device-model name is the BDF the driver and the logs print */
	name[n++] = hex[(desc->bus >> 4) & 0xf];
	name[n++] = hex[desc->bus & 0xf];
	name[n++] = ':';
	name[n++] = hex[(desc->dev >> 4) & 0xf];
	name[n++] = hex[desc->dev & 0xf];
	name[n++] = '.';
	name[n++] = hex[desc->func & 0xf];
	name[n] = '\0';
	d->dev.name = name;
	d->dev.parent = NULL;
}

/* ------------------------------------------------------------------ */
/* startup                                                             */
/* ------------------------------------------------------------------ */

void
lkpi_pci_start(void)
{
	struct lkpi_pcie_be_dev *be[LKPI_PCI_DEVS_MAX];
	struct lkpi_pcie_be_desc desc[LKPI_PCI_DEVS_MAX];
	unsigned int i;
	int n = 0, r;

	if (lkpi_pci_started) {
		lkpi_pci_probe_all();
		return;
	}
	lkpi_pci_started = true;
	if (!lkpi_pcie_be_ready()) {
		lkpi_printf(ULOG_ERROR_LEVEL, "pcie: no backend installed\n");
		return;
	}
	r = lkpi_pcie_be_bus_up(be, desc, LKPI_PCI_DEVS_MAX, &n);
	if (r < 0) {
		lkpi_printf(ULOG_ERROR_LEVEL, "pcie: bus up failed (%d)\n", r);
		lkpi_pci_stats.errors++;
		return;
	}
	lkpi_pci_bus_up = true;
	lkpi_pci_host_dma_mask = (n > 0) ? desc[0].host_dma_mask : 0xffffffffu;
	for (i = 0; i < (unsigned int)n && i < LKPI_PCI_DEVS_MAX; i++)
		lkpi_pci_dev_build(i, be[i], &desc[i]);
	lkpi_pci_dev_count = (unsigned int)n < LKPI_PCI_DEVS_MAX ?
	    (unsigned int)n : LKPI_PCI_DEVS_MAX;
	lkpi_printf(ULOG_INFO_LEVEL, "pcie: bus up, %u device(s)\n",
	    lkpi_pci_dev_count);
	lkpi_pci_probe_all();
}

bool
lkpi_pci_ready(void)
{

	return lkpi_pci_bus_up;
}

unsigned int
lkpi_pci_driver_count_get(void)
{

	return lkpi_pci_driver_count;
}

unsigned int
lkpi_pci_dev_count_get(void)
{

	return lkpi_pci_dev_count;
}

struct pci_dev *
lkpi_pci_dev_get(unsigned int index)
{

	if (index >= lkpi_pci_dev_count)
		return NULL;
	return &lkpi_pci_devs[index];
}

const struct lkpi_pci_stats *
lkpi_pci_stats_get(void)
{

	return &lkpi_pci_stats;
}

const char *
pci_name(const struct pci_dev *dev)
{

	return dev != NULL ? dev->dev.name : "?";
}

void
pci_dev_put(struct pci_dev *dev)
{

	/* this port has no reference-counted device tree; the record lives in the
	 * engine's table for the life of the image, so the release is the identity */
	(void)dev;
}

/* ------------------------------------------------------------------ */
/* the backend seam: registration and the dispatchers                  */
/* ------------------------------------------------------------------ */

static const struct lkpi_pcie_be_ops *lkpi_pcie_be_ops;
static void *lkpi_pcie_be_ctx;

void
lkpi_pcie_be_register(const struct lkpi_pcie_be_ops *ops, void *ctx)
{

	lkpi_pcie_be_ops = ops;
	lkpi_pcie_be_ctx = ctx;
}

bool
lkpi_pcie_be_ready(void)
{

	return lkpi_pcie_be_ops != NULL;
}

int
lkpi_pcie_be_bus_up(struct lkpi_pcie_be_dev **devs,
    struct lkpi_pcie_be_desc *descs, int max, int *count)
{

	if (lkpi_pcie_be_ops == NULL || lkpi_pcie_be_ops->bus_up == NULL)
		return -LKPI_PCIE_BE_ERR_NODEV;
	return lkpi_pcie_be_ops->bus_up(lkpi_pcie_be_ctx, devs, descs, max, count);
}

void
lkpi_pcie_be_bus_down(void)
{

	if (lkpi_pcie_be_ops != NULL && lkpi_pcie_be_ops->bus_down != NULL)
		lkpi_pcie_be_ops->bus_down(lkpi_pcie_be_ctx);
}

int
lkpi_pcie_be_cfg_read(struct lkpi_pcie_be_dev *d, u16 off, int size, u32 *val)
{

	if (lkpi_pcie_be_ops == NULL || lkpi_pcie_be_ops->cfg_read == NULL)
		return -LKPI_PCIE_BE_ERR_NODEV;
	return lkpi_pcie_be_ops->cfg_read(lkpi_pcie_be_ctx, d, off, size, val);
}

int
lkpi_pcie_be_cfg_write(struct lkpi_pcie_be_dev *d, u16 off, int size, u32 val)
{

	if (lkpi_pcie_be_ops == NULL || lkpi_pcie_be_ops->cfg_write == NULL)
		return -LKPI_PCIE_BE_ERR_NODEV;
	return lkpi_pcie_be_ops->cfg_write(lkpi_pcie_be_ctx, d, off, size, val);
}

int
lkpi_pcie_be_bar_map(struct lkpi_pcie_be_dev *d, int index, void **mmio)
{

	if (lkpi_pcie_be_ops == NULL || lkpi_pcie_be_ops->bar_map == NULL)
		return -LKPI_PCIE_BE_ERR_NODEV;
	return lkpi_pcie_be_ops->bar_map(lkpi_pcie_be_ctx, d, index, mmio);
}

int
lkpi_pcie_be_irq_setup(struct lkpi_pcie_be_dev *d, int kind,
    void (*handler)(void *), void *arg, int *irqn)
{

	if (lkpi_pcie_be_ops == NULL || lkpi_pcie_be_ops->irq_setup == NULL)
		return -LKPI_PCIE_BE_ERR_NODEV;
	return lkpi_pcie_be_ops->irq_setup(lkpi_pcie_be_ctx, d, kind, handler, arg,
	    irqn);
}

void
lkpi_pcie_be_irq_clear(struct lkpi_pcie_be_dev *d, int irqn)
{

	if (lkpi_pcie_be_ops != NULL && lkpi_pcie_be_ops->irq_clear != NULL)
		lkpi_pcie_be_ops->irq_clear(lkpi_pcie_be_ctx, d, irqn);
}

int
lkpi_pcie_be_dma_alloc(size_t len, void **cpu, u64 *bus)
{

	if (lkpi_pcie_be_ops == NULL || lkpi_pcie_be_ops->dma_alloc == NULL)
		return -LKPI_PCIE_BE_ERR_NODEV;
	return lkpi_pcie_be_ops->dma_alloc(lkpi_pcie_be_ctx, len, cpu, bus);
}

void
lkpi_pcie_be_dma_free(void *cpu, u64 bus, size_t len)
{

	if (lkpi_pcie_be_ops != NULL && lkpi_pcie_be_ops->dma_free != NULL)
		lkpi_pcie_be_ops->dma_free(lkpi_pcie_be_ctx, cpu, bus, len);
}

int
lkpi_pcie_be_dma_map(void *cpu, size_t len, int dir, u64 *bus)
{

	if (lkpi_pcie_be_ops == NULL || lkpi_pcie_be_ops->dma_map == NULL)
		return -LKPI_PCIE_BE_ERR_NODEV;
	return lkpi_pcie_be_ops->dma_map(lkpi_pcie_be_ctx, cpu, len, dir, bus);
}

void
lkpi_pcie_be_dma_unmap(void *cpu, size_t len, int dir)
{

	if (lkpi_pcie_be_ops != NULL && lkpi_pcie_be_ops->dma_unmap != NULL)
		lkpi_pcie_be_ops->dma_unmap(lkpi_pcie_be_ctx, cpu, len, dir);
}
