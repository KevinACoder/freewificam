/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   lkpi_pcie_port.c
 * @brief  The linuxkpi PCI engine's board adapter: the backend seam over the
 *         RK3568 root complex, the ITS-backed message-interrupt domain and the
 *         cache maintenance the host needs.
 *
 * The seam (components/linuxkpi/pci/lkpi_pcie_backend.h) is the whole surface
 * this file implements. Everything the engine asks for is mapped onto the board's
 * own drivers - the DesignWare root complex (dwc_pcie), the MSI and MSI-X
 * programming (dwc_msi / dwc_msix) and the arch interrupt registry - and nothing
 * above the seam ever sees a register, a window or an INTID it did not ask for.
 *
 * Three decisions worth stating:
 *
 *   - The engine is not told about cache maintenance. A streaming buffer is made
 *     visible in the direction the caller named (a flush before the device reads,
 *     an invalidate before the CPU reads what the device wrote), and a coherent
 *     buffer comes from the non-cacheable window, so it needs none. The seam's
 *     contract says the adapter owns this, and this file is the one place it
 *     happens.
 *   - The interrupt vectors the board hands out are LPIs (INTID 8192 and up).
 *     They are armed through the arch registry with the per-vector argument the
 *     seam carries, so one dispatcher serves every vector, and enabled through
 *     the CMSIS bridge that knows an LPI is enabled by its property byte rather
 *     than by a distributor bit.
 *   - A function that cannot carry the kind asked for (no MSI-X capability, no
 *     domain) answers with the seam's no-vector code, which the engine maps to the
 *     caller's error - a fallback the caller can act on rather than a generic
 *     failure.
 *
 * @author zhugengyu
 * @date   12.10.2026
 */

#include <stdint.h>

#include "cache.h"
#include "dwc_msi.h"
#include "dwc_msix.h"
#include "dwc_pcie.h"
#include "gicv3.h"
#include "gicv3_its.h"
#include "lkpi_pcie_backend.h"
#include "log.h"
#include "msi.h"

/* Vectors the adapter can be asked for at once. The board's domain hands out 48
 * LPIs in total and a function may hold up to 8, so this is a bookkeeping bound. */
#define PORT_IRQ_MAX	8

/* The coherent window. It is the same non-cacheable section the other DMA-capable
 * drivers use, so the pool needs no mapping call and no maintenance; the size and
 * the alignment are the interface's contract rather than the first consumer's
 * wish. dma_alloc_coherent() returns memory aligned to at least the smallest page
 * size, and a driver is entitled to rely on it: the AC 7260's interrupt-cause
 * table asserts its DMA address is ICT_SIZE-aligned (third-party/iwlwifi/pcie/
 * gen1_2/rx.c:2196), which a 64-byte pool cannot promise. The block header is the
 * alignment wide, so every block - and therefore every payload - starts on that
 * boundary; the pool is sized for the driver's queue and command buffers with
 * room over. */
#define PORT_DMA_POOL_SIZE	(1u * 1024u * 1024u)
#define PORT_DMA_ALIGN		4096u

struct port_dma_blk {
	uint32_t	size;		/* payload bytes in this block */
	uint32_t	used;
	uint8_t		pad[PORT_DMA_ALIGN - 8];
};

static uint8_t port_dma_pool[PORT_DMA_POOL_SIZE]
	__attribute__((section(".noncacheable"), aligned(PORT_DMA_ALIGN)));
static bool port_dma_ready;

struct port_irq {
	int		intid;
	int		kind;
	void		(*fn)(void *);
	void		*arg;
	bool		used;
};

static struct port_irq port_irq[PORT_IRQ_MAX];

/* ------------------------------------------------------------------ */
/* the interrupt dispatcher                                            */
/* ------------------------------------------------------------------ */

/* One dispatcher for every vector: the registry carries the slot as the handler's
 * argument, so the delivery finds the pair the engine registered. */
static void port_irq_dispatch(void *arg)
{
	struct port_irq *slot = arg;

	if (slot->fn != NULL) {
		slot->fn(slot->arg);
	}
}

static struct port_irq *port_irq_alloc(void)
{
	uint32_t i;

	for (i = 0; i < PORT_IRQ_MAX; i++) {
		if (!port_irq[i].used) {
			return &port_irq[i];
		}
	}
	return NULL;
}

static struct port_irq *port_irq_find(int intid)
{
	uint32_t i;

