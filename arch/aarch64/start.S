/*
 * @file   start.S
 * @brief  Boot entry: normalise to EL1, install vectors, clear BSS, enter C.
 *
 * U-Boot's `go` hands over at EL2 (non-secure) on this board; EL3 and EL1
 * paths exist as defensive convergences. Everything after el1_entry runs
 * with the board-proven sequence carried over from the prior project's
 * ThreadX carrier, minus the kernel hooks (SPEC v3 §3.1.4; the exploratory
 * 2026-09-30 round confirmed that deviating from this sequence is what
 * breaks boot - issues/20260930-r1-baremetal-rework.md).
 *
 * Two board facts drive the EL2 sequence:
 *
 *  - The loader arrives with the EL2 MMU and caches possibly still enabled.
 *    SCTLR_EL2 is CLEARED (not bit-edited) before anything else so
 *    subsequent table walks and cache operations behave predictably.
 *
 *  - CNTHCTL_EL2.EL1PCEN / EL1PCTEN must be set (bits 0 and 1) or EL1 cannot
 *    reach the counters and every access traps into the old EL2 vector
 *    table. The tick uses the virtual timer CNTV, but the bits are set so
 *    counter reads behave from EL1 either way.
 *
 * CurrentEL encodes the level as 0x4 / 0x8 / 0xC (not 1 / 2 / 3) - a classic
 * source of misrouted boot paths.
 *
 * .S files run the preprocessor, so board_conf.h (data-only #defines) is
 * includable directly.
 */

#include "board_conf.h"

	.arch	armv8-a
	.section .text.boot, "ax"

	.globl	_start
	.type	_start, %function
_start:
	/* Mask everything until EL1 is set up. */
	msr	daifset, #0xf

	/* This path is boot-core-only. A secondary running here would clear
	 * the BSS under the running image and re-derive state that must only
	 * exist once. The other three cores are held by BL31/OP-TEE; if one
	 * lands here anyway, park it where it cannot do harm. The logical
	 * core number comes from BOARD_MPIDR_CORE_SHIFT (Aff1 on RK3568,
	 * measured MPIDR = 0x8100_0N00). */
	mrs	x1, mpidr_el1
	lsr	x1, x1, #BOARD_MPIDR_CORE_SHIFT
	and	x1, x1, #0xff
	cbz	x1, 1f
2:	wfe
	b	2b
1:

	mrs	x0, CurrentEL
	and	x0, x0, #0xc
	cmp	x0, #0xc			/* EL3 */
	b.eq	from_el3
	cmp	x0, #0x8			/* EL2 */
	b.eq	from_el2
	b	el1_entry			/* already EL1 */

from_el3:
	/* Give EL2 a clean AArch64 world and hand over to the EL2 path. */
	mov	x0, #0
	msr	sctlr_el2, x0
	msr	hcr_el2, xzr
	mov	x0, #0x30			/* SCR_EL3: NS | IRQ | FIQ */
	msr	scr_el3, x0
	adr	x1, from_el2
	msr	elr_el3, x1
	mov	x0, #0x3c9			/* SPSR_EL3: EL2h, DAIF masked */
	msr	spsr_el3, x0
	isb
	eret

from_el2:
	/* Stack: EL2 has its own SP_EL2; set it before anything else. */
	ldr	x0, =__stack_top
	mov	sp, x0

	/* The loader may leave the EL2 MMU and caches on; turn them off
	 * before touching memory that was mapped behind our back. Full
	 * clear, not a bit-edit: the loader's residue is unknown. */
	dsb	sy
	msr	sctlr_el2, xzr
	isb

	/* Let EL1 run AArch64, reach the counters, and take no traps. */
	mov	x0, #0x80000000			/* HCR_EL2.RW = 1 */
	msr	hcr_el2, x0
	mrs	x0, cnthctl_el2
	orr	x0, x0, #3			/* EL1PCTEN | EL1PCEN */
	msr	cnthctl_el2, x0
	msr	cptr_el2, xzr			/* no FP/SIMD trapping */
	msr	hstr_el2, xzr
	msr	vttbr_el2, xzr

	adr	x1, el1_entry
	msr	elr_el2, x1
	mov	x0, #0x3c5			/* SPSR_EL2: EL1h, DAIF masked
						 * - masking rides ERET into
						 * EL1, no re-mask needed */
	msr	spsr_el2, x0
	isb
	eret

el1_entry:
	/* Use SP_EL1 (the loader may hand over with SPSEL=0) and clear SCTLR
	 * so MMU and caches are off regardless of what the loader left. */
	msr	spsel, #1
	msr	sctlr_el1, xzr
	ic	iallu
	dsb	sy
	isb

	/* FP/SIMD access enabled at EL1. The build is -mgeneral-regs-only so
	 * no FP instruction should exist; the enable is inherited policy from
	 * the board-proven carrier ("no surprises vs the prior image"). */
	mrs	x0, cpacr_el1
	orr	x0, x0, #(3 << 20)
	msr	cpacr_el1, x0

	/* Boot vector table (located by symbol, not by offset - .text.boot
	 * must stay the first thing in the image for `go`). */
	adr	x0, boot_vectors
	msr	vbar_el1, x0
	isb

	ldr	x0, =__stack_top
	mov	sp, x0

	/* Clear BSS: the C runtime relies on zero-initialised statics. Plain
	 * 8-byte stores - no library calls that might use dc zva. */
	ldr	x0, =__bss_start
	ldr	x1, =__bss_end
1:	cmp	x0, x1
	b.hs	2f
	str	xzr, [x0], #8
	b	1b
