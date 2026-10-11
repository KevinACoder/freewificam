/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/pci.h
 * @brief   The PCI record a driver's type headers name even when only the USB path is
 *          built.
 *
 * @design declaration-only compatibility face, and deliberately just the record's name:
 *         the compiled set names `struct pci_dev` in declarations and callbacks that the
 *         USB build never takes, so the type must exist for those declarations to parse.
 *         Nothing in the compiled set dereferences it (no PCI transport unit is built),
 *         so no member is carried; the day a PCI consumer is compiled, this face grows the
 *         members that consumer names, transcribed from include/linux/pci.h.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    10.10.2026
 */

#ifndef _LINUXKPI_LINUX_PCI_H_
#define _LINUXKPI_LINUX_PCI_H_

#include "../../linuxkpi_core.h"

struct pci_dev;

#endif /* _LINUXKPI_LINUX_PCI_H_ */
