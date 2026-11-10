/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   kpi_live.c
 * @brief  Case kpi_live: the liblinux80211 PCI engine against the board's
 *         real root complex - the component's first on-target proof.
 *
 * What this proves, and what it does not: the engine's Linux-facing records
 * are built from a real bus enumeration through the RK3568 backend adapter
 * (ports/pci-rk3568), and the board's two functions are named with the
 * identity the engine read.  No driver attaches here; attaching is the
 * wireless line's business.
 *
 * Anchors:
 *   kpi: core ready
 *   kpi: pci devs=<n>
 *   kpi: dev <vid:did> class <cc> rev <r>
 *   kpi: PASS
 */

#include <stdint.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linux80211/linux80211.h"
#include "linuxkpi_pci.h"
#include "adapter.h"

int
case_kpi_live_run(void)
{
	unsigned int i;
	unsigned int n;
	int saw_wifi = 0;
	int saw_nvme = 0;

	lkpi_core_init();
	CASE_ANCHOR("kpi: core ready\n");

	lkpi_pcie_port_install();
	lkpi_pci_start();

	if (!lkpi_pci_ready()) {
		CASE_ANCHOR("kpi: FAIL pci engine not ready\n");
		return 1;
	}
	n = lkpi_pci_dev_count_get();
	CASE_ANCHOR("kpi: pci devs=%u\n", n);
	if (n == 0) {
		CASE_ANCHOR("kpi: FAIL no function enumerated\n");
		return 2;	/* platform-inactive: no link/endpoint */
	}
	for (i = 0; i < n; i++) {
		struct pci_dev *dev = lkpi_pci_dev_get(i);

		if (dev == NULL)
			continue;
		CASE_INFO("kpi: dev %04x:%04x class %06x rev %02x\n",
			  (unsigned)dev->vendor, (unsigned)dev->device,
			  (unsigned)dev->class, (unsigned)dev->revision);
		if (dev->vendor == 0x8086 && dev->device == 0x08b1)
			saw_wifi = 1;
		if (dev->vendor == 0x126f && dev->device == 0x2263)
			saw_nvme = 1;
	}
	CASE_ANCHOR("kpi: known wifi=%d nvme=%d\n", saw_wifi, saw_nvme);
	CASE_ANCHOR("kpi: PASS\n");
	return 0;
}
