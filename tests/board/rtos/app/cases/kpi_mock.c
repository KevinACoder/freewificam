/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   kpi_mock.c
 * @brief  Case kpi_mock: the component's bus and network seams against a
 *         second implementation - a synthetic PCI host, a synthetic SDIO card
 *         and a null egress - with no hardware behind any of them.
 *
 * This is the replaceability proof: the same engines the board runs against
 * its real root complex and card stack are linked here against implementations
 * that live in this case.  A seam that only one implementation satisfies is not
 * a seam; this arm would not compile or run if the engines had reached into a
 * board register.
 *
 * Anchors:
 *   kpi: mock pci devs=1
 *   kpi: mock sdio card=024c:c821 funcs=1
 *   kpi: mock net rx=1
 *   kpi: PASS
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linux80211/linux80211.h"
#include "linux80211/net.h"

#include "linuxkpi_pci.h"
#include "linuxkpi_sdio.h"
#include "lkpi_pcie_backend.h"
#include "lkpi_sdio_backend.h"

#include <linux/mmc/sdio_func.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

/* ------------------------------------------------------------------ */
/* the synthetic PCI host                                              */
/* ------------------------------------------------------------------ */

static u8 mock_cfg[256];
static u8 mock_bar[4096] __attribute__((aligned(4096)));
static u8 mock_dma[4096] __attribute__((aligned(4096)));
static void (*mock_irq_handler)(void *);
static void *mock_irq_arg;

static int
mock_pci_bus_up(void *ctx, struct lkpi_pcie_be_dev **devs,
    struct lkpi_pcie_be_desc *descs, int max, int *count)
{
	(void)ctx;
	if (max < 1) {
		*count = 0;
		return 0;
	}
	/* a 8086:08b1 function: the identity the live arm looks for */
	mock_cfg[0x00] = 0x86; mock_cfg[0x01] = 0x80;
	mock_cfg[0x02] = 0xb1; mock_cfg[0x03] = 0x08;
	mock_cfg[0x08] = 0x14;			/* revision */
	mock_cfg[0x0b] = 0x02;			/* class: network controller */
	mock_cfg[0x0a] = 0x80;
	memset(descs, 0, sizeof(*descs));
	descs[0].vendor = 0x8086;
	descs[0].device = 0x08b1;
	descs[0].class_code = 0x028000;
	descs[0].revision = 0x14;
	descs[0].bus = 1;
	descs[0].host_dma_mask = 0xffffffffULL;
	descs[0].bar[0].phys = (u64)(uintptr_t)mock_bar;
	descs[0].bar[0].size = sizeof(mock_bar);
	descs[0].bar[0].flags = 0x200;		/* IORESOURCE_MEM */
	devs[0] = (struct lkpi_pcie_be_dev *)(uintptr_t)0x1;
	*count = 1;
	return 1;
}

static void mock_pci_bus_down(void *ctx) { (void)ctx; }

static int
mock_pci_cfg_read(void *ctx, struct lkpi_pcie_be_dev *d, u16 off, int size,
    u32 *val)
{
	(void)ctx; (void)d;
	if (off + (u16)size > sizeof(mock_cfg))
		return -LKPI_PCIE_BE_ERR_RANGE;
	*val = 0;
	memcpy(val, &mock_cfg[off], (size_t)size);
	return 0;
}

static int
mock_pci_cfg_write(void *ctx, struct lkpi_pcie_be_dev *d, u16 off, int size,
    u32 val)
{
	(void)ctx; (void)d;
	if (off + (u16)size > sizeof(mock_cfg))
		return -LKPI_PCIE_BE_ERR_RANGE;
	memcpy(&mock_cfg[off], &val, (size_t)size);
	return 0;
}

static int mock_pci_bar_map(void *ctx, struct lkpi_pcie_be_dev *d, int index,
    void **mmio)
{
	(void)ctx; (void)d;
	if (index != 0)
		return -LKPI_PCIE_BE_ERR_INVAL;
	*mmio = mock_bar;
	return 0;
}

static int
mock_pci_irq_setup(void *ctx, struct lkpi_pcie_be_dev *d, int kind,
    void (*handler)(void *), void *arg, int *irqn)
{
	(void)ctx; (void)d; (void)kind;
	mock_irq_handler = handler;
	mock_irq_arg = arg;
	*irqn = 9000;
	return 0;
}

static void
mock_pci_irq_clear(void *ctx, struct lkpi_pcie_be_dev *d, int irqn)
{
	(void)ctx; (void)d; (void)irqn;
	mock_irq_handler = NULL;
}

static int mock_pci_dma_alloc(void *ctx, size_t len, void **cpu, u64 *bus)
{
	(void)ctx;
	if (len > sizeof(mock_dma))
		return -LKPI_PCIE_BE_ERR_DMAMASK;
	*cpu = mock_dma;
	*bus = (u64)(uintptr_t)mock_dma;
	return 0;
}

static void mock_pci_dma_free(void *ctx, void *cpu, u64 bus, size_t len)
{
	(void)ctx; (void)cpu; (void)bus; (void)len;
}

static int
mock_pci_dma_map(void *ctx, void *cpu, size_t len, int dir, u64 *bus)
{
	(void)ctx; (void)cpu; (void)len; (void)dir;
	*bus = (u64)(uintptr_t)cpu;
	return 0;
}

static void mock_pci_dma_unmap(void *ctx, void *cpu, size_t len, int dir)
{
	(void)ctx; (void)cpu; (void)len; (void)dir;
}

static void mock_pci_dma_sync(void *ctx, void *cpu, size_t len, int dir)
{
	(void)ctx; (void)cpu; (void)len; (void)dir;
}

