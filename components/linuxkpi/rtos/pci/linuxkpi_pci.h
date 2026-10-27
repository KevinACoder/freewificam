/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_pci.h
 * @brief   The linuxkpi PCI engine's entries: bring-up, the diagnostic counters,
 *          and the interrupt dispatch the Linux face deliberately does not carry.
 *
 * @design ports & adapters, the engine half: the driver-facing face is
 *         include/linux/pci.h and include/linux/dma-mapping.h, the host is reached
 *         through lkpi_pcie_backend.h, and this header is what a board image's
 *         bring-up (and the host suite) calls to start the engine and read its
 *         state. Reference for the shape: components/linuxkpi/usb/linuxkpi_usb.h:1-40.
 * @date    12.10.2026
 */

#ifndef LINUXKPI_PCI_H
#define LINUXKPI_PCI_H

#include "linuxkpi_core.h"
#include <linux/pci.h>

/* Start the engine: install nothing (a backend must already be registered), bring
 * the bus up through the seam, build the device records and probe every
 * registered driver. Idempotent: a second call re-runs the probe pass only. */
void	lkpi_pci_start(void);

/* non-zero once the bus came up */
bool	lkpi_pci_ready(void);

/* how many drivers are registered and how many devices enumerated (the bring-up
 * and its tests read these) */
unsigned int	lkpi_pci_driver_count_get(void);
unsigned int	lkpi_pci_dev_count_get(void);

/* the engine's device record at `index`, or NULL (the suite reads the identity it
 * built from the backend's record) */
struct pci_dev	*lkpi_pci_dev_get(unsigned int index);

/* Ask the backend for one vector of `kind` (LKPI_PCIE_BE_IRQ_MSI / _MSIX) and
 * return the INTID to register a handler for. The Linux face carries only MSI
 * (pci_enable_msi); the MSI-X path is this entry, used by the adapter's second
 * route and by the verification unit (no consumer driver names pci_enable_msix). */
int	lkpi_pci_irq_setup(struct pci_dev *pdev, int kind, int *irqn);
void	lkpi_pci_irq_clear(struct pci_dev *pdev, int kind, int irqn);

struct lkpi_pci_stats {
	unsigned int	probes;		/* successful driver probes */
	unsigned int	probe_errors;	/* probes that ran and failed */
	unsigned int	irqs;		/* interrupts delivered to a handler */
	unsigned int	irq_threads;	/* threaded halves actually run */
	unsigned int	msi;		/* MSI vectors allocated */
	unsigned int	dma_maps;	/* streaming maps */
	unsigned int	errors;		/* seam calls that returned an error */
};

const struct lkpi_pci_stats *lkpi_pci_stats_get(void);

/*
 * The two interrupt counters the join case reads (scalars only, so a net80211
 * case never has to include the Linux device model).  `hard` is how many times
 * the delivery path reached the driver's hard handler; `thread` is how many
 * threaded halves actually ran.  A frozen receive path with `hard` still rising
 * is a bottom-half problem; with `hard` frozen it is an interrupt (or device)
 * problem - the distinction the join investigation needs.
 */
void	lkpi_pci_irq_counters(unsigned int *hard, unsigned int *thread);

#endif /* LINUXKPI_PCI_H */
