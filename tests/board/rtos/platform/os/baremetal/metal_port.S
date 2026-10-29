/* SPDX-License-Identifier: GPL-2.0 */
/*
 * metal_port.S - context switch, first-run trampoline and exception vectors
 * for the baremetal CMSIS2 backend.
 *
 * The switch saves only the callee-saved integer state (x19..x30). The port
 * is built with -mgeneral-regs-only, so there is no FP/SIMD state to spill,
 * and a thread never leaves the CPU except inside metal_switch(): the saved
 * frame is exactly what a normal function call already had to preserve.
 *
 * Tasks run at EL1h (SP_EL1, the task's own stack). The IRQ slot used is the
 * "current EL with SP_ELx" one; a stray exception from any other slot parks in
 * a loop instead of silently returning into a corrupted context.
 */

	.text

/* void metal_switch(uint64_t **save_sp, uint64_t *new_sp) */
	.global metal_switch
	.type	metal_switch, %function
	.align	2
metal_switch:
	sub	sp, sp, #96
	stp	x19, x20, [sp, #0]
	stp	x21, x22, [sp, #16]
	stp	x23, x24, [sp, #32]
	stp	x25, x26, [sp, #48]
	stp	x27, x28, [sp, #64]
	stp	x29, x30, [sp, #80]
	mov	x9, sp
	str	x9, [x0]
	mov	sp, x1
	ldp	x19, x20, [sp, #0]
	ldp	x21, x22, [sp, #16]
	ldp	x23, x24, [sp, #32]
	ldp	x25, x26, [sp, #48]
	ldp	x27, x28, [sp, #64]
	ldp	x29, x30, [sp, #80]
	add	sp, sp, #96
	ret
	.size	metal_switch, . - metal_switch

/* First-run trampoline: x19 = entry, x20 = argument. */
	.global metal_task_entry
	.type	metal_task_entry, %function
	.align	2
metal_task_entry:
	mov	x0, x20
	blr	x19
	bl	metal_thread_terminate_self
1:	b	1b
	.size	metal_task_entry, . - metal_task_entry

/* ------------------------------------------------------------------ */
/* exception vectors                                                   */
/* ------------------------------------------------------------------ */

	.section .metal_vectors, "ax", %progbits
	.align	11
	.global metal_vector_table
metal_vector_table:
	/* current EL with SP_EL0: never used by this port */
	b	metal_fault
	.balign	128
	b	metal_fault
	.balign	128
	b	metal_fault
	.balign	128
	b	metal_fault
	.balign	128

	/* current EL with SP_ELx: IRQ is the only live slot */
	b	metal_fault
	.balign	128
	b	metal_irq_entry
	.balign	128
	b	metal_fault
	.balign	128
	b	metal_fault
	.balign	128

	/* lower EL: no user mode in this port */
	b	metal_fault
	.balign	128
	b	metal_fault
	.balign	128
	b	metal_fault
	.balign	128
	b	metal_fault
	.balign	128

metal_fault:
	mov	x3, x30
	mrs	x0, esr_el1
	mrs	x1, elr_el1
	mrs	x2, far_el1
	bl	metal_fault_report
1:	b	1b

/* IRQ from EL1h: save the caller-saved integer state, dispatch, restore. */
metal_irq_entry:
	msr	daifset, #2
	sub	sp, sp, #160
	stp	x0, x1, [sp, #0]
	stp	x2, x3, [sp, #16]
	stp	x4, x5, [sp, #32]
	stp	x6, x7, [sp, #48]
	stp	x8, x9, [sp, #64]
	stp	x10, x11, [sp, #80]
	stp	x12, x13, [sp, #96]
	stp	x14, x15, [sp, #112]
	stp	x16, x17, [sp, #128]
	str	x18, [sp, #144]
	bl	metal_irq_handler
	/* Defensive: nothing in the C handler may leave IRQs open in the
	 * return path; the eret restores the thread's DAIF from SPSR. */
	msr	daifset, #2
	ldp	x0, x1, [sp, #0]
	ldp	x2, x3, [sp, #16]
	ldp	x4, x5, [sp, #32]
	ldp	x6, x7, [sp, #48]
	ldp	x8, x9, [sp, #64]
	ldp	x10, x11, [sp, #80]
	ldp	x12, x13, [sp, #96]
	ldp	x14, x15, [sp, #112]
	ldp	x16, x17, [sp, #128]
	ldr	x18, [sp, #144]
	add	sp, sp, #160
	eret

	.section .note.GNU-stack, "", %progbits