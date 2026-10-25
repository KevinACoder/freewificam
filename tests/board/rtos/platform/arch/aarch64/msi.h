/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   msi.h
 * @brief  Message-signalled interrupt (MSI/MSI-X) domain - a board service.
 *
 * An endpoint's MSI is a posted memory write: the function writes a small
 * payload to a doorbell address, and whichever interrupt controller is behind
 * that address turns it into an interrupt. On this SoC the doorbell is the
 * GICv3 ITS's GITS_TRANSLATER, the payload is an event id, and the ITS
 * translates (Requester ID, event id) into an LPI INTID through tables the
 * board layer owns.
 *
 * That translation is neither a PCIe concern nor an NVMe concern, so it lives
 * behind this tiny interface: the PCIe driver knows *which* function wants
 * vectors (the Requester ID) and programs the endpoint's MSI-X table with what
 * it is handed here; the board layer (port/aarch64/gicv3_msi.c) owns the ITS
 * side. Keeping it an interface is also what lets the ITS implementation be
 * replaced (another GIC, another translation scheme) without touching the
 * drivers - the same reason include/pcie.h exists.
 *
 * CONTRACT:
 *  - Allocate() is task-context only: it programs ITS tables, which is a
 *    command sequence, not something an interrupt handler may do.
 *  - `requester_id` is the PCIe Requester ID, BDF as bus<<8 | slot<<3 | func.
 *    It is the ITS DeviceID, so it identifies the function, not the driver.
 *  - `data` in each returned vector is the ITS event id, which is also the
 *    MSI-X table entry index the PCIe side must program. The pair (address,
 *    data) is what goes into the endpoint; `intid` is what the OS arms with
 *    IRQ_SetHandler + IRQ_Enable.
 *  - Free() withdraws the mappings; the caller disables the INTIDs first.
 *  - A domain that cannot serve a request returns a negative ARM_DRIVER_ERROR*
 *    (or a short count from Allocate) rather than pretending: a function whose
 *    vectors never arrive is otherwise indistinguishable from one that never
 *    raises an interrupt.
 */

#ifndef FREEWEBCAMERA_MSI_H
#define FREEWEBCAMERA_MSI_H

#include <stdint.h>

#include "Driver_Common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MSI_API_VERSION ARM_DRIVER_VERSION_MAJOR_MINOR(0, 1)

/* One MSI vector, from allocation to endpoint programming. */
typedef struct _MSI_VECTOR {
	uint32_t intid;		/* GIC INTID to arm (LPIs: 8192 + slot) */
	uint64_t address;	/* message address written into the endpoint */
	uint32_t data;		/* message payload: the ITS event id */
} MSI_VECTOR;

typedef struct _MSI_DOMAIN {
	ARM_DRIVER_VERSION (*GetVersion)(void);
	/* Map up to `count` vectors for one function and fill `vectors` with
	 * them. Returns the number of vectors armed (>= 1) or a negative
	 * ARM_DRIVER_ERROR*. */
	int32_t (*Allocate)(uint32_t requester_id, uint32_t count,
			    MSI_VECTOR *vectors);
	/* Withdraw every mapping of that function. */
	void (*Free)(uint32_t requester_id);
} MSI_DOMAIN;

/* Install a domain implementation. The board's ITS-backed domain installs
 * itself on the first msi_domain_get(), so a caller that only ever uses the
 * board's own path never needs this. */
int32_t msi_domain_register(const MSI_DOMAIN *domain);

/* The active domain, or NULL when the platform has none (no MSI support at
 * all). Never NULL on this board once the ITS is up. */
const MSI_DOMAIN *msi_domain_get(void);

#ifdef __cplusplus
}
#endif

#endif /* FREEWEBCAMERA_MSI_H */