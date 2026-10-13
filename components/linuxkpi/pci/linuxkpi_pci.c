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
#include <linux/dmapool.h>
#include <linux/interrupt.h>
#include <linux/scatterlist.h>
#include <linux/device.h>
#include <linux/mm.h>

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

/* the extended-capability list's geometry and header fields (the interface's
 * uapi/linux/pci_regs.h numbering: the list starts at 0x100, the header carries
 * the id in the low half and the next-capability offset in bits 20..31). The
 * interface's own walk is drivers/pci/pci.c's PCI_FIND_NEXT_EXT_CAP macro. */
#define	PCI_CFG_SPACE_SIZE	256
#define	PCI_CFG_SPACE_EXP_SIZE	4096
#define	PCI_EXT_CAP_ID(hdr)	((hdr) & 0x0000ffffu)
#define	PCI_EXT_CAP_NEXT(hdr)	(((hdr) >> 20) & 0xffcu)

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
/* the driver that claimed each device, so the device model's unbind
 * (device_release_driver) can call its remove() - the engine otherwise only
 * remembers that a device is claimed, not by whom */
static struct pci_driver *lkpi_pci_dev_owner[LKPI_PCI_DEVS_MAX];
static unsigned int lkpi_pci_dev_count;
static bool lkpi_pci_bus_up;
static u32 lkpi_pci_host_dma_mask;
static bool lkpi_pci_started;

static struct lkpi_pci_stats lkpi_pci_stats;

/* one delivered vector: the INTID the backend returned, the function it belongs
 * to, the handler the caller registered and (for a threaded request) the half the
 * hard handler asks to run. The threaded half may sleep - that is the interface's
 * contract for it - so it runs on the engine's workqueue and never on the
 * delivery path, which is an interrupt: a vendored receive path that takes a lock
 * there is refused by a thread-only lock (the AC 7260's sweep: CMSIS answered
 * "resource" to a mutex acquire from interrupt context, the lock silently stayed
 * unheld, and the imported code's own assert then parked the board). */
