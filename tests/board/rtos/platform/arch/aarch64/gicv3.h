/*
 * @file   gicv3.h
 * @brief  GIC-600 (GICv3) bring-up + simplified IRQ registry (arch interface).
 */

#ifndef FREEWIFICAM_GICV3_H
#define FREEWIFICAM_GICV3_H

#include <stdint.h>

typedef void (*irq_handler_t)(void *arg);

/* First LPI INTID. LPIs are numbered from here, not from 0 (INTID 8192 is LPI
 * slot 0). A message interrupt is an architectural GICv3 line, so the number
 * belongs with the controller rather than with the ITS driver that hands the
 * slots out. */
#define IRQ_LPI_INTID_FIRST	8192U

/* Bring the controller up (idempotent). Conditional RMW everywhere: firmware
 * hands over with the distributor partially enabled, and a careless store
 * clears ARE and wedges the CPU interface (every IAR1 read then returns the
 * spurious 1023 - an interrupt storm). */
void gicv3_init(void);

/* Registry: one handler per INTID, over the whole GICv3 INTID space - SGI/PPI,
 * SPI and LPI alike, because dispatch is by the acknowledged INTID. Registering
 * does not enable: an SPI/PPI is enabled through the distributor/redistributor,
 * an LPI through its property byte. */
int32_t irq_register(uint32_t intid, irq_handler_t handler, void *arg);

/* Enable/disable one INTID. For SGI/PPI this is a single-bit read-modify-
 * write in the redistributor's SGI/PPI bank - never a bank-wide write, that
 * bank holds firmware-owned secure lines. */
int32_t irq_enable(uint32_t intid);
int32_t irq_disable(uint32_t intid);

int32_t irq_clear_pending(uint32_t intid);

/* Raw 8-bit GIC priority (lower = more urgent). BYTE-granular register. */
int32_t irq_set_priority(uint32_t intid, uint8_t raw_priority);

/* Trigger-mode bookkeeping. The registers are NOT written: PPI lines are
 * level-sensitive at reset (the generic timer's line clears when TVAL is
 * reloaded - requesting edge semantics would be wrong), and the distributor
 * init already clears every SPI's ICFGR pair to level. */
int32_t irq_set_mode_level(uint32_t intid);

/* Read-only state queries (bring-up diagnostics): the pending bit (ISPENDR,
 * read-only) tells "the device still holds the line down" (interrupt storm
 * in progress) apart from "the handler loops without the line" (software
 * livelock). Enabled bit for completeness. Any context. */
int32_t gic_irq_is_pending(uint32_t intid);
int32_t gic_irq_is_enabled(uint32_t intid);

/* The running interrupt priority (ICC_RPR_EL1); 0xff when no interrupt is
 * active. Safe from any context. */
uint32_t gic_running_priority(void);

/* Acknowledge/end pair for an exception entry's IRQ slot. The raw system
 * registers (ICC_IAR1_EL1 / ICC_EOIR1_EL1) stay inside this module: the
 * entry reads the INTID with gic_iar_read(), dispatches with the returned
 * value, and ends the interrupt by writing the SAME value back through
 * gic_irq_eoi(). Which code owns that pair is per-port (the FreeRTOS port
 * does it inside its own asm; the ThreadX entry does it in tx_vectors.S) -
 * the dispatcher below touches neither register. */
uint32_t gic_iar_read(void);
void gic_irq_eoi(uint32_t iar);

/* Dispatch one already-acknowledged interrupt, from the exception entry's
 * IRQ slot: iar = the value gic_iar_read() returned. Runs the registered
 * handler (if any); spurious (1023) returns without one. Ending the
 * interrupt belongs to the entry (gic_irq_eoi). Returns iar unchanged so
 * the caller can hand it straight to gic_irq_eoi(). Runs with IRQs masked
 * (PSTATE.I set by the exception entry); no nesting. */
uint32_t gic_irq_dispatch(uint32_t iar);

#endif /* FREEWIFICAM_GICV3_H */
