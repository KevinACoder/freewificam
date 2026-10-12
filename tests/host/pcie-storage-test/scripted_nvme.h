/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    scripted_nvme.h
 * @brief   A scripted stand-in for the board's NVMe controller.
 *
 * The storage stack under test binds through the block-device seam, whose single
 * registration point (ports/fatfs/blkdev.c) names the board's NVMe driver. This
 * test double implements that same frozen interface (ARM_DRIVER_NVME from
 * nvme.h) plus the driver's bring-up entry points, so the real binding code runs
 * unchanged and the storage stack sees a controller - one that can be scripted to
 * misbehave on demand.
 *
 * The model is deliberately small but faithful to the properties the stack
 * depends on:
 *
 *   - a submission becomes visible only when its doorbell is written, and the
 *     device executes it in a BOUNDED number of service iterations; a command
 *     whose completion never arrives must therefore return an error rather than
 *     spin forever (the fault-injection cases assert this);
 *   - a completion entry is posted by the device and reaped by the driver's wait
 *     loop, counted separately, so "consumed exactly once" is checkable;
 *   - the interrupt is a separate signal: it may be delivered late (after the
 *     completion was already reaped) and must not reap a second time, or be
 *     suppressed, in which case the completion count and the interrupt count
 *     disagree (the reverse image of the board's stability criterion).
 */

#ifndef PCIE_STORAGE_TEST_SCRIPTED_NVME_H
#define PCIE_STORAGE_TEST_SCRIPTED_NVME_H

#include <stdint.h>

#include "nvme.h"

/* --- the device's own geometry -------------------------------------------- */

#define SCRIPTED_NVME_BLOCK_SIZE	512u
/* 32 MiB: comfortably above the exFAT minimum (4096 filesystem sectors of
 * 4096 bytes == 16 MiB) with room for the round-trip file. */
#define SCRIPTED_NVME_BLOCKS		(32u * 1024u * 1024u / SCRIPTED_NVME_BLOCK_SIZE)

/* The bound on the completion wait: a command whose completion has not arrived
 * after this many service iterations is a timeout, not an infinite loop. */
#define SCRIPTED_NVME_MAX_POLLS		256u

/* --- fault injection ------------------------------------------------------- */

#define SCRIPTED_FAULT_NONE		0u
#define SCRIPTED_FAULT_DROP_COMPLETION	(1u << 0)	/* completion never arrives */
#define SCRIPTED_FAULT_DROP_DOORBELL	(1u << 1)	/* submission never visible */
#define SCRIPTED_FAULT_LATE_IRQ		(1u << 2)	/* irq arrives after the reap */
#define SCRIPTED_FAULT_SUPPRESS_IRQ	(1u << 3)	/* completion, no interrupt */
#define SCRIPTED_FAULT_LIE		(1u << 4)	/* reads return inverted bytes */

/* Forget every recorded count, clear the faults and the store. */
void scripted_nvme_reset(void);

void scripted_nvme_set_fault(uint32_t fault);
void scripted_nvme_clear_fault(uint32_t fault);

/* --- the recorded state ---------------------------------------------------- */

uint32_t scripted_nvme_submissions(void);
uint32_t scripted_nvme_completions(void);	/* completion entries posted */
uint32_t scripted_nvme_consumed(void);		/* entries reaped by the wait loop */
uint32_t scripted_nvme_irqs(void);		/* interrupts delivered */
uint32_t scripted_nvme_spurious_irqs(void);	/* irqs with nothing to reap */
uint32_t scripted_nvme_polls(void);		/* bounded-wait iterations used */
uint32_t scripted_nvme_doorbell_dropped(void);

/* The last request the device saw, in its own 512-byte block units. */
uint64_t scripted_nvme_last_lba(void);
uint32_t scripted_nvme_last_blocks(void);

/* Deliver an interrupt that was deferred by SCRIPTED_FAULT_LATE_IRQ: it finds
 * the completion queue already empty and must not reap a second time. */
void scripted_nvme_late_irq(void);

/* The frozen driver interface and the board driver's bring-up entry points. */
extern ARM_DRIVER_NVME Driver_NVME;
void dwc_nvme_init(void);
uint32_t dwc_nvme_ctrl_count(void);
uint32_t dwc_nvme_irq_count(uint32_t ctrl);

#endif /* PCIE_STORAGE_TEST_SCRIPTED_NVME_H */
