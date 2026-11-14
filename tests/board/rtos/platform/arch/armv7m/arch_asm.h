/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * arch_asm.h - the ARMv7-M common-instruction macros.
 *
 * The app layer takes barriers, wait instructions and interrupt masking from
 * here; the AArch64 version of this file is platform/arch/aarch64/arch_asm.h.
 * Only the names the shared app uses are carried.
 */

#ifndef ARCH_ARM_V7M_ARCH_ASM_H
#define ARCH_ARM_V7M_ARCH_ASM_H

#define ARCH_ISB()	__asm__ __volatile__("isb" ::: "memory")
#define ARCH_DSB()	__asm__ __volatile__("dsb" ::: "memory")
#define ARCH_DSB_ST()	__asm__ __volatile__("dsb" ::: "memory")
#define ARCH_DMB()	__asm__ __volatile__("dmb" ::: "memory")

#define ARCH_WFI()	__asm__ __volatile__("wfi" ::: "memory")
#define ARCH_WFE()	__asm__ __volatile__("wfe")

/* PRIMASK has one meaning: all configurable interrupts masked.  The shared
 * app only uses the "mask everything" and "unmask" forms. */
#define ARCH_DAIF_SET(bits)	__asm__ __volatile__("cpsid i" ::: "memory")
#define ARCH_DAIF_CLR(bits)	__asm__ __volatile__("cpsie i" ::: "memory")
#define ARCH_DAIF_GET(_v)	__asm__ __volatile__("mrs %0, primask" : "=r"(_v))
#define ARCH_DAIF_PUT(_v)	__asm__ __volatile__("msr primask, %0" :: "r"(_v) : "memory")

#define ARCH_IRQ_MASK()		ARCH_DAIF_SET(2)
#define ARCH_IRQ_UNMASK()	ARCH_DAIF_CLR(2)

#endif /* ARCH_ARM_V7M_ARCH_ASM_H */
