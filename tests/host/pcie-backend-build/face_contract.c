/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    face_contract.c
 * @brief   One translation unit that includes the PCIe backend line's headers
 *          and the board port headers they bridge to, on their own, and asserts
 *          the constants the layers above them are written against.
 *
 * @design contract translation unit, the same shape as the other port-build
 *         suites' contract unit (tests/host/fsl-sdmmc-port-build/face_contract.c):
 *         the faces are the product of this round, so they must stand without
 *         the units that consume them, and the numbers a caller depends on are
 *         asserted rather than assumed.
 *
 * The assertions fall into the groups that a silent change would hurt:
 *
 *  - the CMSIS-Driver vocabulary the drivers share. A status is 0 or negative
 *    and the error codes are a fixed small set; the NVMe event bits and the
 *    API version are the interface's, not a driver's.
 *  - the board coordinates: the ITS base and the raw API-call priority level
 *    the LPI property bytes are stamped with. Both are board facts a wrong
 *    value turns into silence rather than an error.
 *  - the MSI-X capability and table-entry layout, which dwc_msix.c programs
 *    byte for byte.
 *  - the flat PCI-access record's layout, which dwc_nvme.c and dwc_msix.c index
 *    by field.
 *
 * The declarations themselves are pinned by signature: taking each one's address
 * through a pointer of the expected type fails here if a prototype drifts, which
 * is the failure mode a header-only face otherwise hides until a caller links
 * against it.
 */

#include <stddef.h>
#include <stdint.h>

/* --- the line's own headers ------------------------------------------------- */
#include "Driver_Common.h"
#include "dwc_pcie.h"
#include "irq_ctrl.h"
#include "nvme.h"
#include "regs.h"

/* --- the driver headers this line's faces live in --------------------------- */
#include "dwc_msix.h"
#include "dwc_nvme.h"
#include "gicv3_its.h"
#include "msi.h"
#include "pcie_dw.h"

/* --- the board port headers the line bridges to ----------------------------- */
#include "board_conf.h"
#include "cache.h"
#include "gicv3.h"
#include "log.h"

/* --- the CMSIS-Driver vocabulary -------------------------------------------- */

_Static_assert(ARM_DRIVER_OK == 0, "success is zero");
_Static_assert(ARM_DRIVER_ERROR == -1, "the generic error code");
_Static_assert(ARM_DRIVER_ERROR_TIMEOUT == -3, "the timeout error code");
_Static_assert(ARM_DRIVER_ERROR_UNSUPPORTED == -4, "the unsupported error code");
_Static_assert(ARM_DRIVER_ERROR_PARAMETER == -5, "the parameter error code");
_Static_assert(ARM_POWER_FULL == 2, "the full-power state value");

_Static_assert(NVME_API_VERSION == ARM_DRIVER_VERSION_MAJOR_MINOR(0, 1),
	       "the NVMe interface version");
_Static_assert(NVME_EVENT_READ_DONE == (1UL << 0), "the read-done event bit");
_Static_assert(NVME_EVENT_WRITE_DONE == (1UL << 1), "the write-done event bit");
_Static_assert(NVME_EVENT_ERROR == (1UL << 2), "the error event bit");

/* --- board coordinates ------------------------------------------------------- */

/* The ITS register file and the doorbell an endpoint writes: the driver's
 * GITS_TRANSLATER offset plus this base is the message address. */
_Static_assert(BOARD_ITS_BASE == 0xfd440000UL, "the GICv3 ITS register base");
_Static_assert(BOARD_GICR_BASE == 0xfd460000UL, "the core 0 redistributor base");

/* The LPI priority byte the ITS driver stamps: the raw form of the API-call
 * level (11 << 4), the level an interrupt handler that calls a kernel FromISR
 * API must sit at. */
_Static_assert(BOARD_IRQ_PRIORITY_API_CALL_RAW == 0xb0u,
	       "the raw API-call priority level");

/* The LPI window: LPIs are numbered from 8192 and the driver hands out 48. */
_Static_assert(IRQ_LPI_INTID_FIRST == 8192U, "the first LPI INTID");
_Static_assert(ITS_LPI_QUANTITY == 48U, "the LPI slots the driver hands out");

