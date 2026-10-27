/*
 * @file   start.S
 * @brief  Boot entry: normalise to EL1, install vectors, clear BSS, enter C.
 *
 * Every step of this sequence was established on this board by experiment;
 * deviating from it is what breaks boot. Two deliberate shapes here: the
 * debug-stub branches are absent (this image has no stub), and the
 * pre-scheduler IRQ slot reports and parks like the other fault slots -
 * once the RTOS port owns interrupts there is no legitimate IRQ before the
 * scheduler starts, so an early one is a fault to be seen, not a call to
 * dispatch.
 *
 * U-Boot's `go` hands over at EL2 (non-secure) on this board; EL3 and EL1
 * paths exist as defensive convergences.
 *
 * Three board facts drive the EL2 sequence (firefly v1 / this SoC evidence,
 * 2026-10-16; the RK3568 carrier's simpler order survived there by luck,
 * not by architecture):
 *
 *  - The loader received our image through DMA writes that landed in ITS
 *    D-cache. Disabling the D-cache without first cleaning it orphans those
 *    dirty lines - DRAM keeps the stale bytes and the fetch after the
 *    SCTLR change executes garbage (two board rounds died with an EC=0
 *    abort on the isb right after `msr sctlr_el2`, with both a full-zero
 *    and an M/C/I read-modify-write). So: flush the whole data cache
 *    BEFORE touching SCTLR_EL2.
 *
 *  - SCTLR_EL2 is cleared with a read-modify-write of the M/C/I bits only:
 *    a full-register zero also clears RES1 bits, which is UNPREDICTABLE.
 *
 *  - CNTHCTL_EL2.EL1PCEN / EL1PCTEN must be set (bits 0 and 1) or EL1 cannot
 *    reach the counters and every access traps into the old EL2 vector
 *    table.
 *
 * CurrentEL encodes the level as 0x4 / 0x8 / 0xC (not 1 / 2 / 3) - a classic
 * source of misrouted boot paths.
 *
 * .S files run the preprocessor, so board_conf.h (data-only #defines) is
 * includable directly.
 *
 * Bring-up breadcrumbs: every stage prints one short line through the
 * console UART the loader left live (a raw PL011 poll-write; the image's
 * own uart_early_init later re-programs the same part). A boot that dies
 * now says WHERE on the serial line instead of nothing.
 */

#include "board_conf.h"

	.arch	armv8-a
	.section .text.boot, "ax"

/* Breadcrumb emitter (definition before first use: gas macros must be).
 * The function body lives with the other pre-MMU helpers below. */
.macro	EARLY_PUTS msg
	ldr	x0, =\msg
	bl	early_puts_raw
.endm

	.globl	_start
	.type	_start, %function
