/*
 * @file   tx_vectors.S
 * @brief  ThreadX runtime vector table + the SPSel hand-over stubs (SPEC S3
 *         threadx-up-boot §3.3/§3.2.3).
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
 *    frame lands on the interrupted context's own stack - the port's
 *    calling contract (x29/x30 pushed first, then _tx_thread_context_save,
 *    the C dispatcher, _tx_thread_context_restore).
 *
 * Vector routing: everything in this image runs with SPSel=0, so live
 * exceptions always arrive through the "current EL with SP0" group. The
 * SPx and lower-EL groups are parked - they would mean the SPSel invariant
 * is broken. FIQ belongs to the secure world (OP-TEE holds Group 0) and is
 * expected never to arrive; it parks SILENTLY - printing could deadlock on
 * the console lock the FIQ just preempted, so a silent hang IS the
 * diagnostic. Synchronous exceptions are faults: the kernel exits to C only
 * via branches the port controls, there is no legitimate sync-exception
 * user (T4 will retarget the sync slots to the gdb stub instead).
 *
 * The table shares the existing .vectors section with start.S's
 * boot_vectors (spec §3.4.1: two tables in one section, VBAR picks the
 * live one by symbol; zero linker-script change). Until T2 installs this
 * table via vbar_install(), boot_vectors stays the live table.
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
 * The C dispatcher is the BASE layer's gic_irq_dispatch (IAR read, handler
 * table, EOI at the tail - spec §3.3.3): the kernel round swaps the tick
 * handler in the existing table via irq_register, it does not rebuild
 * dispatch. */
	.globl	tx_irq_first_level
	.type	tx_irq_first_level, %function
tx_irq_first_level:
	msr	spsel, 0
	stp	x29, x30, [sp, #-16]!
	bl	_tx_thread_context_save
	bl	gic_irq_dispatch
	b	_tx_thread_context_restore
	.size	tx_irq_first_level, . - tx_irq_first_level

/* Fault landing: without the stub a sync is a fatal park (kind 1 = sync,
 * 3 = serror; the ESR carries the rest). Lower-EL sync entries share kind 1
 * - no lower-EL context exists to produce one, and a hit there is itself
 * the diagnosis. x0-x17 are free to clobber here - the faulting context is
 * not coming back. */
	.globl	tx_fault_park		/* C lander lives in tx_glue.c */
tx_sync_fault:
#ifdef CONFIG_GDB_STUB
	/* gdb build (T4): a synchronous exception in the ThreadX world is a
	 * host stop, not a park - the tick poll's BRK lands here too, and
	 * gdb_sync_entry decides stop vs polled-fatal (no dbgport yet).
	 * Frame recipe = start.S boot_sync_entry (R3 board-proven, 288-byte
	 * struct gdb_trapframe), with one port-specific prologue step: the
	 * exception entry flipped SPSel to 1, and the port's world lives on
	 * SP_EL0 - switch back FIRST so the frame is carved on the
	 * interrupted context's own stack. gdb_sync_entry returns when the
	 * host continued; elr/spsr come back from the frame (they may have
	 * been rewritten), so a fault continued unchanged re-executes under
	 * host control. */
	msr	spsel, 0
	sub	sp, sp, #288
	stp	x16, x17, [sp, #0xb0]	/* originals first */
	mov	x16, sp
	add	x16, x16, #288		/* interrupted SP (pre-reserve) */
	str	x16, [sp, #0]
	str	x30, [sp, #8]
	mrs	x17, elr_el1
	str	x17, [sp, #0x10]
	mrs	x17, spsr_el1
	str	x17, [sp, #0x18]
	mrs	x17, esr_el1
	str	x17, [sp, #0x20]
	mrs	x17, far_el1
	str	x17, [sp, #0x28]
	stp	x0, x1, [sp, #0x30]
	stp	x2, x3, [sp, #0x40]
	stp	x4, x5, [sp, #0x50]
	stp	x6, x7, [sp, #0x60]
	stp	x8, x9, [sp, #0x70]
	stp	x10, x11, [sp, #0x80]
	stp	x12, x13, [sp, #0x90]
	stp	x14, x15, [sp, #0xa0]
	/* x16/x17 slots already hold their originals */
	stp	x18, x19, [sp, #0xc0]
	stp	x20, x21, [sp, #0xd0]
	stp	x22, x23, [sp, #0xe0]
	stp	x24, x25, [sp, #0xf0]
	stp	x26, x27, [sp, #0x100]
	stp	x28, x29, [sp, #0x110]

	mov	x0, sp
	bl	gdb_sync_entry

	ldr	x17, [sp, #0x10]
	msr	elr_el1, x17
	ldr	x17, [sp, #0x18]
	msr	spsr_el1, x17
	ldp	x0, x1, [sp, #0x30]
	ldp	x2, x3, [sp, #0x40]
	ldp	x4, x5, [sp, #0x50]
	ldp	x6, x7, [sp, #0x60]
	ldp	x8, x9, [sp, #0x70]
	ldp	x10, x11, [sp, #0x80]
	ldp	x12, x13, [sp, #0x90]
	ldp	x14, x15, [sp, #0xa0]
	ldp	x16, x17, [sp, #0xb0]
	ldp	x18, x19, [sp, #0xc0]
	ldp	x20, x21, [sp, #0xd0]
	ldp	x22, x23, [sp, #0xe0]
	ldp	x24, x25, [sp, #0xf0]
	ldp	x26, x27, [sp, #0x100]
	ldp	x28, x29, [sp, #0x110]
	ldr	x30, [sp, #8]
	ldr	x16, [sp, #0]
	mov	sp, x16
	eret
#else
	mov	x0, #1
	b	tx_fault_park
#endif

tx_serror_fault:
	mov	x0, #3
	b	tx_fault_park

tx_fiq_park:
	/* No print: a FIQ here preempted the console lock holder and the
	 * print would spin on it forever. Silent hang = secure world routed
	 * a FIQ at us. */
1:	wfe
	b	1b

/* Entry stub: called by main() (THREADX_BUILD) instead of returning.
 * Keeps the current sp (the 64 KiB boot region top, __stack_top) and hands
 * it to the kernel as SP_EL0. tx_kernel_enter is a macro in tx_api.h; from
 * assembly the internal symbol must be used directly. Never returns. */
	.globl	tx_kernel_enter_spsel0
	.type	tx_kernel_enter_spsel0, %function
tx_kernel_enter_spsel0:
	mov	x0, sp
	msr	spsel, 0
	mov	sp, x0
	b	_tx_initialize_kernel_enter
	.size	tx_kernel_enter_spsel0, . - tx_kernel_enter_spsel0