struct lkpi_pci_irq_slot {
	int		intid;
	int		kind;
	struct pci_dev	*pdev;
	irq_handler_t	fn;
	irq_handler_t	thread_fn;
	void		*dev;
	bool		active;
	struct work_struct	thread_work;
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

/* the threaded half, on the engine's workqueue (a thread, so it may sleep) */
static void
lkpi_pci_irq_thread_work(struct work_struct *w)
{
	struct lkpi_pci_irq_slot *slot =
	    container_of(w, struct lkpi_pci_irq_slot, thread_work);

	if (slot->thread_fn != NULL)
		(void)slot->thread_fn(slot->intid, slot->dev);
}

/* what the backend calls: it carries the slot, and the slot carries the handler
 * the caller registered (a vector with no handler yet is delivered and dropped).
 * When the hard half returns IRQ_WAKE_THREAD, the threaded half is queued to the
 * workqueue: the delivery path is an interrupt, and the interface's threaded half
 * is the half allowed to sleep and to take locks. (The previous spelling ran it
 * inline on the delivery path; the vendored receive path is what found the
 * difference.) */
static void
lkpi_pci_irq_trampoline(void *arg)
{
	struct lkpi_pci_irq_slot *slot = arg;
	irqreturn_t ret;

	lkpi_pci_stats.irqs++;
	if (slot->fn == NULL)
		return;
	ret = slot->fn(slot->intid, slot->dev);
	if (ret == IRQ_WAKE_THREAD && slot->thread_fn != NULL)
		schedule_work(&slot->thread_work);
}

/* record the threaded half of a request_irq() that has just been accepted */
static void
lkpi_pci_irq_set_thread(unsigned int irq, irq_handler_t thread_fn)
{
	struct lkpi_pci_irq_slot *slot = lkpi_pci_irq_find((int)irq);

	if (slot != NULL)
		slot->thread_fn = thread_fn;
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
	slot->thread_fn = NULL;
	slot->dev = NULL;
	/* the deferral must be usable before any delivery can queue it */
	INIT_WORK(&slot->thread_work, lkpi_pci_irq_thread_work);
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
		/* a queued or running threaded half finishes before the slot can
		 * be handed to another device */
		cancel_work_sync(&slot->thread_work);
		slot->active = false;
		slot->fn = NULL;
		slot->thread_fn = NULL;
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
	slot->thread_fn = NULL;
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
	cancel_work_sync(&slot->thread_work);
	slot->fn = NULL;
	slot->thread_fn = NULL;
	slot->dev = NULL;
}

/* [LNX] include/linux/interrupt.h:211-215: the device-managed threaded request.
 * The interface registers both halves and has the kernel run the threaded half in
 * its own thread; this port's engine has one delivery path and no interrupt
 * thread, so the request is the engine's request_irq() plus the threaded half,
 * which the trampoline runs when the hard half asks for it. The managed half (the
 * release on device detach) has no counterpart without devres: the vector lives
 * for the image's life, or until free_irq(). */
int
devm_request_threaded_irq(struct device *dev, unsigned int irq,
    irq_handler_t handler, irq_handler_t thread_fn, unsigned long irqflags,
    const char *devname, void *dev_id)
{
	int r;

	(void)dev;
	if (handler == NULL)
		return -EINVAL;
	r = request_irq(irq, handler, irqflags, devname, dev_id);
	if (r != 0)
		return r;
	lkpi_pci_irq_set_thread(irq, thread_fn);
	return 0;
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

/* [LNX] include/linux/dma-mapping.h:169-171,615: the page-map form. The interface
 * maps the page's physical address (kernel/dma/mapping.c: dma_map_page_attrs);
 * this port's seam speaks a CPU address and identity-maps it, so the page's CPU
 * address plus the offset is the same buffer. attrs is dropped at the seam:
 * DMA_ATTR_SKIP_CPU_SYNC asks the adapter to skip maintenance it would otherwise
 * do, and doing it anyway is always safe (it is the attribute's "fast path", not a
 * correctness requirement). */
dma_addr_t
dma_map_page_attrs(struct device *dev, struct page *page, size_t offset,
    size_t size, enum dma_data_direction dir, unsigned long attrs)
{

	(void)attrs;
	if (page == NULL)
		return DMA_MAPPING_ERROR;
	return dma_map_single(dev, (char *)page_address(page) + offset, size, dir);
}

dma_addr_t
dma_map_page(struct device *dev, struct page *page, size_t offset, size_t size,
    enum dma_data_direction dir)
{

	/* the interface's macro form is the attribute form with no attributes */
	return dma_map_page_attrs(dev, page, offset, size, dir, 0);
}

void
dma_unmap_page(struct device *dev, dma_addr_t addr, size_t size,
    enum dma_data_direction dir)
{

	/* the interface's macro form (include/linux/dma-mapping.h:616) is the
	 * attribute unmap with no attributes; the engine's single unmap is this
	 * port's one unmap mechanism */
	dma_unmap_single(dev, addr, size, dir);
}

/* [LNX] include/linux/dma-mapping.h:443-456: the single-direction sync pair. The
 * interface moves ownership of a live mapping back to the CPU (for_cpu) or to the
 * device (for_device); this port reaches the seam's dma_sync, which re-runs the
 * direction's maintenance without unmapping, so the adapter owns the cache work
 * and a synthetic backend ignores it. The port identity-maps, so the bus address
 * the caller holds is the CPU address the seam expects. */
void
dma_sync_single_for_cpu(struct device *dev, dma_addr_t addr, size_t size,
    enum dma_data_direction dir)
{

	(void)dev;
	lkpi_pcie_be_dma_sync((void *)(uintptr_t)addr, size, (int)dir);
}

void
dma_sync_single_for_device(struct device *dev, dma_addr_t addr, size_t size,
    enum dma_data_direction dir)
{

	(void)dev;
	lkpi_pcie_be_dma_sync((void *)(uintptr_t)addr, size, (int)dir);
}

/* [LNX] include/linux/dma-mapping.h:570-574: the interface's inline unmaps the
 * table's original entry count (dma_unmap_sg_attrs over orig_nents). The port
 * identity-maps, so each entry's recorded bus address is the CPU address the
 * seam's unmap expects; an entry that was never mapped carries a zero address and
 * is skipped. */
void
dma_unmap_sgtable(struct device *dev, struct sg_table *sgt,
    enum dma_data_direction dir, unsigned long attrs)
{
	struct scatterlist *sg;
	unsigned int i;

	(void)attrs;
	if (sgt == NULL)
		return;
	for_each_sgtable_sg(sgt, sg, i) {
		if (sg_dma_address(sg) == 0)
			continue;
		dma_unmap_single(dev, sg_dma_address(sg), sg_dma_len(sg), dir);
	}
}

/* ------------------------------------------------------------------ */
/* the coherent pool (linux/dmapool.h)                                 */
/* ------------------------------------------------------------------ */

/* [LNX] include/linux/dmapool.h:14: the pool record the interface keeps private.
 * This port has no sub-allocator, so the record carries only what one allocation
 * needs: the block size every block is carved at and the device the blocks are
 * allocated for. */
struct dma_pool {
	size_t		size;	/* the block size every allocation is carved at */
	struct device	*dev;	/* the device the blocks are allocated for */
};

/* [LNX] include/linux/dmapool.h:34-35: the managed pool. The interface carves
 * fixed-size coherent blocks out of one larger coherent allocation; this port
 * hands each block out as one coherent allocation (the engine's
 * dma_alloc_coherent), which is the same block with the same bus address. `align`
 * and `allocation` are the interface's sub-allocator tuning and are dropped: the
 * coherent window this port hands out has no boundary the caller's alignment
 * would violate. The managed free-on-detach half has no counterpart without
 * devres. */
struct dma_pool *
dmam_pool_create(const char *name, struct device *dev, size_t size, size_t align,
    size_t allocation)
{
	struct dma_pool *pool;

	(void)name;
	(void)align;
	(void)allocation;
	pool = kmalloc(sizeof(*pool), GFP_KERNEL);
	if (pool == NULL)
		return NULL;
	pool->size = size;
	pool->dev = dev;
	return pool;
}

/* [LNX] include/linux/dmapool.h:27-28 */
void *
dma_pool_alloc(struct dma_pool *pool, gfp_t mem_flags, dma_addr_t *handle)
{

	if (pool == NULL)
		return NULL;
	return dma_alloc_coherent(pool->dev, pool->size, handle, mem_flags);
}

/* [LNX] include/linux/dmapool.h:29 */
void
dma_pool_free(struct dma_pool *pool, void *vaddr, dma_addr_t addr)
{

	if (pool == NULL)
		return;
	dma_free_coherent(pool->dev, pool->size, vaddr, addr);
}

/* ------------------------------------------------------------------ */
/* the scatter/gather helpers (linux/scatterlist.h)                    */
/* ------------------------------------------------------------------ */

/* [LNX] include/linux/scatterlist.h:447; body lib/scatterlist.c:130-134: zero the
 * entries and mark the last one the end of the list, which is what sg_next() and
 * sg_is_last() walk. */
void
sg_init_table(struct scatterlist *sgl, unsigned int nents)
{

	if (sgl == NULL || nents == 0)
		return;
	memset(sgl, 0, sizeof(*sgl) * nents);
	sg_mark_end(&sgl[nents - 1]);
}

/* [LNX] include/linux/scatterlist.h:442; body lib/scatterlist.c:25-30: the entry
 * count, following the chain to the end marker. */
int
sg_nents(struct scatterlist *sg)
{
	int nents;

	for (nents = 0; sg != NULL; sg = sg_next(sg))
		nents++;
	return nents;
}

/* [LNX] include/linux/scatterlist.h:524; body lib/scatterlist.c:1049-1053
 * (sg_copy_buffer in the buffer-to-list direction, which walks the list, skips
 * `skip` bytes and copies into each entry's page). The interface reaches the page
 * through kmap(); this port reaches it through page_address() - the same
 * mechanism its skbuff fragment views use. Returns the number of bytes copied. */
size_t
sg_pcopy_from_buffer(struct scatterlist *sgl, unsigned int nents,
    const void *buf, size_t buflen, loff_t skip)
{
	struct scatterlist *sg;
	size_t offset = 0;
	unsigned int i;
	loff_t rest = skip;

	if (sgl == NULL || buf == NULL || buflen == 0)
		return 0;
	for_each_sg(sgl, sg, nents, i) {
		size_t len;
		void *addr;

		if (rest >= (loff_t)sg->length) {
			rest -= (loff_t)sg->length;
			continue;
		}
		len = (size_t)sg->length - (size_t)rest;
		if (len > buflen - offset)
			len = buflen - offset;
		if (len == 0)
			break;
		addr = (char *)page_address(sg_page(sg)) + sg->offset +
		    (size_t)rest;
		memcpy(addr, (const char *)buf + offset, len);
		offset += len;
		rest = 0;
		if (offset >= buflen)
			break;
	}
	return offset;
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

static int
lkpi_pci_probe_dev(struct pci_dev *d)
{
	unsigned int i;
	int last_error = 0;

	if (!d->be_present || lkpi_pci_dev_claimed[d->be_index])
		return 0;
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
				lkpi_pci_dev_owner[d->be_index] = drv;
				lkpi_printf(ULOG_INFO_LEVEL,
				    "pcie: '%s' claimed %04x:%04x\n",
				    drv->name != NULL ? drv->name : "?",
				    d->vendor, d->device);
				/* one driver per device, as upstream: a claimed
				 * device does not run a second driver's probe */
				return 0;
			}
			/* this driver's table matched but its probe failed: the
			 * device stays unbound and the next driver may claim it.
			 * The failure is remembered so device_reprobe() can hand
			 * the interface's caller the probe's error. */
			lkpi_pci_stats.probe_errors++;
			last_error = r;
			break;
		}
	}
	return last_error;
}

