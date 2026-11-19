/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/pci_ids.h
 * @brief   The PCI vendor/device ids a compiled driver's tables name.
 *
 * @design declaration-only compatibility face. The interface
 *         (include/linux/pci_ids.h) is the kernel's whole vendor/device number
 *         database; a port carries the entries its compiled set names, with the
 *         interface's values and the interface's spelling (PCI_VENDOR_ID_<vend> /
 *         PCI_DEVICE_ID_<vend>_<part>). brcmfmac names the Broadcom vendor id
 *         through its own chip-id header (include/brcm_hw_ids.h:16 hands
 *         PCI_VENDOR_ID_BROADCOM to BRCM_PCIE_VENDOR_ID_BROADCOM), which is why
 *         sdio.c and bcmsdh.c include this header.
 *
 *         linux/pci.h defines the Intel vendor id it needs at its own site
 *         (PCI_VENDOR_ID_INTEL); this file does not repeat it - one definition
 *         per name.
 * @date    19.11.2026
 */

#ifndef _LINUXKPI_LINUX_PCI_IDS_H_
#define _LINUXKPI_LINUX_PCI_IDS_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/pci_ids.h: the Broadcom vendor id. */
#define	PCI_VENDOR_ID_BROADCOM		0x14e4

#endif /* _LINUXKPI_LINUX_PCI_IDS_H_ */
