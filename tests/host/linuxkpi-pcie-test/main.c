/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * main.c - the linuxkpi PCI host mock suite: the assertion face.
 *
 * One process, one run: the synthetic host and its three scripted functions come
 * up behind the engine's backend seam, two clean-room drivers register before the
 * bus enumerates and a third registers after it, and every group asserts as it
 * goes. The anchors it prints (prefix "pcie-smoke: ") are the machine-checkable
 * result; the exit code is zero only when every assertion held.
 *
 * Groups (the round's set):
 *   G1  enumeration and the identity records the engine built
 *   G2  config space: sizes, the unimplemented-offset answer, the error path
 *   G3  enable and bus-master (the COMMAND bits)
 *   G4  the capability walk, the BAR window and its write/read
 *   G5  MSI: allocate, deliver, the busy re-request, release
 *   G6  MSI-X single vector, and the no-vector path
 *   G7  DMA: coherent conservation, the streaming direction order, the mask
 *   G8  the driver registry: the wildcard match, the late claim, the counters
 *
 * Modes: the default run above; "--fault cfg-tamper" perturbs a config byte the
 * identity assertion reads and must go red; "--fault lying-host" makes the
 * scripted doorbell deliver nothing and must go red.
 *
 * @design composition over the sibling mock suites - ref
 *         tests/host/linuxkpi-usb-test/main.c (synthetic host + scripted device +
 *         anchors + a named fault invocation); the runtime lifecycle (task start,
 *         scheduler end) follows it too.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>

#include "lkpi_pcie_backend.h"
#include "linuxkpi_pci.h"
#include "mock_pcie_hcd.h"

/* ------------------------------------------------------------------ */
/* the run's bookkeeping                                               */
/* ------------------------------------------------------------------ */

static int failures;
static const char *g_fault;

static void
anchor(int held, const char *fmt, ...)
{
	va_list ap;

	printf("pcie-smoke: ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	fflush(stdout);
	if (!held)
		failures++;
}

/* ------------------------------------------------------------------ */
/* the driver stand-in: three clean-room drivers                       */
/* ------------------------------------------------------------------ */

struct mock_driver {
	struct pci_driver	drv;
	unsigned int		probes;
	struct pci_dev		*last;
};

static struct mock_driver drv_a, drv_b, drv_c;

static int
probe_common(struct pci_dev *dev, struct mock_driver *m)
{

	m->probes++;
	m->last = dev;
	pci_set_drvdata(dev, m);
	return 0;
}

static int
probe_a(struct pci_dev *dev, const struct pci_device_id *id)
{

	(void)id;
	return probe_common(dev, &drv_a);
}

static int
probe_b(struct pci_dev *dev, const struct pci_device_id *id)
{

	(void)id;
	return probe_common(dev, &drv_b);
}

static int
probe_c(struct pci_dev *dev, const struct pci_device_id *id)
{

	(void)id;
	return probe_common(dev, &drv_c);
}

/* an exact id, a wildcard id and an id whose device only appears unclaimed */
static const struct pci_device_id ids_a[] = {
	{ PCI_DEVICE(0x1234, 0x5678) },
	{ }
};

static const struct pci_device_id ids_b[] = {
	{ .vendor = 0x1234, .device = PCI_ANY_ID,
	  .subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID },
	{ }
};

static const struct pci_device_id ids_c[] = {
	{ PCI_DEVICE(0xabcd, 0x0001) },
	{ }
};

/* ------------------------------------------------------------------ */
/* the interrupt handler                                               */
/* ------------------------------------------------------------------ */

static unsigned int g_irq_count;
static int g_irq_last;

static irqreturn_t
irq_handler(int irq, void *dev_id)
{

	(void)dev_id;
	g_irq_count++;
	g_irq_last = irq;
	return IRQ_HANDLED;
}

/* ------------------------------------------------------------------ */
/* the run                                                             */
/* ------------------------------------------------------------------ */

static void
run(void)
{
	struct mock_pcie_backend *b = mock_pcie_backend();
	struct pci_dev *dev0, *dev1;
	struct lkpi_pci_stats stats_copy;
	const struct lkpi_pci_stats *stats;
	u8 b8 = 0;
	u16 w16 = 0;
	u32 v32 = 0;
	int rc, irq = 0;

	/* ---- G1: enumeration and identity ---------------------------- */
	dev0 = lkpi_pci_dev_get(0);
	dev1 = lkpi_pci_dev_get(1);
	anchor(lkpi_pci_ready() && lkpi_pci_dev_count_get() == 3,
	    "hcd registered devs=%u", lkpi_pci_dev_count_get());
	anchor(dev0 != NULL && dev0->vendor == 0x1234 && dev0->device == 0x5678 &&
	    dev0->class == 0x020000u,
	    "dev %s id=%04x:%04x class=0x%06x", dev0 != NULL ? pci_name(dev0) : "?",
	    dev0 != NULL ? dev0->vendor : 0, dev0 != NULL ? dev0->device : 0,
	    dev0 != NULL ? dev0->class : 0);
	anchor(dev1 != NULL && dev1->device == 0x5679 && dev1->class == 0x028000u,
	    "dev %s id=%04x:%04x class=0x%06x", dev1 != NULL ? pci_name(dev1) : "?",
	    dev1 != NULL ? dev1->vendor : 0, dev1 != NULL ? dev1->device : 0,
	    dev1 != NULL ? dev1->class : 0);

	/* ---- G2: config space --------------------------------------- */
	rc = pci_read_config_dword(dev0, MOCK_CFG_VENDOR, &v32);
	anchor(rc == 0 && v32 == 0x56781234u, "cfg dword vendor-device=0x%08x", v32);

	{
		u8 rb = 0;
		u16 rw = 0;
		u32 rd = 0;

		(void)pci_read_config_byte(dev0, MOCK_CFG_VENDOR, &rb);
		(void)pci_read_config_word(dev0, MOCK_CFG_VENDOR, &rw);
		(void)pci_read_config_dword(dev0, MOCK_CFG_VENDOR, &rd);
		anchor(rb == 0x34 && rw == 0x1234 && rd == 0x56781234u,
		    "cfg sizes byte=0x%02x word=0x%04x dword=0x%08x", rb, rw, rd);
	}

	v32 = 0;
	rc = pci_read_config_dword(dev0, 0x100, &v32);
	anchor(rc == 0 && v32 == 0xffffffffu, "cfg unimplemented=0x%08x", v32);

	mock_pcie_set_cfg_fail(MOCK_CFG_VENDOR);
	b8 = 0;
	rc = pci_read_config_byte(dev0, MOCK_CFG_VENDOR, &b8);
	anchor(rc == -LKPI_PCIE_BE_ERR_TIMEOUT && b8 == 0xff,
	    "cfg error rc=%d val=0x%02x", rc, b8);

	/* ---- G3: enable and bus master ------------------------------ */
	(void)pci_enable_device(dev0);
	w16 = 0;
	(void)pci_read_config_word(dev0, PCI_COMMAND, &w16);
	anchor(w16 == PCI_COMMAND_MEMORY, "enable command=0x%04x", w16);

	pci_set_master(dev0);
	w16 = 0;
	(void)pci_read_config_word(dev0, PCI_COMMAND, &w16);
	anchor(w16 == (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER),
	    "master command=0x%04x", w16);

	/* ---- G4: capabilities and the BAR window -------------------- */
	{
		int msi = pci_find_capability(dev0, PCI_CAP_ID_MSI);
		int msix = pci_find_capability(dev0, PCI_CAP_ID_MSIX);

		anchor(msi == MOCK_CFG_MSI_CAP && msix == MOCK_CFG_MSIX_CAP,
		    "cap msi=0x%02x msix=0x%02x", msi, msix);
	}
	{
		resource_size_t len = pci_resource_len(dev0, 0);
		volatile u8 *p = (volatile u8 *)pci_ioremap_bar(dev0, 0);
		int ok = 0;

		if (p != NULL) {
			p[0x10] = 0xde;
			p[0x11] = 0xad;
			p[0x12] = 0xbe;
			p[0x13] = 0xef;
			ok = (p[0x10] == 0xde && p[0x11] == 0xad &&
			    p[0x12] == 0xbe && p[0x13] == 0xef);
		}
		anchor(p != NULL && len == MOCK_PCIE_BAR_SIZE && ok,
		    "bar0 len=%u write-read ok=%d", (unsigned)len, ok);
	}

	/* ---- G5: MSI ------------------------------------------------ */
	rc = pci_enable_msi(dev0);
	anchor(rc == 0 && dev0->irq == 8192u, "msi enable irq=%u ok=%d",
	    dev0->irq, rc == 0);

	g_irq_count = 0;
	g_irq_last = 0;
	rc = request_irq(dev0->irq, irq_handler, 0, "mock-pcie", NULL);
	mock_pcie_fire_irq();
	anchor(rc == 0 && g_irq_count == 1 && g_irq_last == 8192,
	    "msi deliver n=%u ok=%d", g_irq_count, g_irq_count == 1);

	anchor(pci_enable_msi(dev0) == -EBUSY, "msi re-enable busy ok=1");

	free_irq(dev0->irq, NULL);
	pci_disable_msi(dev0);
	anchor(dev0->irq == 0u && !dev0->msi_enabled, "msi release ok=1");

	/* ---- G6: MSI-X ---------------------------------------------- */
	g_irq_count = 0;
	g_irq_last = 0;
	rc = lkpi_pci_irq_setup(dev0, LKPI_PCIE_BE_IRQ_MSIX, &irq);
	anchor(rc == 0 && irq == 8193, "msix vector irq=%d ok=%d", irq, rc == 0);
	rc = request_irq((unsigned int)irq, irq_handler, 0, "mock-pcie", NULL);
	mock_pcie_fire_irq();
	anchor(rc == 0 && g_irq_count == 1, "msix deliver n=%u ok=%d",
	    g_irq_count, g_irq_count == 1);
	free_irq((unsigned int)irq, NULL);
	lkpi_pci_irq_clear(dev0, LKPI_PCIE_BE_IRQ_MSIX, irq);

	mock_pcie_set_msix_available(false);
	rc = lkpi_pci_irq_setup(dev1, LKPI_PCIE_BE_IRQ_MSIX, &irq);
	mock_pcie_set_msix_available(true);
	anchor(rc == -LKPI_PCIE_BE_ERR_NOVECTOR, "msix no-vector rc=%d ok=%d",
	    rc, rc == -LKPI_PCIE_BE_ERR_NOVECTOR);

	/* ---- G7: DMA ------------------------------------------------ */
	{
		dma_addr_t bus = 0;
		void *cpu = dma_alloc_coherent(&dev0->dev, 64, &bus, 0);
		int ok = (cpu != NULL && bus != 0);

		dma_free_coherent(&dev0->dev, 64, cpu, bus);
		anchor(ok && b->dma_allocs == 1 && b->dma_frees == 1,
		    "dma coherent conserve ok=%d", ok);
	}
	{
		void *buf = b->dev[0].bar;
		dma_addr_t a;

		b->dma_dir_n = 0;
		a = dma_map_single(&dev0->dev, buf, 32, DMA_TO_DEVICE);
		dma_unmap_single(&dev0->dev, a, 32, DMA_TO_DEVICE);
		a = dma_map_single(&dev0->dev, buf, 32, DMA_FROM_DEVICE);
		dma_unmap_single(&dev0->dev, a, 32, DMA_FROM_DEVICE);
		anchor(b->dma_dir_n == 4 &&
		    b->dma_dir_log[0] == LKPI_PCIE_BE_DMA_TO_DEVICE &&
		    b->dma_dir_log[2] == LKPI_PCIE_BE_DMA_FROM_DEVICE,
		    "dma map dirs flush=%d invalidate=%d ok=%d",
		    b->dma_dir_log[0] == LKPI_PCIE_BE_DMA_TO_DEVICE,
		    b->dma_dir_log[2] == LKPI_PCIE_BE_DMA_FROM_DEVICE,
		    b->dma_dir_n == 4);
	}
	{
		int ok = dma_set_mask_and_coherent(&dev0->dev, 0xffffffffu) == 0 &&
		    dma_set_mask_and_coherent(&dev0->dev, 0x100000000ULL) ==
		    -LKPI_PCIE_BE_ERR_DMAMASK;
		anchor(ok, "dma mask ok=%d", ok);
	}

	/* ---- G8: the driver registry -------------------------------- */
	drv_c.drv.name = "mock-c";
	drv_c.drv.id_table = ids_c;
	drv_c.drv.probe = probe_c;
	(void)pci_register_driver(&drv_c.drv);
	anchor(drv_c.probes == 1 && drv_c.last != NULL &&
	    strcmp(pci_name(drv_c.last), "03:00.0") == 0,
	    "driver late-claim dev=%s ok=%d",
	    drv_c.last != NULL ? pci_name(drv_c.last) : "?",
	    drv_c.probes == 1);

	stats = lkpi_pci_stats_get();
	stats_copy = *stats;
	anchor(stats_copy.probes == 3 && stats_copy.probe_errors == 0,
	    "probe stats ok=%u errors=%u", stats_copy.probes,
	    stats_copy.probe_errors);
}

static void
app_task(void *arg)
{

	(void)arg;
	mock_pcie_reset();
	if (g_fault != NULL && strcmp(g_fault, "cfg-tamper") == 0)
		mock_pcie_backend()->dev[0].cfg[MOCK_CFG_VENDOR] ^= 0xff;
	if (g_fault != NULL && strcmp(g_fault, "lying-host") == 0)
		mock_pcie_set_lying(true);

	lkpi_pcie_be_register(mock_pcie_ops(), mock_pcie_backend());

	drv_a.drv.name = "mock-a";
	drv_a.drv.id_table = ids_a;
	drv_a.drv.probe = probe_a;
	drv_b.drv.name = "mock-b";
	drv_b.drv.id_table = ids_b;
	drv_b.drv.probe = probe_b;
	(void)pci_register_driver(&drv_a.drv);
	(void)pci_register_driver(&drv_b.drv);

	lkpi_pci_start();

	run();

	if (failures == 0)
		anchor(1, "PASS");
	else
		anchor(0, "FAIL failures=%d", failures);
	cmsis2_host_end_scheduler();
}

int
main(int argc, char **argv)
{
	osThreadId_t t;
	osThreadAttr_t attr = { 0 };
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--fault") == 0 && i + 1 < argc) {
			g_fault = argv[++i];
		} else {
			printf("pcie-smoke: usage: %s [--fault cfg-tamper|lying-host]\n",
			    argv[0]);
			return 2;
		}
	}

	if (osKernelInitialize() != osOK) {
		printf("pcie-smoke: kernel init failed\n");
		return 2;
	}
	attr.name = "pcie-mock";
	attr.stack_size = 32768u;
	attr.priority = osPriorityNormal;
	t = osThreadNew(app_task, NULL, &attr);
	if (t == NULL) {
		printf("pcie-smoke: cannot create the application task\n");
		return 2;
	}
	if (osKernelStart() != osOK)
		return 2;
	return failures == 0 ? 0 : 1;
}