static void
lkpi_pci_probe_all(void)
{
	unsigned int i;

	for (i = 0; i < lkpi_pci_dev_count; i++)
		(void)lkpi_pci_probe_dev(&lkpi_pci_devs[i]);
}

/* ------------------------------------------------------------------ */
/* device records                                                      */
/* ------------------------------------------------------------------ */

/*
 * The bus under every enumerated function. The interface's device model gives a
 * device's bus a name, and Linux's PCI core is what supplies it: the name of
 * pci_bus_type is "pci" (drivers/pci/pci-driver.c). One reader in the compiled
 * set exists - the vendored firmware-error dump copies it into its
 * human-readable field (third-party/iwlwifi/fw/dbg-old.c:849,
 * fwrt->dev->bus->name) - and leaving the pointer unset made that read a wild
 * dereference: the first board run that reached the dump took a translation
 * fault on the address a NULL-derived pointer produces (far=0x180120).
 */
static struct bus_type lkpi_pci_bus_type = {
	.name = "pci",
};

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
	d->dev.bus = &lkpi_pci_bus_type;
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

/* [LNX] include/linux/pci.h:1275: `struct pci_dev *pci_dev_get(struct pci_dev *dev)`
 * - the take that pairs with pci_dev_put. The interface increments the record's
 * reference count; this port keeps every record in the engine's table for the life
 * of the image and has no count to move, so the take is the identity, exactly as
 * the release is (pci_dev_put below). The walkers that hand a record out
 * (pci_get_device, pci_get_slot) call it so the call sites read as upstream's do. */