_start:
	/* Mask everything until EL1 is set up. */
	msr	daifset, #0xf

	/* This path is boot-core-only. A secondary running here would clear
	 * the BSS under the running image and re-derive state that must only
	 * exist once. The other cores are held by the firmware; if one lands
	 * here anyway, park it where it cannot do harm. The logical core
	 * number comes from BOARD_MPIDR_CORE_SHIFT (Aff0 on firefly v1 per
	 * the DTS cpu reg numbering; Aff1 on RK3568 - a per-SoC fact). */
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
	/* VERBATIM VENDOR SEQUENCE (user directive, 2026-10-16): this block
	 * replicates the reference standalone SDK's fboot.S El2Entry instruction
	 * for instruction - order AND values - because that is the one
	 * sequence proven on this silicon from this loader. Notable vendor
	 * values: cptr_el2 = 0x33ff (NOT a plain zero), hcr_el2 = RW only,
	 * both SCTLR levels cleared through the same M/C/I read-modify-write,
	 * cnthctl = 3, spsr = EL1h|DAIF. The breadcrumbs between steps are
	 * the only addition (they print through the loader's live UART and
	 * have been proven harmless). Reference source of truth: the vendor
	 * SDK's arch/armv8/aarch64/gcc/fboot.S + fboot.h
	 * (disable_mmu / dcache macros). */
	ldr	x0, =__stack_top
	mov	sp, x0
	EARLY_PUTS	msg_el2

	/* dcache cisw: clean + invalidate every level, set by set, way by way
	 * (the architecture's CLIDR/CCSIDR walk - flush_dcache_all below). */
	bl	flush_dcache_all
	EARLY_PUTS	msg_dcache

	/* disable_mmu sctlr_el2, x9 */
	mrs	x9, sctlr_el2
	bic	x9, x9, #(1 << 0)
	bic	x9, x9, #(1 << 2)
	bic	x9, x9, #(1 << 12)
	msr	sctlr_el2, x9
	isb
	EARLY_PUTS	msg_el2mmu
	ic	iallu
	dsb	nsh
	isb
	EARLY_PUTS	msg_ic

	/* hcr_el2 = RW (AArch64 EL1); cptr_el2 = 0x33ff (vendor value); hstr
	 * and vttbr cleared. */
	mov	x9, #(1 << 31)
	msr	hcr_el2, x9
	mov	x9, #0x33ff
	msr	cptr_el2, x9
	msr	hstr_el2, xzr
	msr	vttbr_el2, xzr

	/* disable_mmu sctlr_el1, x9 (from EL2, before the first EL1 fetch) */
	mrs	x9, sctlr_el1
	bic	x9, x9, #(1 << 0)
	bic	x9, x9, #(1 << 2)
	bic	x9, x9, #(1 << 12)
	msr	sctlr_el1, x9
	isb

	/* spsr_el2 = EL1h with DAIF masked; CNTHCTL_EL2 = 3 (EL1 timer
	 * access). */
	mov	x9, #0x3c5
	msr	spsr_el2, x9
	mov	x0, #0x3
	msr	cnthctl_el2, x0

	EARLY_PUTS	msg_eret
	adr	x1, el1_entry
	msr	elr_el2, x1
	eret

el1_entry:
	/* FIRST instruction prints: this breadcrumb is the proof the ERET
	 * landed here at all (round 5 died between "eret to el1" and this
	 * line - the EL2-side SCTLR_EL1 store had poisoned the EL1 regime;
	 * that store is gone now). */
	EARLY_PUTS	msg_el1
	msr	spsel, #1
	/* SCTLR_EL1 probe: the raw value goes out, and the M/C/I bits are
	 * cleared ONLY if one of them is actually set - each store is
	 * followed by a full TLB invalidate, because on this silicon a
	 * SCTLR store without one makes the regime's next fetch
	 * UNPREDICTABLE (the round 1-5 evidence; the reset value normally
	 * has all three clear, so the common path writes nothing). */
	mrs	x0, sctlr_el1
	bl	early_puthex32
	EARLY_PUTS	msg_sctlr1
	tbnz	x0, #0, 6f
	tbnz	x0, #2, 6f
	tbnz	x0, #12, 6f
	b	7f
6:	bic	x0, x0, #1
	bic	x0, x0, #4
	bic	x0, x0, #0x1000
	msr	sctlr_el1, x0
	tlbi	vmalle1
	dsb	sy
	isb
7:	ic	iallu
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

	/* Observe, do not depend: what `go` passed in x0 (logged for evidence
	 * only). */
	ldr	x1, =boot_arg_x0
	str	x0, [x1]

	/* Identity-mapped MMU with caches on. Needed before C code runs:
	 * unaligned Normal access must work, and the cache-maintenance
	 * discipline requires a real data cache. */
	EARLY_PUTS	msg_mmu
	bl	mmu_enable
	EARLY_PUTS	msg_main
	bl	main

3:	wfe
	b	3b
	.size	_start, . - _start

/* --- bring-up helpers (pre-MMU, same section) ----------------------------- */

/* One breadcrumb line through the console UART the loader left live.
 * Raw PL011 semantics: FR at +0x18 (TXFF = bit 5), DR at +0. The image's
 * own uart_early_init re-programs the part later; until then this just
 * borrows the loader's configuration. */
early_puts_raw:					/* x0: NUL-terminated string */
1:	ldrb	w1, [x0], #1
	cbz	w1, 2f
	ldr	x2, =BOARD_UART_BASE
