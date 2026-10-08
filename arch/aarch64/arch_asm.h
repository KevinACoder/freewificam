/* SPDX-License-Identifier: BSD-2-Clause */
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

#endif /* ARCH_AARCH64_ARCH_ASM_H */
