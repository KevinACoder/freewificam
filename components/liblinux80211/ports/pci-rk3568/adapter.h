/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   lkpi_pcie_port.h
 * @brief  Install the linuxkpi PCI engine's board backend.
 *
 * The board image calls this once before it starts the engine; the host suite
 * calls it to drive the same adapter against its own scripted board drivers.
 *
 * @author zhugengyu
 * @date   12.10.2026
 */

#ifndef FREEWIFICAM_LKPI_PCIE_PORT_H
#define FREEWIFICAM_LKPI_PCIE_PORT_H

/* Register the adapter's backend with the engine (idempotent). */
void lkpi_pcie_port_install(void);

#endif /* FREEWIFICAM_LKPI_PCIE_PORT_H */
