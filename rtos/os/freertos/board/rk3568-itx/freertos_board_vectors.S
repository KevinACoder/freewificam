/**
 * @file    freertos_board_vectors.S
 * @brief   The FreeRTOS runtime vector table (_freertos_vector_table).
 * @author  zhugengyu
 * @date    07.10.2026
 * @design  vector table + single dispatch entry - the 16-slot AArch64
 *          layout with handler entries in both current-EL rows follows the
 *          upstream FreeRTOS AArch64 demo (FreeRTOS-Kernel upstream,
 *          Demo/CORTEX_A53_64-bit_UltraScale_MPSoC/RTOSDemo_A53/src/
 *          FreeRTOS_asm_vectors.S:136-152); its bare-spin slots are
 *          replaced by the glue's park line.
 *
 * The port installs this table into VBAR_EL1 when it restores the first task
 * context (portASM.S, vPortRestoreTaskContext), replacing the boot table
 * from start.S. Tasks run at EL1 with SPSel=0, so the "current EL with
 * SP_EL0" row is the live one; the SP_ELx row carries the same entries so an
 * exception taken while the exception stack is already in use cannot fall
 * through to an unhandled slot.
 *
 * Handler slots: synchronous -> the port's SVC handler (portYIELD is SVC 0
 * in the GUEST build), IRQ -> the port's IRQ entry (which reads ICC_IAR1_EL1,
 * calls vApplicationIRQHandler and writes ICC_EOIR1_EL1). Every other slot
 * reports through freertos_board_fault_park().
 */

	.arch	armv8-a
	.section .freertos_vectors, "ax"
	.align	11
	.globl	_freertos_vector_table
	.type	_freertos_vector_table, %object
_freertos_vector_table:
	/* Current EL with SP_EL0 (the running-task row) */
	.p2align 7
	b	FreeRTOS_SWI_Handler
	.p2align 7
	b	FreeRTOS_IRQ_Handler
	.p2align 7
	b	rtos_fiq_fault
	.p2align 7
	b	rtos_serr_fault
	/* Current EL with SP_ELx */
	.p2align 7
	b	FreeRTOS_SWI_Handler
	.p2align 7
	b	FreeRTOS_IRQ_Handler
	.p2align 7
	b	rtos_fiq_fault
	.p2align 7
	b	rtos_serr_fault
	/* Lower EL, AArch64: nothing below us exists in this image */
	.p2align 7
	b	rtos_sync_fault
	.p2align 7
	b	rtos_irq_fault
	.p2align 7
	b	rtos_fiq_fault
	.p2align 7
	b	rtos_serr_fault
	/* Lower EL, AArch32 */
	.p2align 7
	b	rtos_sync_fault
	.p2align 7
	b	rtos_irq_fault
	.p2align 7
	b	rtos_fiq_fault
	.p2align 7
	b	rtos_serr_fault

/* Non-handler slots: report and park through the glue. A fault that cannot
 * speak is indistinguishable from a dead board. */
rtos_sync_fault:
	ldr	x0, =msg_sync
	b	rtos_fault_common
rtos_irq_fault:
	ldr	x0, =msg_irq
	b	rtos_fault_common
rtos_fiq_fault:
	ldr	x0, =msg_fiq
	b	rtos_fault_common
rtos_serr_fault:
	ldr	x0, =msg_serr
rtos_fault_common:
	bl	freertos_board_fault_park
	b	.		/* not reached: the park never returns */

	.section .rodata, "a"
msg_sync:	.asciz	"fatal: synchronous exception\n"
msg_irq:	.asciz	"fatal: unexpected IRQ\n"
msg_fiq:	.asciz	"fatal: FIQ\n"
msg_serr:	.asciz	"fatal: SError\n"
