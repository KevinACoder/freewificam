/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file    main.c
 * @brief   The PCI adapter and message-interrupt domain host suite: the
 *          assertion face.
 *
 * One process, one run. Three groups are exercised against the scripted board
 * drivers in mock_board.c:
 *
 *   A4  the adapter (ports/pcie/lkpi_pcie_port.c) driven end to end by the
 *       engine: enumeration and the identity records it builds, config-space
 *       pass-through and its error mapping, the BAR window, the interrupt arm
 *       and release (with the LPI enable/disable the adapter owes), the MSI-X
 *       no-vector mapping, and the DMA direction maintenance.
 *   A5  the message-interrupt domain itself (arch/aarch64/gicv3_msi.c) against
 *       the scripted ITS primitives: the requester id it hands down, the event-id
 *       payload it hands back, the one-set-per-function rule, the unwind on a
 *       failed map, and the release.
 *   A5r the arch registry (arch/aarch64/gicv3.c): an LPI and an SPI registered
 *       side by side, each dispatched to its own handler, with an unregistered
 *       LPI reaching no one.
 *
 * The anchors it prints (prefix "pcie-adapter: ") are the machine-checkable
 * result; the exit code is zero only when every assertion held.
 *
 * Modes: the default run above; "--fault cfg-error" makes the scripted config
 * read fail and must go red (the pass-through anchor cannot match).
 *
 * @design composition over the sibling host suites - ref
 *         tests/host/linuxkpi-usb-test/main.c and tests/host/linuxkpi-pcie-test/main.c
 *         (scripted counterpart + anchors + a named fault invocation).
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
#include "lkpi_pcie_port.h"
#include "mock_board.h"

static int failures;
static const char *g_fault;

