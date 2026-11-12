/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   lkpi_pcie_port.h
 * @brief  Install the linuxkpi PCI engine's board backend.
 *
 * The board image calls this once before it starts the engine; the host suite
 * calls it to drive the same adapter against its own scripted board drivers.
 *
 * The A4 stall round also reads the AC 7260's four interrupt-status registers
 * from a task after the host has gone quiet.  That surface is here, not in the
 * Linux face: the registers belong to the function's first window, which only
 * this adapter knows.
 *
 * @author zhugengyu
 * @date   12.10.2026
 */

#ifndef FREEWIFICAM_LKPI_PCIE_PORT_H
#define FREEWIFICAM_LKPI_PCIE_PORT_H

#include <stdint.h>

/* Register the adapter's backend with the engine (idempotent). */
void lkpi_pcie_port_install(void);

/* One interrupt-status snapshot: the four registers the interrupt trace reads,
 * taken either at an interrupt or from a task (the diagnostic profile). */
struct lkpi_pcie_csr_snap {
	unsigned long	tick;
	uint32_t	int_status;
	uint32_t	int_mask;
	uint32_t	fh_status;
	uint32_t	reset;
};

/* Read the four status registers now (0 = read, -1 = no function/snapshot
 * support compiled in).  Compiled in only with
 * CONFIG_LKPI_PCIE_CSR_SNAPSHOT; the normal images keep the call out. */
int lkpi_pcie_csr_snapshot(struct lkpi_pcie_csr_snap *out);

/* Copy the interrupt-time ring, oldest first (0 = no delivery recorded yet). */
unsigned int lkpi_pcie_csr_irq_ring(struct lkpi_pcie_csr_snap *out,
    unsigned int max);

#endif /* FREEWIFICAM_LKPI_PCIE_PORT_H */