3:	ldr	w3, [x2, #0x18]
	tbnz	w3, #5, 3b			/* wait for TX FIFO room */
	str	w1, [x2]
	b	1b
2:	ret

/* One 32-bit hex word (no prefix/newline) through the live loader UART -
 * the probe channel for register values during bring-up. */
early_puthex32:				/* x0: value */
	ldr	x2, =BOARD_UART_BASE
	mov	x3, #28
1:	lsr	w1, w0, w3
	and	w1, w1, #0xf
	cmp	w1, #9
	b.hi	2f
	add	w1, w1, #'0'
	b	3f
2:	add	w1, w1, #('a' - 10)
3:	ldr	w4, [x2, #0x18]
	tbnz	w4, #5, 3b			/* wait for TX FIFO room */
	str	w1, [x2]
	sub	w3, w3, #4
	b.ge	1b
	ret

/* Clean + invalidate the whole data cache, level by level, set by set,
 * way by way - the architecture's CLIDR/CCSIDR maintenance walk (every
 * OS port carries this recipe; the labels and comments are this file's).
 * MUST run before the caches are disabled: dirty lines orphaned by a
 * cache disable leave stale DRAM behind, and the loader's DMA writes put
 * our image's bytes in exactly those lines (board-fact block above). */
flush_dcache_all:
	dsb	sy
	mrs	x0, clidr_el1
	and	x3, x0, #0x7000000		/* cache levels present */
	lsr	x3, x3, #23
	cbz	x3, 5f				/* none: nothing to flush */
	mov	x10, #0				/* current cache level */
1:	add	x2, x10, x10, lsr #1		/* 3-bit Ctype stride = level*1.5 */
	lsr	x1, x0, x2
	and	x1, x1, #7			/* this level's cache type */
	cmp	x1, #2				/* < 2: no D/unified cache here */
	b.lt	4f
	msr	csselr_el1, x10
	isb
	mrs	x1, ccsidr_el1			/* the level's geometry */
	and	x2, x1, #7			/* line-size field */
	add	x2, x2, #4			/* set-field shift */
	mov	x4, #0x3ff
	and	x4, x4, x1, lsr #3		/* associativity - 1 */
	clz	w5, w4				/* way field's bit position */
	mov	x7, #0x7fff
	and	x7, x7, x1, lsr #13		/* number of sets - 1 */
2:	mov	x9, x4				/* ways countdown */
3:	lsl	x6, x9, x5			/* way into its field */
	orr	x11, x10, x6
	lsl	x6, x7, x2			/* set into its field */
	orr	x11, x11, x6
	dc	cisw, x11
	subs	x9, x9, #1
	b.ge	3b
	subs	x7, x7, #1
	b.ge	2b
4:	add	x10, x10, #2			/* next level's Ctype slot */
	cmp	x10, x3
	b.lt	1b
5:	dsb	sy
	isb
	ret

	.section .bss, "aw", %nobits
	.align 3
	.globl boot_arg_x0
	.type boot_arg_x0, %object
boot_arg_x0:
	.skip 8

	.section .rodata, "a"
msg_el2:	.asciz	"boot: el2 entry\n"
msg_dcache:	.asciz	"boot: dcache flushed\n"
msg_el2mmu:	.asciz	"boot: el2 mmu off\n"
msg_ic:		.asciz	"boot: ic iallu\n"
msg_eret:	.asciz	"boot: eret to el1\n"
msg_el1:	.asciz	"boot: el1\n"
msg_sctlr1:	.asciz	" boot: sctlr_el1 above\n"
msg_mmu:	.asciz	"boot: mmu_enable\n"
msg_main:	.asciz	"boot: main\n"
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

/*
 * Boot vector table. Present so a fault taken before any handler exists
 * lands somewhere diagnosable instead of being ignored. Each entry reports
 * the exception kind through the polled console, then parks. The IRQ slots
 * fault too: the scheduler-start path keeps IRQs masked in the CPU, so a
 * pre-scheduler IRQ means something is wrong and must be visible.
 */
	.section .vectors, "ax"
	.align	11
	.globl boot_vectors
boot_vectors:
	/* Current EL with SP_EL0 */
	.p2align 7
	b	boot_sync_fault
	.p2align 7
	b	boot_irq_fault
	.p2align 7
	b	boot_fiq_fault
	.p2align 7
	b	boot_serr_fault
	/* Current EL with SP_ELx */
	.p2align 7
	b	boot_sync_fault
	.p2align 7
	b	boot_irq_fault
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
	/* Any IRQ before the scheduler starts: nothing legitimate can
	 * interrupt us yet - report and park like any other fault. */
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