static const struct lkpi_pcie_be_ops mock_pci_ops = {
	.bus_up = mock_pci_bus_up,
	.bus_down = mock_pci_bus_down,
	.cfg_read = mock_pci_cfg_read,
	.cfg_write = mock_pci_cfg_write,
	.bar_map = mock_pci_bar_map,
	.irq_setup = mock_pci_irq_setup,
	.irq_clear = mock_pci_irq_clear,
	.dma_alloc = mock_pci_dma_alloc,
	.dma_free = mock_pci_dma_free,
	.dma_map = mock_pci_dma_map,
	.dma_unmap = mock_pci_dma_unmap,
	.dma_sync = mock_pci_dma_sync,
};

/* ------------------------------------------------------------------ */
/* the synthetic SDIO card                                             */
/* ------------------------------------------------------------------ */

static int
mock_sdio_card_init(void *ctx, struct lkpi_sdio_be_card_info *info)
{
	(void)ctx;
	memset(info, 0, sizeof(*info));
	info->vendor = 0x024c;			/* the bench's Realtek card */
	info->device = 0xc821;
	info->func_count = 1;
	info->fn0_max_block = 512;
	info->fn_max_block[1] = 512;
	info->cccr_version = 3;
	info->sdio_version = 3;
	info->clock_hz = 50000000;
	info->host_max_blk_size = 512;
	info->host_max_blk_count = 1;
	info->bus_width = 4;
	info->high_speed = true;
	info->multi_block = true;
	return 0;
}

static void mock_sdio_card_deinit(void *ctx) { (void)ctx; }

static int
mock_sdio_io_direct(void *ctx, unsigned int func, unsigned int addr, bool write,
    u8 *value)
{
	(void)ctx; (void)func; (void)addr;
	if (!write)
		*value = 0x42;
	return 0;
}

static int
mock_sdio_io_extended(void *ctx, unsigned int func, unsigned int addr, void *buf,
    unsigned int count, unsigned int flags)
{
	(void)ctx; (void)func; (void)addr;
	if ((flags & LKPI_SDIO_BE_F_WRITE) == 0)
		memset(buf, 0x5a, count);
	return 0;
}

static int mock_sdio_set_block_size(void *ctx, unsigned int func, unsigned int size)
{
	(void)ctx; (void)func; (void)size;
	return 0;
}

static int mock_sdio_irq_pending(void *ctx, u8 *pending)
{
	(void)ctx;
	*pending = 0;
	return 0;
}

static const struct lkpi_sdio_be_ops mock_sdio_ops = {
	.card_init = mock_sdio_card_init,
	.card_deinit = mock_sdio_card_deinit,
	.io_direct = mock_sdio_io_direct,
	.io_extended = mock_sdio_io_extended,
	.set_block_size = mock_sdio_set_block_size,
	.irq_pending = mock_sdio_irq_pending,
	.card_irq_mode = NULL,
};

/* ------------------------------------------------------------------ */
/* the null egress: the second implementation of the network seam      */
/* ------------------------------------------------------------------ */

static unsigned int mock_net_rx_frames;

void
lkpi_netdev_rx(struct sk_buff *skb)
{
	mock_net_rx_frames++;
	kfree_skb(skb);
}

/* ------------------------------------------------------------------ */

int
case_kpi_mock_run(void)
{
	struct pci_dev *pdev;
	struct sdio_func *func;
	struct sk_buff *skb;
	u16 vendor = 0;
	u8 byte = 0;
	int err = 0;

	lkpi_core_init();
	CASE_ANCHOR("kpi: core ready\n");

	/* --- PCI engine over the synthetic host --- */
	lkpi_pcie_be_register(&mock_pci_ops, NULL);
	lkpi_pci_start();
	if (!lkpi_pci_ready() || lkpi_pci_dev_count_get() != 1) {
		CASE_ANCHOR("kpi: FAIL mock pci devs=%u\n",
			    lkpi_pci_dev_count_get());
		return 1;
	}
	pdev = lkpi_pci_dev_get(0);
	if (pdev == NULL || pci_read_config_word(pdev, 0x00, &vendor) != 0 ||
	    vendor != 0x8086) {
		CASE_ANCHOR("kpi: FAIL mock pci config\n");
		return 1;
	}
	CASE_ANCHOR("kpi: mock pci devs=1 %04x:%04x\n",
		    (unsigned)pdev->vendor, (unsigned)pdev->device);

	/* --- SDIO engine over the synthetic card --- */
	lkpi_sdio_be_register(&mock_sdio_ops, NULL);
	lkpi_sdio_start();
	if (!lkpi_sdio_ready()) {
		CASE_ANCHOR("kpi: FAIL mock sdio not ready\n");
		return 1;
	}
	func = lkpi_sdio_func(1);
	if (func == NULL) {
		CASE_ANCHOR("kpi: FAIL mock sdio no function 1\n");
		return 1;
	}
	sdio_claim_host(func);
	byte = sdio_readb(func, 0x00, &err);
	sdio_release_host(func);
	if (err != 0 || byte != 0x42) {
		CASE_ANCHOR("kpi: FAIL mock sdio c52 byte=%02x err=%d\n",
			    (unsigned)byte, err);
		return 1;
	}
	CASE_ANCHOR("kpi: mock sdio card=024c:c821 funcs=1 c52=%02x\n",
		    (unsigned)byte);

	/* --- the net egress seam: netif_rx must reach the registered egress --- */
	skb = __dev_alloc_skb(64, GFP_KERNEL);
	if (skb == NULL || skb_put(skb, 64) == NULL) {
		CASE_ANCHOR("kpi: FAIL mock net skb\n");
		return 1;
	}
	(void)netif_rx(skb);
	CASE_ANCHOR("kpi: mock net rx=%u\n", mock_net_rx_frames);

	CASE_ANCHOR("kpi: PASS\n");
	return 0;
}