struct pci_dev *
pci_dev_get(struct pci_dev *dev)
{

	return dev;
}

void
pci_dev_put(struct pci_dev *dev)
{

	/* this port has no reference-counted device tree; the record lives in the
	 * engine's table for the life of the image, so the release is the identity */
	(void)dev;
}

/* ------------------------------------------------------------------ */
/* the device-table walks (linux/pci.h)                                */
/* ------------------------------------------------------------------ */

/* [LNX] include/linux/pci.h:1308-1309: the interface walks the global device list
 * and returns the next function whose ids match (the for_each_pci_dev() macro,
 * include/linux/pci.h:615, is the from = NULL loop). This port's device list is the
 * engine's table, so the walk is over it; PCI_ANY_ID is the wildcard. The record is
 * handed back with the interface's take (pci_dev_get, whose release pci_dev_put is
 * the identity here). */
struct pci_dev *
pci_get_device(unsigned int vendor, unsigned int device, struct pci_dev *from)
{
	unsigned int i = 0;

	if (from != NULL) {
		for (i = 0; i < lkpi_pci_dev_count; i++) {
			if (&lkpi_pci_devs[i] == from) {
				i++;
				break;
			}
		}
		if (i >= lkpi_pci_dev_count)
			return NULL;
	}
	for (; i < lkpi_pci_dev_count; i++) {
		struct pci_dev *d = &lkpi_pci_devs[i];

		if (!d->be_present)
			continue;
		if (vendor != PCI_ANY_ID && d->vendor != (u16)vendor)
			continue;
		if (device != PCI_ANY_ID && d->device != (u16)device)
			continue;
		return pci_dev_get(d);
	}
	return NULL;
}

