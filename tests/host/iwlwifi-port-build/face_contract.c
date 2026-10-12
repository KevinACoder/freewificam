/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    face_contract.c
 * @brief   The first-party header face the iwlwifi port compiles against, as one
 *          translation unit on its own.
 *
 * The driver's bytes are compiled against this face by the sibling `compile`
 * segment; this unit exists so the face has to stand up without them. It includes
 * every header the driver's closure reaches and pins the shapes a driver would
 * notice if they moved.
 *
 * @design build-gate contract unit - ref tests/host/rtw89-port-build/face_contract.c
 *         (the same segment of the sibling port gate).
 * @date    12.10.2026
 */

#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/device-id/pci.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>

/* The record a driver's probe receives and the table it is matched against. */
_Static_assert(sizeof(struct pci_device_id) >= 4 * sizeof(u32),
    "pci_device_id carries the vendor/device/subsystem/class words");

/* The BAR count the resource array is indexed by. */
_Static_assert(PCI_STD_NUM_BARS == 6, "a standard function has six BARs");

/* The DMA directions keep the interface's numbering: a driver compares them and
 * the adapter switches on them. */
_Static_assert(DMA_BIDIRECTIONAL == 0, "dma direction numbering");
_Static_assert(DMA_TO_DEVICE == 1, "dma direction numbering");
_Static_assert(DMA_FROM_DEVICE == 2, "dma direction numbering");
_Static_assert(DMA_NONE == 3, "dma direction numbering");

/* The handler's return values a driver compares against. */
_Static_assert(IRQ_NONE == 0 && IRQ_HANDLED == 1, "irqreturn values");

void face_contract_anchor(void);