static void
anchor(int held, const char *fmt, ...)
{
	va_list ap;

	printf("pcie-adapter: ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	fflush(stdout);
	if (!held)
		failures++;
}

/* ------------------------------------------------------------------ */
/* a driver stand-in, so the probe path runs                           */
/* ------------------------------------------------------------------ */

static unsigned int g_probes;
static struct pci_dev *g_probed;

static int
probe_dev(struct pci_dev *dev, const struct pci_device_id *id)
{

	(void)id;
	g_probes++;
	g_probed = dev;
	return 0;
}

static const struct pci_device_id probe_ids[] = {
	{ PCI_DEVICE(0x1234, 0x5678) },
	{ }
};

/* ------------------------------------------------------------------ */
/* the interrupt handlers                                              */
/* ------------------------------------------------------------------ */

static unsigned int g_irq_count;

static irqreturn_t
irq_handler(int irq, void *dev_id)
{

	(void)irq;
	(void)dev_id;
	g_irq_count++;
	return IRQ_HANDLED;
}

/* ------------------------------------------------------------------ */
/* the run                                                             */
/* ------------------------------------------------------------------ */

static void
run(void)
{
	struct mock_board *b = mock_board();
	struct pci_dev *dev0, *dev1;
	const MSI_DOMAIN *domain;
	struct pci_driver drv;
	MSI_VECTOR vec[4];
	int rc, irq = 0;
	uint32_t v32 = 0;
	u8 b8 = 0;

	/* ---- A4: enumeration and the identity records ---------------- */
	dev0 = lkpi_pci_dev_get(0);
	dev1 = lkpi_pci_dev_get(1);
	anchor(lkpi_pci_ready() && lkpi_pci_dev_count_get() == 2 &&
	    b->init_calls == 1,
	    "bus up devs=%u init=%u", lkpi_pci_dev_count_get(), b->init_calls);
	anchor(dev0 != NULL && dev0->vendor == 0x1234 && dev0->device == 0x5678 &&
	    dev0->class == 0x028000u,
	    "desc dev0 id=%04x:%04x class=0x%06x",
	    dev0 != NULL ? dev0->vendor : 0, dev0 != NULL ? dev0->device : 0,
	    dev0 != NULL ? dev0->class : 0);
	anchor(dev1 != NULL && dev1->device == 0x5679 && dev1->class == 0x020000u,
	    "desc dev1 id=%04x:%04x class=0x%06x",
	    dev1 != NULL ? dev1->vendor : 0, dev1 != NULL ? dev1->device : 0,
	    dev1 != NULL ? dev1->class : 0);

	drv.name = "mock-adapter";
	drv.id_table = probe_ids;
	drv.probe = probe_dev;
	(void)pci_register_driver(&drv);
	anchor(g_probes == 1 && g_probed == dev0, "driver probed dev0 ok=%d",
	    g_probes == 1);

	/* ---- A4: config pass-through and the error mapping ----------- */
	(void)pci_write_config_dword(dev0, 0x00, 0x56781234u);
	rc = pci_read_config_dword(dev0, 0x00, &v32);
	anchor(rc == 0 && v32 == 0x56781234u, "cfg passthrough dword=0x%08x", v32);

	b->cfg_fail_dev = 0;
	b8 = 0;
	rc = pci_read_config_byte(dev0, 0x00, &b8);
	anchor(rc == -LKPI_PCIE_BE_ERR_XFER && b8 == 0xff,
	    "cfg error rc=%d val=0x%02x", rc, b8);

	/* ---- A4: the BAR window ------------------------------------- */
	{
		void *mmio = pci_ioremap_bar(dev0, 0);

		anchor(mmio == (void *)(uintptr_t)b->dev[0].bar[0] &&
		    pci_resource_start(dev0, 0) == b->dev[0].bar[0],
		    "bar map identity ok=%d", mmio != NULL);
	}

	/* ---- A4: the interrupt arm, delivery and release ------------- */
	rc = pci_enable_msi(dev0);
	anchor(rc == 0 && dev0->irq == 8192u && b->msi_allocs == 1 &&
	    b->lpi_enables == 1,
	    "msi arm intid=%u allocs=%u enables=%u", dev0->irq, b->msi_allocs,
	    b->lpi_enables);

	g_irq_count = 0;
	(void)request_irq(dev0->irq, irq_handler, 0, "mock-adapter", NULL);
	mock_board_dispatch(8192u);
	anchor(g_irq_count == 1, "irq dispatch handler n=%u ok=%d", g_irq_count,
	    g_irq_count == 1);

	free_irq(dev0->irq, NULL);
	pci_disable_msi(dev0);
	anchor(b->msi_releases == 1 && b->lpi_disables == 1,
	    "msi release releases=%u disables=%u", b->msi_releases,
	    b->lpi_disables);

	/* ---- A4: the MSI-X no-vector mapping ------------------------ */
	b->msix_unsupported = true;
	rc = lkpi_pci_irq_setup(dev1, LKPI_PCIE_BE_IRQ_MSIX, &irq);
	b->msix_unsupported = false;
	anchor(rc == -LKPI_PCIE_BE_ERR_NOVECTOR, "msix no-vector rc=%d", rc);

	/* ---- A4: the DMA direction maintenance ---------------------- */
	{
		dma_addr_t a;
		unsigned int flush0 = b->cache_flushes;
		unsigned int inv0 = b->cache_invalidates;

		a = dma_map_single(&dev0->dev, b->dev[0].bar, 32, DMA_TO_DEVICE);
		dma_unmap_single(&dev0->dev, a, 32, DMA_TO_DEVICE);
		anchor(b->cache_flushes == flush0 + 1, "dma map tx flush=%u ok=%d",
		    b->cache_flushes - flush0, b->cache_flushes == flush0 + 1);

		a = dma_map_single(&dev0->dev, b->dev[0].bar, 32, DMA_FROM_DEVICE);
		dma_unmap_single(&dev0->dev, a, 32, DMA_FROM_DEVICE);
		anchor(b->cache_invalidates > inv0,
		    "dma map rx invalidate=%u ok=%d",
		    b->cache_invalidates - inv0, b->cache_invalidates > inv0);
	}
	{
		dma_addr_t bus = 0;
		void *cpu = dma_alloc_coherent(&dev0->dev, 128, &bus, 0);
		int ok = (cpu != NULL && bus != 0);

		dma_free_coherent(&dev0->dev, 128, cpu, bus);
		anchor(ok, "dma coherent ok=%d", ok);
	}

	/* ---- A5: the message-interrupt domain ----------------------- */
	domain = msi_domain_get();
	anchor(domain != NULL, "domain present ok=%d", domain != NULL);
	if (domain != NULL) {
		unsigned int map0 = b->its_map_n;

		memset(vec, 0, sizeof(vec));
		rc = (int)domain->Allocate(0x0100u, 1, vec);
		anchor(rc == 1 && vec[0].intid == 8192u && vec[0].data == 0 &&
		    vec[0].address == b->its_doorbell,
		    "domain alloc n=%d intid=%u data=%u addr=%#llx", rc,
		    vec[0].intid, vec[0].data, (unsigned long long)vec[0].address);
		anchor(b->its_attach_n == 1 && b->its_attach_rid[0] == 0x0100u &&
		    b->its_map_n == map0 + 1 && b->its_map_rid[map0] == 0x0100u &&
		    b->its_map_event[map0] == 0,
		    "domain rid attach=%u map=%u", b->its_attach_n,
		    b->its_map_n - map0);

		rc = (int)domain->Allocate(0x0100u, 1, vec);
		anchor(rc < 0, "domain second alloc refused ok=%d", rc < 0);

		{
			unsigned int un0 = b->its_unmap_n;

			domain->Free(0x0100u);
			anchor(b->its_unmap_n == un0 + 1, "domain free unmaps=%u ok=%d",
			    b->its_unmap_n - un0, b->its_unmap_n == un0 + 1);
		}

		/* a four-vector request whose third map fails must unwind the two
		 * it already made */
		{
			unsigned int un0 = b->its_unmap_n;

			b->its_fail_at_event = (int)b->its_map_n + 2;
			rc = (int)domain->Allocate(0x0200u, 4, vec);
			b->its_fail_at_event = -1;
			anchor(rc < 0 && b->its_unmap_n == un0 + 2,
			    "domain fail unwinds n=%u ok=%d", b->its_unmap_n - un0,
			    b->its_unmap_n == un0 + 2);
		}
	}

	/* The arch registry's own LPI / SPI coexistence lives in the board suite:
	 * arch/aarch64/gicv3.c carries its system-register accesses as inline
	 * aarch64 asm in the same translation unit as the registry, so it cannot be
	 * compiled for the host, and the line's boundary rules keep this round from
	 * splitting it.  The registry's dispatch logic is asserted on the board
	 * instead (threadx-rk3568-pcie/gicv3-lpi-registry). */
}

static void
app_task(void *arg)
{

	(void)arg;
	mock_board_reset();
	if (g_fault != NULL && strcmp(g_fault, "cfg-error") == 0) {
		mock_board()->cfg_fail_dev = 0;
		mock_board()->cfg_fail_forever = true;
	}

	lkpi_pcie_port_install();
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
			printf("pcie-adapter: usage: %s [--fault cfg-error]\n", argv[0]);
			return 2;
		}
	}

	if (osKernelInitialize() != osOK) {
		printf("pcie-adapter: kernel init failed\n");
		return 2;
	}
	attr.name = "pcie-adapter";
	attr.stack_size = 32768u;
	attr.priority = osPriorityNormal;
	t = osThreadNew(app_task, NULL, &attr);
	if (t == NULL) {
		printf("pcie-adapter: cannot create the application task\n");
		return 2;
	}
	if (osKernelStart() != osOK)
		return 2;
	return failures == 0 ? 0 : 1;
}