/* [LNX] include/linux/pci.h:1315: the function at (bus, devfn). The engine's
 * records carry both, so the walk is over its table. */
struct pci_dev *
pci_get_slot(struct pci_bus *bus, unsigned int devfn)
{
	unsigned int i;

	for (i = 0; i < lkpi_pci_dev_count; i++) {
		struct pci_dev *d = &lkpi_pci_devs[i];

		if (d->be_present && d->bus == bus && d->devfn == devfn)
			return pci_dev_get(d);
	}
	return NULL;
}

/* [LNX] include/linux/pci.h:1724-1725: the first table row a device matches. The
 * engine's own id match (lkpi_pci_id_match, the walk its probe pass uses) is the
 * one mechanism, so this is stated over it rather than written a second time. */
const struct pci_device_id *
pci_match_id(const struct pci_device_id *ids, struct pci_dev *dev)
{

	if (ids == NULL || dev == NULL)
		return NULL;
	for (; !lkpi_pci_id_is_end(ids); ids++) {
		if (lkpi_pci_id_match(ids, dev))
			return ids;
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/* the remaining config-space and resource entries (linux/pci.h)        */
/* ------------------------------------------------------------------ */

/* [LNX] include/linux/pci.h:1300: the offset of the extended capability, or 0.
 * The interface walks the extended list from 0x100 (drivers/pci/pci.c:517-525 and
 * its PCI_FIND_NEXT_EXT_CAP macro, drivers/pci/pci.h:190); the walk is stated here
 * over the engine's config read. A malformed list stops at the end marker (a zero
 * header) or when the read fails, and the TTL bounds a list that loops. */
u16
pci_find_ext_capability(struct pci_dev *dev, int cap)
{
	u16 pos = PCI_CFG_SPACE_SIZE;
	int ttl = (PCI_CFG_SPACE_EXP_SIZE - PCI_CFG_SPACE_SIZE) / 8;
	u32 header = 0;

	while (ttl-- > 0 && pos >= PCI_CFG_SPACE_SIZE) {
		if (pci_read_config_dword(dev, pos, &header) != 0)
			break;
		if (header == 0)
			break;
		if ((int)PCI_EXT_CAP_ID(header) == cap)
			return pos;
		pos = (u16)PCI_EXT_CAP_NEXT(header);
	}
	return 0;
}

/* [LNX] include/linux/pci.h:1356: read a word out of the PCIe capability. The
 * interface reads at pci_pcie_cap(dev) + pos and rejects an odd offset
 * (drivers/pci/access.c:427-460); this face records "is PCIe" as a bit and not the
 * capability offset, so the offset comes from the engine's own capability walk -
 * the one mechanism this port uses to find a capability. A device without a PCIe
 * capability answers 0, as the interface's unimplemented-register arm does. */
int
pcie_capability_read_word(struct pci_dev *dev, int pos, u16 *val)
{
	int cap;

	if (val == NULL)
		return -EINVAL;
	*val = 0;
	if ((pos & 1) != 0)
		return -EINVAL;
	cap = pci_find_capability(dev, PCI_CAP_ID_EXP);
	if (cap == 0)
		return 0;
	return pci_read_config_word(dev, cap + pos, val);
}

/* [LNX] include/linux/pci.h:1430: the device-managed enable. The engine's
 * pci_enable_device() sets the memory-enable bit; the interface's managed half
 * (the disable on detach) has no counterpart without devres, so the enable is the
 * whole call here and a probe sees the interface's effect. */
int
pcim_enable_device(struct pci_dev *pdev)
{

	return pci_enable_device(pdev);
}

/* [LNX] include/linux/pci.h:2543: the interface takes an exclusive claim on every
 * BAR window. This port has no resource registry - the backend owns the windows
 * and the engine reads their geometry off the enumeration record (see
 * lkpi_pci_dev_build) - so there is nothing to claim and the answer is the
 * interface's success. The mapping that matters is pcim_iomap(). */
int
pcim_request_all_regions(struct pci_dev *pdev, const char *name)
{

	(void)pdev;
	(void)name;
	return 0;
}

/* [LNX] include/linux/pci.h:2544: map a BAR. The interface's maxlen caps the
 * mapping (0 = the whole window); the engine's pci_ioremap_bar() always maps the
 * whole window the backend describes, which is what the driver's bar 0 is. The
 * managed unmap-on-detach half has no counterpart without devres. */
void __iomem *
pcim_iomap(struct pci_dev *pdev, int bar, unsigned long maxlen)
{

	(void)maxlen;
	return pci_ioremap_bar(pdev, bar);
}

/* [LNX] include/linux/pci.h:1494: assign a resource. This port's device record
 * carries the windows the backend already mapped (lkpi_pci_dev_build), so there is
 * nothing for the engine to assign and the interface's "already assigned" answer
 * is 0. */
int
pci_assign_resource(struct pci_dev *dev, int i)
{

	(void)dev;
	(void)i;
	return 0;
}

/* [LNX] include/linux/pci.h:1535: whether the device can wake from `state`. The
 * interface answers from the device's PM-capability presence and its PME-support
 * bitmap (drivers/pci/pci.c); this face carries neither (no consumer named them),
 * so no state is PME-capable here - and the driver's power-source choice then
 * falls to VMAIN, which is what a port without VAUX wants. */
bool
pci_pme_capable(struct pci_dev *dev, pci_power_t state)
{

	(void)dev;
	(void)state;
	return false;
}

/* [LNX] include/linux/pci.h:1950: the interface's own no-ASPM answer
 * (include/linux/pci.h:1958-1959, the !CONFIG_PCIEASPM arm) - this port has no ASPM
 * state machine to disable. The AC 7260 does not reach it either: its family has
 * pcie_l1_allowed set (third-party/iwlwifi/cfg/7000.c:58), so the driver's L1
 * workaround is not entered. */
int
pci_disable_link_state(struct pci_dev *pdev, int state)
{

	(void)pdev;
	(void)state;
	return 0;
}

/* [LNX] include/linux/pci.h:1769-1770: the interface's own no-MSI answer
 * (include/linux/pci.h:1823-1826, the !CONFIG_PCI_MSI arm). The port's MSI-X route
 * is the engine entry lkpi_pci_irq_setup() (linuxkpi_pci.h), which no Linux-face
 * driver names; and the AC 7260 does not reach this call: its family does not
 * support multiple RX queues (mac_cfg->mq_rx_supported is unset for the 7000
 * family), so iwl_pcie_set_interrupt_capa() goes straight to MSI. */
int
pci_enable_msix_range(struct pci_dev *dev, struct msix_entry *entries, int minvec,
    int maxvec)
{

	(void)dev;
	(void)entries;
	(void)minvec;
	(void)maxvec;
	return -ENOSYS;
}

/* [LNX] include/linux/pci.h:1485: reset the function. The interface tries the
 * device's reset methods in turn and answers -ENOTSUPP when none exists
 * (drivers/pci/pci.c, __pci_reset_function_locked); this port's seam carries no
 * function-reset operation, so there is no method to try and the interface's
 * no-method answer is the honest one. The caller (the firmware-error recovery
 * path) ignores the value and the device keeps its state. */
int
pci_reset_function(struct pci_dev *dev)
{

	(void)dev;
	return -ENOTSUPP;
}

/* ------------------------------------------------------------------ */
/* the remove / rescan entries (linux/pci.h)                           */
/* ------------------------------------------------------------------ */

/* [LNX] include/linux/pci.h:1554-1555: the interface's global rescan lock
 * (drivers/pci/pci.c). This port has no device removal or re-enumeration to
 * serialize - the engine enumerates once, at lkpi_pci_start() - so the pair is
 * empty. The driver's removal work still brackets its body with them, as the
 * interface's callers do. */
void
pci_lock_rescan_remove(void)
{
}

void
pci_unlock_rescan_remove(void)
{
}

/* [LNX] include/linux/pci.h:1279: remove a device. The port has no hotplug: a
 * device record cannot be freed and the bus cannot be re-enumerated. The honest
 * analogue of the interface's removal is to mark the record not-present, so every
 * later config access, BAR map and interrupt request through the engine is refused
 * instead of talking to a device that is gone. */
void
pci_stop_and_remove_bus_device(struct pci_dev *dev)
{

	if (dev != NULL)
		dev->be_present = 0;
}

/* [LNX] include/linux/pci.h:1553: re-enumerate a bus. The engine enumerates once,
 * at lkpi_pci_start(); there is no second enumeration to run, so nothing is added
 * and the interface's count of new devices is 0. */
unsigned int
pci_rescan_bus(struct pci_bus *bus)
{

	(void)bus;
	return 0;
}

/* ------------------------------------------------------------------ */
/* the device model (linux/device.h)                                   */
/* ------------------------------------------------------------------ */

/* This port's only device model is the PCI engine's: the devices are the engine's
 * table records, whose `dev` member is the device-model node. The bodies below are
 * therefore the engine's, stated over that table. */

/* [LNX] include/linux/device.h:1360-1361: the reference pair. The interface takes
 * and drops a reference on a device; this port's device records live in the
 * engine's table for the life of the image, so there is no count to maintain - the
 * take hands the pointer back and the drop has nothing to decrement (the same
 * decision pci_dev_put records). */
struct device *
get_device(struct device *dev)
{

	return dev;
}

void
put_device(struct device *dev)
{

	(void)dev;
}

/* the engine's record for a device-model node, or NULL */
static struct pci_dev *
lkpi_pci_dev_from_device(struct device *dev)
{
	unsigned int i;

	for (i = 0; i < lkpi_pci_dev_count; i++) {
		if (&lkpi_pci_devs[i].dev == dev)
			return &lkpi_pci_devs[i];
	}
	return NULL;
}

/* [LNX] include/linux/device.h:1312: unbind a device from its driver. The
 * interface's release runs the driver's remove() and drops the binding; this port's
 * device model is the engine's, so the release is the claiming driver's remove()
 * over the record with the claim cleared, which lets a later probe pass bind it
 * again. A device that is not claimed has nothing to release. */
void
device_release_driver(struct device *dev)
{
	struct pci_dev *d = lkpi_pci_dev_from_device(dev);
	struct pci_driver *drv;

	if (d == NULL || !d->be_present || !lkpi_pci_dev_claimed[d->be_index])
		return;
	drv = lkpi_pci_dev_owner[d->be_index];
	lkpi_pci_dev_claimed[d->be_index] = false;
	lkpi_pci_dev_owner[d->be_index] = NULL;
	if (drv != NULL && drv->remove != NULL)
		drv->remove(d);
}

/* [LNX] include/linux/device.h:1316: detach the device if it is bound and run the
 * bus match again (drivers/base/bus.c:884-890). The re-probe is the engine's own
 * match-and-probe pass over this one device, so the result is that pass's result. */
int
device_reprobe(struct device *dev)
{
	struct pci_dev *d;

	device_release_driver(dev);
	d = lkpi_pci_dev_from_device(dev);
	if (d == NULL || !d->be_present)
		return -ENODEV;
	return lkpi_pci_probe_dev(d);
}

/* [LNX] include/linux/device/devres.h:93-94: format a string into device-managed
 * memory. The interface reaches it from include/linux/device.h:31 (the devres
 * header); the two-pass form is the interface's own (drivers/base/devres.c:
 * devm_kasprintf over devm_kvasprintf) - the first pass asks the platform's
 * vsnprintf for the length, the second writes into the block. The managed
 * free-on-detach half has no counterpart without devres. The first consumer is the
 * MSI-X queue-name builder, which the AC 7260 never reaches (see
 * pci_enable_msix_range). */
char *
devm_kasprintf(struct device *dev, gfp_t gfp, const char *fmt, ...)
{
	va_list ap, ap2;
	char *p;
	int len;

	(void)dev;
	if (fmt == NULL)
		return NULL;
	va_start(ap, fmt);
	va_copy(ap2, ap);
	len = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (len < 0) {
		va_end(ap2);
		return NULL;
	}
	p = kmalloc((size_t)len + 1, gfp);
	if (p != NULL)
		(void)vsnprintf(p, (size_t)len + 1, fmt, ap2);
	va_end(ap2);
	return p;
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

void
lkpi_pcie_be_dma_sync(void *cpu, size_t len, int dir)
{

	/* an adapter that needs cache maintenance for a live streaming buffer
	 * implements dma_sync; without it the call is a no-op, which is what a
	 * synthetic backend (whose buffers need no maintenance) wants */
	if (lkpi_pcie_be_ops != NULL && lkpi_pcie_be_ops->dma_sync != NULL)
		lkpi_pcie_be_ops->dma_sync(lkpi_pcie_be_ctx, cpu, len, dir);
}
