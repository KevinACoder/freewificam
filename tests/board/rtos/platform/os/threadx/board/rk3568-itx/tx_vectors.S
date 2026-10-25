/*
 * @file    tx_vectors.S
 * @brief   ThreadX runtime vector table + the SPSel hand-over stubs.
 * @author  zhugengyu
 * @date    07.10.2026
 * @design  template method - the port fixes the frame contract (x29/x30
 *          pushed first, then _tx_thread_context_save, the C dispatcher,
 *          _tx_thread_context_restore) and this file supplies the concrete
 *          entry sequence: acknowledge (gic_iar_read), dispatch
 *          (gic_irq_dispatch), end (gic_irq_eoi).
 *
 * The ARMv8-A UP port runs EVERYTHING at EL1 with SPSel=0: threads, the
 * kernel and ISRs share the one architecturally visible stack pointer
 * (SP_EL0); the "system stack" is a software variable
 * (_tx_thread_system_stack_ptr) that _tx_thread_context_save switches to
 * before running C dispatch code. Two consequences shape this file:
 *
 *  - start.S hands control over with SPSel=1 (EL1h). The entry stub below
 *    switches to SPSel=0 while keeping sp pointed at the same physical boot
 *    stack before entering the kernel. From there nothing ever returns past
 *    the switch, and no context in this image ever runs with SPSel=1 again.
 *
 *  - AArch64 exception entry always flips SPSel to 1. Every real entry of
 *    this table therefore starts by switching back (`msr spsel, 0`) so the
 *    frame lands on the interrupted context's own stack.
 *
 * Vector routing: everything in this image runs with SPSel=0, so live
 * exceptions always arrive through the "current EL with SP0" group. The SPx
 * and lower-EL groups are parked - they would mean the SPSel invariant is
 * broken. FIQ belongs to the secure world (OP-TEE holds Group 0) and is
 * expected never to arrive; it parks SILENTLY - printing could deadlock on
 * the console lock the FIQ just preempted, so a silent hang IS the
 * diagnostic. Synchronous exceptions are faults: the kernel exits to C only
 * via branches the port controls.
 *
 * The table shares the .vectors section with start.S's boot_vectors (VBAR
 * picks the live one by symbol; zero linker-script change). Until
 * tx_application_define installs this table via vbar_install(),
 * boot_vectors stays the live table.
 *
 * EOI ownership: the entry below ends the interrupt itself, with the value
 * gic_iar_read returned. The port does not touch EOIR1, and neither does
 * gic_irq_dispatch - ending an interrupt twice corrupts the active-priority
 * state.
 */

	.arch	armv8-a
	.section .vectors, "ax"
	.align	11

	.globl	_threadx_vector_table
	.type	_threadx_vector_table, %object
_threadx_vector_table:

	/* --- current EL with SP0: the only live group --- */
	b	tx_sync_fault		/* 0x000 sync  */
	.balign 0x80
	b	tx_irq_first_level	/* 0x080 irq   */
	.balign 0x80
	b	tx_fiq_park		/* 0x100 fiq   */
	.balign 0x80
	b	tx_serror_fault		/* 0x180 serror*/

	/* --- current EL with SPx: unreachable while the SPSel invariant
	 *     holds; a hit here is itself the diagnosis --- */
	.balign 0x80
	b	tx_sync_fault		/* 0x200 sync  */
	.balign 0x80
	b	tx_irq_first_level	/* 0x280 irq   */
	.balign 0x80
	b	tx_fiq_park		/* 0x300 fiq   */
	.balign 0x80
	b	tx_serror_fault		/* 0x380 serror*/

	/* --- lower EL, AArch64: no lower-EL context exists in this image --- */
	.balign 0x80
	b	tx_sync_fault		/* 0x400 sync  */
	.balign 0x80
	b	tx_sync_fault		/* 0x480 irq   */
	.balign 0x80
	b	tx_fiq_park		/* 0x500 fiq   */
	.balign 0x80
	b	tx_serror_fault		/* 0x580 serror*/

	/* --- lower EL, AArch32 --- */
	.balign 0x80
	b	tx_sync_fault		/* 0x600 sync  */
	.balign 0x80
	b	tx_sync_fault		/* 0x680 irq   */
	.balign 0x80
	b	tx_fiq_park		/* 0x700 fiq   */
	.balign 0x80
	b	tx_serror_fault		/* 0x780 serror*/

	.size	_threadx_vector_table, . - _threadx_vector_table

	.text
	.align	4

/* IRQ first-level entry: the port's contract. Interrupts are masked by
 * exception entry (no nesting). The x29/x30 push is part of the frame
 * contract - _tx_thread_context_save stores x0..x19 itself and its idle
 * path pops this push along with its own stores.
 *
 * The acknowledge/end pair belongs to this entry (EOI ownership above); the
 * C dispatcher is the BASE layer's gic_irq_dispatch - the kernel round
 * swaps the tick handler in the existing table via irq_register, it does
 * not rebuild dispatch. The three arch calls keep every raw system-register
 * access inside arch/aarch64/gicv3.c. */
	.globl	tx_irq_first_level
	.type	tx_irq_first_level, %function
tx_irq_first_level:
	msr	spsel, 0
	stp	x29, x30, [sp, #-16]!
	bl	_tx_thread_context_save
	bl	gic_iar_read
	bl	gic_irq_dispatch
	bl	gic_irq_eoi
	b	_tx_thread_context_restore
	.size	tx_irq_first_level, . - tx_irq_first_level

/* Fault landing: without the stub a sync is a fatal park (kind 1 = sync,
 * 3 = serror, 2 = fiq; the ESR carries the rest). Lower-EL sync entries
 * share kind 1 - no lower-EL context exists to produce one, and a hit there
 * is itself the diagnosis. x0-x17 are free to clobber here - the faulting
 * context is not coming back. */
	.globl	tx_fault_park		/* C lander lives in tx_glue.c */
tx_sync_fault:
	mov	x0, #1
	b	tx_fault_park

tx_serror_fault:
	mov	x0, #3
	b	tx_fault_park

tx_fiq_park:
	/* A FIQ arrived with no owner in this image (secure-world misroute).
	 * Historically silent because a console-lock print here could spin
	 * forever on the interrupted holder's lock; the lock-free emergency
	 * outlet means the death now leaves one line instead of silence. */
	mov	x0, #2
	b	tx_fault_park

/* Entry stub: called by main() instead of returning. Keeps the current sp
 * (the boot region top, __stack_top) and hands it to the kernel as SP_EL0.
 * tx_kernel_enter is a macro in tx_api.h; from assembly the internal symbol
 * must be used directly. Never returns. */
	.globl	tx_kernel_enter_spsel0
	.type	tx_kernel_enter_spsel0, %function
tx_kernel_enter_spsel0:
	mov	x0, sp
	msr	spsel, 0
	mov	sp, x0
	b	_tx_initialize_kernel_enter
	.size	tx_kernel_enter_spsel0, . - tx_kernel_enter_spsel0
