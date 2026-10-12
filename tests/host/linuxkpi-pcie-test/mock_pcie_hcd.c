/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * mock_pcie_hcd.c - the synthetic PCI host and scripted device model.
 *
 * Every seam operation is answered from a constant table: config space is a
 * 256-byte image with a two-entry capability list, BAR0 is a writable memory
 * region the model hands out by pointer, the doorbell is a function the test
 * calls, and the DMA calls hand out a small static pool while recording the
 * direction of every map. Nothing here talks to hardware; the model's only job is
 * to be the host the engine cannot tell from a real one.
 */

#include <string.h>

#include <linux/pci.h>

#include "lkpi_pcie_backend.h"
#include "mock_pcie_hcd.h"

static struct mock_pcie_backend mock;

/* the coherent pool the model hands out (four slots is more than any case needs) */
#define	MOCK_DMA_SLOTS	4
static u8 mock_dma_pool[MOCK_DMA_SLOTS][256] __attribute__((aligned(64)));
static bool mock_dma_used[MOCK_DMA_SLOTS];

static void
mock_dev_build(struct mock_pcie_dev *d, u8 bus, u16 device, u8 subclass)
{

	memset(d, 0, sizeof(*d));
	d->vendor = 0x1234;
	d->device = device;
	d->bus = bus;
	d->devfn = 0;
	d->cfg[MOCK_CFG_VENDOR] = 0x34;
	d->cfg[MOCK_CFG_VENDOR + 1] = 0x12;
	d->cfg[MOCK_CFG_DEVICE] = (u8)(device & 0xff);
	d->cfg[MOCK_CFG_DEVICE + 1] = (u8)(device >> 8);
	d->cfg[MOCK_CFG_STATUS] = 0x10;	/* capabilities list present */
	d->cfg[MOCK_CFG_REVISION] = 0x01;
	d->cfg[MOCK_CFG_CLASS] = 0x02;	/* network controller */
	d->cfg[MOCK_CFG_CLASS + 1] = subclass;
	d->cfg[MOCK_CFG_SUBSYS_VENDOR] = 0x34;
	d->cfg[MOCK_CFG_SUBSYS_VENDOR + 1] = 0x12;
	d->cfg[MOCK_CFG_SUBSYS_ID] = 0x01;
	d->cfg[MOCK_CFG_CAP_PTR] = MOCK_CFG_MSI_CAP;
	d->cfg[MOCK_CFG_MSI_CAP] = PCI_CAP_ID_MSI;
	d->cfg[MOCK_CFG_MSI_CAP + 1] = MOCK_CFG_MSIX_CAP;
	d->cfg[MOCK_CFG_MSIX_CAP] = PCI_CAP_ID_MSIX;
	d->cfg[MOCK_CFG_MSIX_CAP + 1] = 0x00;
}

void
mock_pcie_reset(void)
{

	memset(&mock, 0, sizeof(mock));
	mock.cfg_fail_at = -1;
	mock.msix_available = true;
	mock.n_devs = MOCK_PCIE_DEVS;
	mock_dev_build(&mock.dev[0], 0x01, 0x5678, 0x00);	/* network controller */
	mock_dev_build(&mock.dev[1], 0x02, 0x5679, 0x80);	/* other network */
	mock_dev_build(&mock.dev[2], 0x03, 0x0001, 0x08);	/* left unclaimed */
	mock.dev[2].vendor = 0xabcd;
	mock.dev[2].cfg[MOCK_CFG_VENDOR] = 0xcd;
	mock.dev[2].cfg[MOCK_CFG_VENDOR + 1] = 0xab;
	memset(mock_dma_used, 0, sizeof(mock_dma_used));
}

struct mock_pcie_backend *
mock_pcie_backend(void)
{

	return &mock;
}

void
mock_pcie_fire_irq(void)
{

	if (mock.lying || !mock.irq_active || mock.irq_handler == NULL)
		return;
	mock.irq_handler(mock.irq_arg);
}

void
mock_pcie_set_cfg_fail(int off)
{

	mock.cfg_fail_at = off;
}

