/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * arch_asm.h - the AArch64 common-instruction macros. The only place
 * component/port/OS/app code takes these instructions from; run
 * discipline: bare __asm__ blocks for this family must not be scattered
 * through the tree (the arch modules - generic_timer.c, gicv3's ICC regs,
 * mmu's system controls - keep their own module-owned register asm).
 */

#ifndef ARCH_AARCH64_ARCH_ASM_H
#define ARCH_AARCH64_ARCH_ASM_H

/* --- instruction synchronization barriers --- */

#define ARCH_ISB()	__asm__ __volatile__("isb" ::: "memory")
#define ARCH_DSB()	__asm__ __volatile__("dsb sy" ::: "memory")
/* Store-only barrier: DMA-visible stores ordered ahead of the doorbell
 * register write without paying the full-system drain (the GMAC OWN-grant
 * discipline). */
#define ARCH_DSB_ST()	__asm__ __volatile__("dsb st" ::: "memory")
#define ARCH_DMB()	__asm__ __volatile__("dmb ish" ::: "memory")

/* --- wait instructions --- */

#define ARCH_WFI()	__asm__ __volatile__("wfi" ::: "memory")
#define ARCH_WFE()	__asm__ __volatile__("wfe")

/* --- DAIF interrupt masking --- */
/* bits: D(8) I(7) A(6) F(6+2); #2 = IRQ (I) alone, #0xf = everything. */

#define ARCH_DAIF_SET(bits)	\
	__asm__ __volatile__("msr daifset, #" #bits ::: "memory")
#define ARCH_DAIF_CLR(bits)	\
	__asm__ __volatile__("msr daifclr, #" #bits ::: "memory")
#define ARCH_DAIF_GET(_v)	__asm__ __volatile__("mrs %0, daif" : "=r"(_v))
#define ARCH_DAIF_PUT(_v)	\
	__asm__ __volatile__("msr daif, %0" :: "r"(_v) : "memory")

/* the common IRQ-only pair */
#define ARCH_IRQ_MASK()		ARCH_DAIF_SET(2)
#define ARCH_IRQ_UNMASK()	ARCH_DAIF_CLR(2)

/* --- system register access --- */
/* one-line MRS/MSR for any named sysreg: the asm spelling lives here,
 * call sites stay portable C ("mrs %0, sctlr_el1" etc. must not be
 * scattered - app anchors, fault dumps, gdb hooks all go through these) */
#define ARCH_MRS(_reg, _out)	\
	__asm__ __volatile__("mrs %0, " #_reg : "=r"(_out))
#define ARCH_MSR(_reg, _val)	\
	__asm__ __volatile__("msr " #_reg ", %0" :: "r"(_val) : "memory")

/* --- wide block copy ------------------------------------------------------ */
/* 64 bytes per expansion through four LDP/STP pairs, plus an 8-byte chunk.
 * Both pointers may be unaligned: Normal memory tolerates unaligned pair
 * accesses (SCTLR.A stays 0 on this image - start.S clears SCTLR_EL1 and
 * mmu.c only sets M/C/I), which is the whole point. The compiler's own copy
 * loop moves one byte-aligned u64 per iteration because -mstrict-align
 * forbids it from assuming anything wider; the census priced the image's
 * memcpy at 1.5 us for a 1514-byte frame, and most of that is the serial
 * loop rather than the memory system. Device memory must NOT use these
 * (unaligned accesses to Device memory fault); callers copy between Normal
 * regions. */
#define ARCH_COPY_64(_dst, _src)					\
	__asm__ __volatile__(						\
		"ldp x4, x5, [%1]\n\t"					\
		"ldp x6, x7, [%1, #16]\n\t"				\
		"ldp x8, x9, [%1, #32]\n\t"				\
		"ldp x10, x11, [%1, #48]\n\t"				\
		"stp x4, x5, [%0]\n\t"					\
		"stp x6, x7, [%0, #16]\n\t"				\
		"stp x8, x9, [%0, #32]\n\t"				\
		"stp x10, x11, [%0, #48]\n\t"				\
		: : "r"(_dst), "r"(_src)				\
		: "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "memory")

#define ARCH_COPY_8(_dst, _src)						\
	__asm__ __volatile__(						\
		"ldr x4, [%1]\n\t"					\
		"str x4, [%0]\n\t"					\
		: : "r"(_dst), "r"(_src) : "x4", "memory")

#endif /* ARCH_AARCH64_ARCH_ASM_H */