	for (i = 0; i < PORT_IRQ_MAX; i++) {
		if (port_irq[i].used && port_irq[i].intid == intid) {
			return &port_irq[i];
		}
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/* the DMA window                                                      */
/* ------------------------------------------------------------------ */

static void port_dma_init(void)
{
	struct port_dma_blk *blk = (struct port_dma_blk *)port_dma_pool;

	if (port_dma_ready) {
		return;
	}
	blk->size = PORT_DMA_POOL_SIZE - sizeof(*blk);
	blk->used = 0;
	port_dma_ready = true;
}

static uint8_t *port_dma_block_next(const struct port_dma_blk *blk)
{
	return (uint8_t *)blk + sizeof(*blk) + blk->size;
}

/* First fit over the pool's block list. A freed block coalesces with its
 * neighbour, so a workload that allocates and frees repeatedly does not
 * fragment the window into unusable holes. */
static int port_dma_alloc(size_t len, void **cpu, u64 *bus)
{
	struct port_dma_blk *blk;
	uint8_t *end;

	port_dma_init();
	if (len == 0 || len > PORT_DMA_POOL_SIZE - sizeof(*blk)) {
		return -LKPI_PCIE_BE_ERR_RANGE;
	}
	len = (len + PORT_DMA_ALIGN - 1u) & ~(size_t)(PORT_DMA_ALIGN - 1u);

	end = port_dma_pool + PORT_DMA_POOL_SIZE;
	for (blk = (struct port_dma_blk *)port_dma_pool;
	     (uint8_t *)blk + sizeof(*blk) <= end;
	     blk = (struct port_dma_blk *)port_dma_block_next(blk)) {
		if (blk->used || blk->size < len) {
			continue;
		}
		if (blk->size >= len + sizeof(*blk) + PORT_DMA_ALIGN) {
			struct port_dma_blk *rest =
				(struct port_dma_blk *)((uint8_t *)blk +
					sizeof(*blk) + len);

			rest->size = (uint32_t)(blk->size - len - sizeof(*blk));
			rest->used = 0;
			blk->size = (uint32_t)len;
		}
		blk->used = 1;
		*cpu = (uint8_t *)blk + sizeof(*blk);
		*bus = (u64)(uintptr_t)*cpu;
		return 0;
	}
	return -LKPI_PCIE_BE_ERR_FAIL;
}

static void port_dma_free(void *cpu, u64 bus, size_t len)
{
	struct port_dma_blk *blk;
	uint8_t *end = port_dma_pool + PORT_DMA_POOL_SIZE;

	(void)bus;
	(void)len;
	if (cpu == NULL) {
		return;
	}
	blk = (struct port_dma_blk *)((uint8_t *)cpu - sizeof(*blk));
	if ((uint8_t *)blk < port_dma_pool || (uint8_t *)blk >= end) {
		return;
	}
	blk->used = 0;
	/* coalesce the run of free blocks that follows */
	while ((uint8_t *)blk + sizeof(*blk) + blk->size + sizeof(*blk) <= end) {
		struct port_dma_blk *next =
			(struct port_dma_blk *)port_dma_block_next(blk);

		if (next->used) {
			break;
		}
		blk->size += (uint32_t)(sizeof(*blk) + next->size);
	}
}

/* The direction's maintenance, done where the seam says it belongs. A buffer the
 * device will read is written back; a buffer the device will write is invalidated
 * so the CPU cannot later read a stale line; both directions do both, in that
 * order, so the device never sees a stale line and the CPU never reads one. */
static void port_dma_sync(void *cpu, size_t len, int dir)
{
	uintptr_t addr = (uintptr_t)cpu;

	if (len == 0 || cpu == NULL) {
		return;
	}
	if (dir == LKPI_PCIE_BE_DMA_TO_DEVICE) {
		cache_dcache_flush(addr, len);
	} else if (dir == LKPI_PCIE_BE_DMA_FROM_DEVICE) {
		cache_dcache_invalidate(addr, len);
	} else if (dir == LKPI_PCIE_BE_DMA_BIDIRECTIONAL) {
		cache_dcache_flush(addr, len);
		cache_dcache_invalidate(addr, len);
	}
}

/* ------------------------------------------------------------------ */
/* the seam operations                                                 */
/* ------------------------------------------------------------------ */

static int port_bus_up(void *ctx, struct lkpi_pcie_be_dev **devs,
    struct lkpi_pcie_be_desc *descs, int max, int *count)
{
	uint32_t n, i;

	(void)ctx;
	/* The board's own bring-up: the firmware-trained link is inherited and the
	 * downstream bus is scanned. It is idempotent, so a second start is free. */
	dwc_pcie_init();
	n = dwc_pcie_dev_count();
	if ((int)n > max) {
		n = (uint32_t)max;
	}
	for (i = 0; i < n; i++) {
		const struct dwc_pcie_dev *dev = dwc_pcie_dev(i);
		uint32_t progif = 0;
		uint32_t j;

		if (dev == NULL) {
			continue;
		}
		devs[i] = (struct lkpi_pcie_be_dev *)dev;
		descs[i].vendor = dev->vendor;
		descs[i].device = dev->device;
		/*
		 * The subsystem ids, read from the type-0 header's 0x2c dword
		 * (subsystem vendor id in its low half, subsystem id in the high
		 * half). They are not decoration: a driver's id table may pin the
		 * subsystem id, and iwlwifi's does - its AC 7260 rows are
		 * IWL_PCI_DEVICE(0x08B1, 0x5070, ...) and the macro leaves
		 * .subvendor wild but sets .subdevice, so a device record with a
		 * zero subsystem id matches no row at all and the probe never
		 * runs (measured on the board: the part enumerated and nothing
		 * claimed it).
		 */
		{
			uint32_t subsys = 0;

			(void)dwc_pcie_cfg_read(dev->busn, dev->devfn, 0x2c, 4,
						&subsys);
			descs[i].subsys_vendor = (uint16_t)(subsys & 0xffffu);
			descs[i].subsys_device = (uint16_t)((subsys >> 16) & 0xffffu);
		}
		(void)dwc_pcie_cfg_read(dev->busn, dev->devfn, PCI_CFG_REVISION, 4,
					&progif);
		/* class/subclass/prog-if, as the interface packs them: the byte at
		 * 0x09 is the programming interface, 0x0a the subclass, 0x0b the class */
		descs[i].class_code = ((uint32_t)dev->baseclass << 16) |
		    ((uint32_t)dev->subclass << 8) | (progif >> 8 & 0xffu);
		descs[i].revision = (u8)(progif & 0xffu);
		descs[i].bus = (u8)(dev->busn & 0xffu);
		descs[i].dev = (u8)((dev->devfn >> 3) & 0x1fu);
		descs[i].func = (u8)(dev->devfn & 0x7u);
		descs[i].msi_capable = dev->msi_cap != 0;
		descs[i].msix_capable = dev->msix_cap != 0;
		descs[i].link_gen = 1;		/* the board's link is Gen1-x1 */
		descs[i].link_width = 1;
		descs[i].host_dma_mask = 0xffffffffu;
		for (j = 0; j < LKPI_PCIE_BE_MAX_BARS && j < 6u; j++) {
			descs[i].bar[j].phys = dev->bar[j];
			descs[i].bar[j].size = 0;
			descs[i].bar[j].flags = 0;
		}
	}
	*count = (int)n;
	return (int)n;
}

static void port_bus_down(void *ctx)
{

	(void)ctx;
}

static int port_cfg_read(void *ctx, struct lkpi_pcie_be_dev *d, u16 off,
    int size, u32 *val)
{
	const struct dwc_pcie_dev *dev = (const struct dwc_pcie_dev *)d;
	uint32_t v = 0;
	int32_t ret;

	(void)ctx;
	ret = dwc_pcie_cfg_read(dev->busn, dev->devfn, off, (uint32_t)size, &v);
	if (ret != ARM_DRIVER_OK) {
		return -LKPI_PCIE_BE_ERR_XFER;
	}
	*val = v;
	return 0;
}

static int port_cfg_write(void *ctx, struct lkpi_pcie_be_dev *d, u16 off,
    int size, u32 val)
{
	const struct dwc_pcie_dev *dev = (const struct dwc_pcie_dev *)d;
	int32_t ret;

	(void)ctx;
	ret = dwc_pcie_cfg_write(dev->busn, dev->devfn, off, (uint32_t)size, val);
	return ret == ARM_DRIVER_OK ? 0 : -LKPI_PCIE_BE_ERR_XFER;
}

static int port_bar_map(void *ctx, struct lkpi_pcie_be_dev *d, int index,
    void **mmio)
{
	const struct dwc_pcie_dev *dev = (const struct dwc_pcie_dev *)d;

	(void)ctx;
	if (index < 0 || index >= 6 || dev->bar[index] == 0) {
		return -LKPI_PCIE_BE_ERR_INVAL;
	}
	/* The device windows are identity-mapped as Device, so the address the
	 * firmware assigned is the address the driver uses. */
	*mmio = (void *)(uintptr_t)dev->bar[index];
	return 0;
}

static int port_irq_setup(void *ctx, struct lkpi_pcie_be_dev *d, int kind,
    void (*handler)(void *), void *arg, int *irqn)
{
	const struct dwc_pcie_dev *dev = (const struct dwc_pcie_dev *)d;
	struct port_irq *slot;
	MSI_VECTOR vec;
	int32_t n;

	(void)ctx;
	slot = port_irq_alloc();
	if (slot == NULL) {
		return -LKPI_PCIE_BE_ERR_NOVECTOR;
	}

	if (kind == LKPI_PCIE_BE_IRQ_MSI) {
		n = dwc_msi_alloc(dev, 1, &vec);
	} else {
		n = dwc_msix_alloc(dev, 1, &vec);
	}
	if (n < 1) {
		if (n == ARM_DRIVER_ERROR_UNSUPPORTED) {
			return -LKPI_PCIE_BE_ERR_NOVECTOR;
		}
		return -LKPI_PCIE_BE_ERR_FAIL;
	}

	slot->intid = (int)vec.intid;
	slot->kind = kind;
	slot->fn = handler;
	slot->arg = arg;
	slot->used = true;

	/* One dispatcher, armed with the slot as its argument. An LPI is enabled
	 * through its property byte rather than a distributor bit, so the enable
	 * is the ITS's own call - the vectors this adapter hands out are all
	 * message interrupts, and every message interrupt is an LPI. */
	(void)irq_register(vec.intid, port_irq_dispatch, slot);
	gic_lpi_set_state(vec.intid, 1);

	*irqn = (int)vec.intid;
	return 0;
}

static void port_irq_clear(void *ctx, struct lkpi_pcie_be_dev *d, int irqn)
{
	const struct dwc_pcie_dev *dev = (const struct dwc_pcie_dev *)d;
	struct port_irq *slot = port_irq_find(irqn);

	(void)ctx;
	if (slot == NULL) {
		return;
	}
	gic_lpi_set_state((uint32_t)irqn, 0);
	(void)irq_register((uint32_t)irqn, NULL, NULL);
	if (slot->kind == LKPI_PCIE_BE_IRQ_MSI) {
		(void)dwc_msi_release(dev);
	} else {
		(void)dwc_msix_release(dev);
	}
	slot->used = false;
	slot->fn = NULL;
	slot->arg = NULL;
}

static int port_dma_alloc_op(void *ctx, size_t len, void **cpu, u64 *bus)
{

	(void)ctx;
	return port_dma_alloc(len, cpu, bus);
}

static void port_dma_free_op(void *ctx, void *cpu, u64 bus, size_t len)
{

	(void)ctx;
	port_dma_free(cpu, bus, len);
}

static int port_dma_map(void *ctx, void *cpu, size_t len, int dir, u64 *bus)
{

	(void)ctx;
	if (cpu == NULL) {
		return -LKPI_PCIE_BE_ERR_INVAL;
	}
	port_dma_sync(cpu, len, dir);
	*bus = (u64)(uintptr_t)cpu;
	return 0;
}

static void port_dma_unmap(void *ctx, void *cpu, size_t len, int dir)
{

	(void)ctx;
	/* The device may have written the buffer since the map, so the CPU's view
	 * has to be dropped before it reads. The to-device direction needs nothing
	 * here: the data left at map time. */
	if (dir != LKPI_PCIE_BE_DMA_TO_DEVICE) {
		port_dma_sync(cpu, len, dir);
	}
}

/* The interface's dma_sync_single_for_{device,cpu}: a live streaming mapping
 * whose direction of use has turned around. The maintenance is the same body the
 * map and unmap paths use, so it is the same call - what differs is only when it
 * happens, and that is the caller's. Without this op the engine's dispatcher
 * would have nothing to call and the driver's sync would silently do nothing on
 * the board, which is exactly the shape of bug a descriptor ring hides. */
static void port_dma_sync_op(void *ctx, void *cpu, size_t len, int dir)
{

	(void)ctx;
	port_dma_sync(cpu, len, dir);
}

static const struct lkpi_pcie_be_ops port_ops = {
	.bus_up = port_bus_up,
	.bus_down = port_bus_down,
	.cfg_read = port_cfg_read,
	.cfg_write = port_cfg_write,
	.bar_map = port_bar_map,
	.irq_setup = port_irq_setup,
	.irq_clear = port_irq_clear,
	.dma_alloc = port_dma_alloc_op,
	.dma_free = port_dma_free_op,
	.dma_map = port_dma_map,
	.dma_unmap = port_dma_unmap,
	.dma_sync = port_dma_sync_op,
};

void lkpi_pcie_port_install(void)
{

	lkpi_pcie_be_register(&port_ops, NULL);
}