void
mock_pcie_set_lying(bool lying)
{

	mock.lying = lying;
}

void
mock_pcie_set_msix_available(bool available)
{

	mock.msix_available = available;
}

/* ------------------------------------------------------------------ */
/* the seam operations                                                 */
/* ------------------------------------------------------------------ */

static int
m_bus_up(void *ctx, struct lkpi_pcie_be_dev **devs,
    struct lkpi_pcie_be_desc *descs, int max, int *count)
{
	struct mock_pcie_backend *b = ctx;
	int i, n = b->n_devs;

	if (n > max)
		n = max;
	for (i = 0; i < n; i++) {
		struct mock_pcie_dev *d = &b->dev[i];

		devs[i] = (struct lkpi_pcie_be_dev *)d;
		memset(&descs[i], 0, sizeof(descs[i]));
		descs[i].vendor = d->vendor;
		descs[i].device = d->device;
		descs[i].subsys_vendor = 0x1234;
		descs[i].subsys_device = 0x0001;
		descs[i].class_code = 0x020000u | ((u32)d->cfg[MOCK_CFG_CLASS + 1] << 8);
		descs[i].revision = d->cfg[MOCK_CFG_REVISION];
		descs[i].bus = d->bus;
		descs[i].dev = 0;
		descs[i].func = 0;
		descs[i].msi_capable = 1;
		descs[i].msix_capable = 1;
		descs[i].link_gen = 1;
		descs[i].link_width = 1;
		descs[i].host_dma_mask = 0xffffffffu;
		descs[i].bar[0].phys = (u64)(uintptr_t)d->bar;
		descs[i].bar[0].size = MOCK_PCIE_BAR_SIZE;
		descs[i].bar[0].flags = IORESOURCE_MEM;
	}
	*count = n;
	return n;
}

static void
m_bus_down(void *ctx)
{

	(void)ctx;
}

static int
m_cfg_read(void *ctx, struct lkpi_pcie_be_dev *d, u16 off, int size, u32 *val)
{
	struct mock_pcie_backend *b = ctx;
	struct mock_pcie_dev *dev = (struct mock_pcie_dev *)d;
	u32 v = 0;
	int i;

	b->cfg_reads++;
	if (b->cfg_fail_at >= 0 && (int)off == b->cfg_fail_at) {
		b->cfg_fail_at = -1;	/* one-shot */
		return -LKPI_PCIE_BE_ERR_TIMEOUT;
	}
	if ((int)off + size > (int)sizeof(dev->cfg)) {
		/* an unimplemented offset answers all ones, as a real function does */
		*val = (size == 1) ? 0xffu : (size == 2) ? 0xffffu : 0xffffffffu;
		return 0;
	}
	for (i = 0; i < size; i++)
		v |= (u32)dev->cfg[off + i] << (8 * i);
	*val = v;
	return 0;
}

static int
m_cfg_write(void *ctx, struct lkpi_pcie_be_dev *d, u16 off, int size, u32 val)
{
	struct mock_pcie_backend *b = ctx;
	struct mock_pcie_dev *dev = (struct mock_pcie_dev *)d;
	int i;

	b->cfg_writes++;
	if ((int)off + size > (int)sizeof(dev->cfg))
		return -LKPI_PCIE_BE_ERR_RANGE;
	for (i = 0; i < size; i++)
		dev->cfg[off + i] = (u8)(val >> (8 * i));
	return 0;
}

static int
m_bar_map(void *ctx, struct lkpi_pcie_be_dev *d, int index, void **mmio)
{
	struct mock_pcie_backend *b = ctx;
	struct mock_pcie_dev *dev = (struct mock_pcie_dev *)d;

	if (index != 0)
		return -LKPI_PCIE_BE_ERR_INVAL;
	b->bar_maps++;
	*mmio = dev->bar;
	return 0;
}

