/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   gicv3_its.h
 * @brief  GIC-600 ITS / LPI driver: interface and table geometry.
 *
 * The register sequences, table geometry and cache discipline below are the
 * ones this board's GIC-600 requires; each field encoding and ordering step is
 * called out where it lives. Re-deriving any of them is how the ITS self-test
 * went silent before.
 *
 * Callers address LPIs by raw INTID (IRQ_LPI_INTID_FIRST + slot) rather than
 * through a kernel IRQ window, matching the CMSIS irq_ctrl.h model this
 * project uses for SPIs and PPIs. The LPI priority byte is a FreeRTOS-safe
 * level, not the default driver level.
 *
 * This is not an interface layer: nothing above the board layer should
 * include it. Handlers are registered through CMSIS irq_ctrl.h's
 * IRQ_SetHandler on the LPI's INTID, exactly as for SPIs and PPIs.
 */

#ifndef FREEWEBCAMERA_GICV3_ITS_H
#define FREEWEBCAMERA_GICV3_ITS_H

#include <stdint.h>

#include "gicv3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- geometry (what this board's GIC-600 demands) ------------------------- */

/* First LPI INTID (IRQ_LPI_INTID_FIRST, 8192) comes from gicv3.h: it is a
 * GICv3 architectural number, shared with the registry that dispatches the
 * LPI. Handlers are registered as (IRQ_LPI_INTID_FIRST + slot). */

/* LPI slots the driver hands out. The number is what the self-test grid and
 * the PCIe MSI allocator were sized against. */
#define ITS_LPI_QUANTITY	48U

/* Command queue: 512 entries of four 64-bit words (32-byte slots, 16 KiB). */
#define ITS_CMDQ_ENTRIES	512U

/* Device table: GITS_TYPER.DEVBITS=15 on this part, so the direct table
 * must span the full 16-bit DeviceID space (65536 x 8B = 512 KiB). A
 * shorter table makes the ITS reject every MAPD and the device comes back
 * invalid - silently. */
#define ITS_DEV_TABLE_ENTRIES	65536U

/* Collection table: one 4 KiB page of 8-byte entries. This ITS binds the
 * collection type to BASER1 on reset; a collection table offered in any
 * other slot is silently discarded (which wedges the command queue). */
#define ITS_COLL_TABLE_ENTRIES	512U

/* Per-device interrupt translation table: 64 entries x 8B = 512B,
 * 256-byte aligned. 64 is what MAPD's size field encodes (log2 - 1 = 5). */
#define ITS_ITT_ENTRIES		64U

/* Simultaneously mapped devices: PCIe endpoints plus the its_test grid. */
#define ITS_MAX_DEVICES		8U

/* LPI property/pending tables cover 2^16 LPI INTIDs: GICR_PROPBASER.IDbits
 * is programmed 15 and the tables must span what the redistributor will
 * index, whatever fraction of the slots is ever handed out (64 KiB each,
 * the pending table 64K-aligned for PENDBASER's [51:16] address field). */
#define ITS_PROP_SIZE		65536U
#define ITS_PEND_SIZE		65536U
#define ITS_LPI_IDBITS_FIELD	15U

/* --- init and mapping API -------------------------------------------------- */

/* Bring up the ITS: redistributor LPI tables, device/collection tables,
 * command queue, then the collection for this PE. Idempotent.
 * Returns 0 on success; on failure LPI delivery stays disabled. */
int its_init(void);

/* The address an endpoint writes to raise an MSI (GITS_TRANSLATER). */
uint64_t gic_its_trans_addr(void);

/* Map a device into the ITS (MAPD + SYNC): allocates and binds its ITT.
 * Idempotent per devid. Returns 0 on success. */
int gic_its_device_attach(uint32_t devid);

/* Bind the next free LPI slot to (devid, eventid): MAPTI + SYNC. Returns
 * the LPI's INTID (IRQ_LPI_INTID_FIRST + slot), or -1. The caller arms the
 * LPI through IRQ_SetHandler + IRQ_Enable on that INTID, exactly as for a
 * SPI - the enable reaches the property table through the gicv3.c hook. */
int gic_its_event_map(uint32_t devid, uint32_t eventid);

/* Withdraw a binding (DISCARD). */
void gic_its_event_unmap(uint32_t devid, uint32_t eventid);

/* Inject a message for a mapped (devid, eventid) pair the way an endpoint
 * would - via the ITS's INT command. A CPU store to the doorbell address
 * carries no Requester ID and is dropped, so there is deliberately no
 * "ring doorbell" API: INT is the only injection path that works. */
int gic_its_send_int(uint32_t devid, uint32_t eventid);

/* Drop pending state the ITS may still hold for (devid, eventid). */
int gic_its_send_clear(uint32_t devid, uint32_t eventid);

/* Enable/disable an LPI's delivery: the property table byte + flush + INV.
 * Called from gicv3.c's IRQ_Enable/IRQ_Disable for the LPI window; not
 * normally called directly. */
void gic_lpi_set_state(uint32_t intid, int enable);

/* --- self-test and post-mortem plumbing ------------------------------------ */

/* The four-rung ladder (see its_test.c): single INT, disable/re-enable,
 * silent-while-disabled grid, interleaved re-fire. Returns 0 when every
 * rung passed and stores the number of deliveries observed. */
int its_selftest(uint32_t *delivered);

/* itsdump support: addresses of the driver-owned tables (VA == PA under
 * this project's identity map) and per-device ITT lookup. */
uint64_t its_prop_table(void);
uint64_t its_pend_table(void);
uint64_t its_dev_table(void);
uint64_t its_device_itt(uint32_t devid);

/* Devices currently attached, into out[] (up to max). Returns the count. */
uint32_t its_attached_devices(uint32_t *out, uint32_t max);

/* Diagnostic output (vsnprintf + board_log; '\n' becomes CRLF in
 * the console driver). Shared by its_test.c and itsdump.c. */
void its_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* FREEWEBCAMERA_GICV3_ITS_H */