/* --- MSI-X capability and table layout --------------------------------------- */

_Static_assert(PCI_MSIX_FLAGS_ENABLE == 0x8000u, "the MSI-X enable bit");
_Static_assert(PCI_MSIX_FLAGS_MASKALL == 0x4000u, "the MSI-X function mask");
_Static_assert(PCI_MSIX_TABLE_OFFSET == 0xfffffff8u, "the table-offset field");
_Static_assert(PCI_MSIX_ENTRY_SIZE == 16, "one MSI-X table entry is 16 bytes");
_Static_assert(PCI_MSIX_ENTRY_CTRL_MASKBIT == 0x1u, "the entry vector mask");

_Static_assert(PCI_CLASS_STORAGE == 0x01, "the storage base class");
_Static_assert(PCI_SUBCLASS_NVME == 0x08, "the NVM Express subclass");
_Static_assert(PCI_CMD_MEMORY == (1u << 1), "the memory-space command bit");
_Static_assert(PCI_CMD_MASTER == (1u << 2), "the bus-master command bit");

/* --- the flat PCI-access record --------------------------------------------- */

_Static_assert(sizeof(struct dwc_pcie_dev) == 40U,
	       "the enumerated-function record is five 32-bit fields and six BARs");
_Static_assert(offsetof(struct dwc_pcie_dev, bar) == 16U,
	       "the BAR array follows the identity fields with no hidden padding");
_Static_assert(sizeof(((struct dwc_pcie_dev *)0)->bar) == 24U,
	       "six 32-bit BARs");

/* --- the declared faces, pinned by signature --------------------------------- */

void (*const face_board_log)(const char *, ...) = board_log;
void (*const face_cache_flush)(uintptr_t, unsigned long) = cache_dcache_flush;
void (*const face_cache_invalidate)(uintptr_t, unsigned long) =
	cache_dcache_invalidate;

int32_t (*const face_irq_set_handler)(IRQn_ID_t, IRQHandler_t) = IRQ_SetHandler;
int32_t (*const face_irq_enable)(IRQn_ID_t) = IRQ_Enable;
int32_t (*const face_irq_disable)(IRQn_ID_t) = IRQ_Disable;

void (*const face_pcie_init)(void) = dwc_pcie_init;
uint32_t (*const face_pcie_ctrl_count)(void) = dwc_pcie_ctrl_count;
uint32_t (*const face_pcie_dev_count)(void) = dwc_pcie_dev_count;
const struct dwc_pcie_dev *(*const face_pcie_dev)(uint32_t) = dwc_pcie_dev;
int32_t (*const face_pcie_cfg_read)(uint32_t, uint32_t, uint32_t, uint32_t,
				    uint32_t *) = dwc_pcie_cfg_read;
int32_t (*const face_pcie_cfg_write)(uint32_t, uint32_t, uint32_t, uint32_t,
				     uint32_t) = dwc_pcie_cfg_write;
uint32_t (*const face_pcie_requester_id)(const struct dwc_pcie_dev *) =
	dwc_pcie_requester_id;

int (*const face_its_init)(void) = its_init;
uint64_t (*const face_its_trans_addr)(void) = gic_its_trans_addr;
int (*const face_its_event_map)(uint32_t, uint32_t) = gic_its_event_map;
void (*const face_lpi_set_state)(uint32_t, int) = gic_lpi_set_state;

const MSI_DOMAIN *(*const face_msi_domain_get)(void) = msi_domain_get;

int32_t (*const face_msix_alloc)(const struct dwc_pcie_dev *, uint32_t,
				 MSI_VECTOR *) = dwc_msix_alloc;
int32_t (*const face_msix_release)(const struct dwc_pcie_dev *) =
	dwc_msix_release;

void (*const face_dwc_nvme_init)(void) = dwc_nvme_init;
uint32_t (*const face_dwc_nvme_ctrl_count)(void) = dwc_nvme_ctrl_count;

/* One symbol, so the unit produces an object and the faces' linkage is exercised. */
int pcie_backend_face_contract_present;