static int
m_irq_setup(void *ctx, struct lkpi_pcie_be_dev *d, int kind,
    void (*handler)(void *), void *arg, int *irqn)
{
	struct mock_pcie_backend *b = ctx;

	(void)d;
	if (kind == LKPI_PCIE_BE_IRQ_MSIX && !b->msix_available)
		return -LKPI_PCIE_BE_ERR_NOVECTOR;
	if (b->irq_active)
		return -LKPI_PCIE_BE_ERR_FAIL;
	b->irq_kind = kind;
	b->irq_handler = handler;
	b->irq_arg = arg;
	b->irq_intid = 8192 + (int)b->irq_setups;
	b->irq_setups++;
	b->irq_active = true;
	*irqn = b->irq_intid;
	return 0;
}

static void
m_irq_clear(void *ctx, struct lkpi_pcie_be_dev *d, int irqn)
{
	struct mock_pcie_backend *b = ctx;

	(void)d;
	(void)irqn;
	b->irq_active = false;
	b->irq_handler = NULL;
	b->irq_arg = NULL;
}

static int
m_dma_alloc(void *ctx, size_t len, void **cpu, u64 *bus)
{
	struct mock_pcie_backend *b = ctx;
	int i;

	if (len > sizeof(mock_dma_pool[0]))
		return -LKPI_PCIE_BE_ERR_RANGE;
	for (i = 0; i < MOCK_DMA_SLOTS; i++) {
		if (!mock_dma_used[i]) {
			mock_dma_used[i] = true;
			b->dma_allocs++;
			*cpu = mock_dma_pool[i];
			*bus = (u64)(uintptr_t)mock_dma_pool[i];
			return 0;
		}
	}
	return -LKPI_PCIE_BE_ERR_FAIL;
}

static void
m_dma_free(void *ctx, void *cpu, u64 bus, size_t len)
{
	struct mock_pcie_backend *b = ctx;
	int i;

	(void)bus;
	(void)len;
	for (i = 0; i < MOCK_DMA_SLOTS; i++) {
		if ((void *)mock_dma_pool[i] == cpu) {
			mock_dma_used[i] = false;
			b->dma_frees++;
			return;
		}
	}
}

static int
m_dma_map(void *ctx, void *cpu, size_t len, int dir, u64 *bus)
{
	struct mock_pcie_backend *b = ctx;

	(void)len;
	if (b->dma_dir_n < (sizeof(b->dma_dir_log) / sizeof(b->dma_dir_log[0])))
		b->dma_dir_log[b->dma_dir_n] = dir;
	b->dma_dir_n++;
	*bus = (u64)(uintptr_t)cpu;
	return 0;
}

static void
m_dma_unmap(void *ctx, void *cpu, size_t len, int dir)
{
	struct mock_pcie_backend *b = ctx;

	(void)cpu;
	(void)len;
	if (b->dma_dir_n < (sizeof(b->dma_dir_log) / sizeof(b->dma_dir_log[0])))
		b->dma_dir_log[b->dma_dir_n] = dir;
	b->dma_dir_n++;
}

static void
m_dma_sync(void *ctx, void *cpu, size_t len, int dir)
{
	struct mock_pcie_backend *b = ctx;

	(void)cpu;
	(void)len;
	/* The synthetic host records the direction on the same log the map and
	 * unmap paths use, so a case can tell a sync from a map by the sequence. */
	if (b->dma_dir_n < (sizeof(b->dma_dir_log) / sizeof(b->dma_dir_log[0])))
		b->dma_dir_log[b->dma_dir_n] = dir;
	b->dma_dir_n++;
}

static const struct lkpi_pcie_be_ops mock_ops = {
	.bus_up = m_bus_up,
	.bus_down = m_bus_down,
	.cfg_read = m_cfg_read,
	.cfg_write = m_cfg_write,
	.bar_map = m_bar_map,
	.irq_setup = m_irq_setup,
	.irq_clear = m_irq_clear,
	.dma_alloc = m_dma_alloc,
	.dma_free = m_dma_free,
	.dma_map = m_dma_map,
	.dma_unmap = m_dma_unmap,
	.dma_sync = m_dma_sync,
};

const struct lkpi_pcie_be_ops *
mock_pcie_ops(void)
{

	return &mock_ops;
}
