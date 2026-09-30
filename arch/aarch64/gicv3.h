/*
 * @file   gicv3.h
 * @brief  GIC-600 (GICv3) bring-up + simplified IRQ registry (arch interface).
 */

#ifndef FREEWEBCAM_GICV3_H
#define FREEWEBCAM_GICV3_H

#include <stdint.h>

typedef void (*irq_handler_t)(void *arg);

/* Bring the controller up (idempotent). Conditional RMW everywhere: firmware
 * hands over with the distributor partially enabled, and a careless store
 * clears ARE and wedges the CPU interface (every IAR1 read then returns the
 * spurious 1023 - an interrupt storm). */
void gicv3_init(void);

/* Registry: one handler per INTID (this milestone uses PPI 27/30 and keeps
 * UART INTID 150 reserved). Registering does not enable. */
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

/* Interrupt entry (called from the vector table's IRQ slot):
 * acknowledge (ICC_IAR1_EL1) -> dispatch through the registry ->
 * end-of-interrupt. Spurious (1023) returns without an EOI. Runs with IRQs
 * masked (PSTATE.I set by the exception entry); no nesting. */
void gic_irq_dispatch(void);

#endif /* FREEWEBCAM_GICV3_H */