2:

	/* Observe, do not depend: what `go` passed in x0 (SPEC open question
	 * 2 - semantics unproven, logged for evidence only). */
	ldr	x1, =boot_arg_x0
	str	x0, [x1]

	/* Identity-mapped MMU with caches on. Needed before C code runs:
	 * unaligned Normal access must work, and the cache-maintenance
	 * discipline requires a real data cache. */
	bl	mmu_enable

	bl	main

3:	wfe
	b	3b
	.size	_start, . - _start

	.section .bss, "aw", %nobits
	.align 3
	.globl	boot_arg_x0
	.type	boot_arg_x0, %object
boot_arg_x0:
	.skip 8

/*
 * Boot vector table. Present so a fault taken before any handler exists
 * lands somewhere diagnosable instead of being ignored. Each entry reports
 * the exception kind through the polled console, then parks.
 */
	.section .vectors, "ax"
	.align	11
	.globl	boot_vectors
boot_vectors:
	/* Current EL with SP_EL0 */
	.p2align 7
	b	boot_sync_fault
	.p2align 7
	b	boot_irq_entry
	.p2align 7
	b	boot_fiq_fault
	.p2align 7
	b	boot_serr_fault
	/* Current EL with SP_ELx */
	.p2align 7
	b	boot_sync_fault
	.p2align 7
	b	boot_irq_entry
	.p2align 7
	b	boot_fiq_fault
	.p2align 7
	b	boot_serr_fault
	/* Lower EL, AArch64 - nothing below us should interrupt; fatal. */
	.p2align 7
	b	boot_sync_fault
	.p2align 7
	b	boot_irq_fault
	.p2align 7
	b	boot_fiq_fault
	.p2align 7
	b	boot_serr_fault
	/* Lower EL, AArch32 */
	.p2align 7
	b	boot_sync_fault
	.p2align 7
	b	boot_irq_fault
	.p2align 7
	b	boot_fiq_fault
	.p2align 7
	b	boot_serr_fault

/* Each fault path prints a distinguishable marker, then parks. The console
 * is polled and needs no interrupt, so it is usable this early.
 *
 * The synchronous path additionally reports ESR_EL1 / FAR_EL1 / ELR_EL1: the
 * exception class + faulting address are what turn a coin toss into a
 * diagnosis. uart_early_panic re-programs the UART first, so these prints
 * work even when the fault precedes console init. */
	.type	boot_sync_fault, %function
boot_sync_fault:
	ldr	x0, =msg_sync
	bl	uart_early_panic
	ldr	x0, =msg_esr
	bl	uart_early_puts
	mrs	x0, esr_el1
	bl	uart_early_put_hex32
	ldr	x0, =msg_far
	bl	uart_early_puts
	mrs	x0, far_el1
	bl	uart_early_put_hex32
	ldr	x0, =msg_elr
	bl	uart_early_puts
	mrs	x0, elr_el1
	bl	uart_early_put_hex32
	ldr	x0, =msg_newline
	bl	uart_early_puts
	b	boot_fault_common
	.type	boot_irq_fault, %function
boot_irq_fault:
	/* An IRQ on a LOWER-EL vector slot: nothing below us exists that
	 * could interrupt us - report and park like any other fault. The
	 * normal IRQ path at the current EL is boot_irq_entry above. */
	ldr	x0, =msg_irq
	b	boot_fault_common
	.type	boot_fiq_fault, %function
boot_fiq_fault:
	ldr	x0, =msg_fiq
	b	boot_fault_common
	.type	boot_serr_fault, %function
boot_serr_fault:
	ldr	x0, =msg_serr
	b	boot_fault_common

boot_fault_common:
	/* Report, then park with interrupts masked. Deliberately does not try
	 * to recover: there is no context to return to. uart_early_panic
	 * re-programs the UART first, so a fault that precedes console init
	 * can still speak. */
	bl	uart_early_panic
	ldr	x0, =msg_parked
	bl	uart_early_puts
1:	wfe
	b	1b

/* Interrupt entry from the current EL: acknowledge, dispatch, end - in C.
 *
 * The exception entry hardware has already masked IRQs (PSTATE.I set) and
 * stashed the return state in ELR_EL1/SPSR_EL1, so the only context to
 * preserve is the interrupted C code's caller-clobbered registers: the C
 * ABI makes a callee preserve x19-x28 and nothing else, so x0-x18 and lr
 * must be saved here. sp is SP_EL1 already (el1_entry set SPSEL=1); the
 * 160-byte frame sits inside the boot stack, well above what the C
 * dispatcher uses. */
	.type	boot_irq_entry, %function
boot_irq_entry:
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
	stp	x18, x30, [sp, #144]

	bl	gic_irq_dispatch

	ldp	x0, x1, [sp, #0]
	ldp	x2, x3, [sp, #16]
	ldp	x4, x5, [sp, #32]
	ldp	x6, x7, [sp, #48]
	ldp	x8, x9, [sp, #64]
	ldp	x10, x11, [sp, #80]
	ldp	x12, x13, [sp, #96]
	ldp	x14, x15, [sp, #112]
	ldp	x16, x17, [sp, #128]
	ldp	x18, x30, [sp, #144]
	add	sp, sp, #160
	eret

	.section .rodata, "a"
	.globl	msg_sync
msg_sync:	.asciz	"fatal: synchronous exception\n"
	.globl	msg_irq
msg_irq:	.asciz	"fatal: IRQ before scheduler\n"
	.globl	msg_fiq
msg_fiq:	.asciz	"fatal: FIQ\n"
	.globl	msg_serr
msg_serr:	.asciz	"fatal: SError\n"
msg_esr:	.asciz	"fatal: ESR_EL1="
msg_far:	.asciz	" FAR_EL1="
msg_elr:	.asciz	" ELR_EL1="
msg_newline:	.asciz	"\n"
msg_parked:	.asciz	"fatal: parked; power-cycle to recover\n"
