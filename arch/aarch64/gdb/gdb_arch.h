/*
 * @file   gdb_arch.h
 * @brief  AArch64 (ARMv8-A, EL1) side of the serial GDB stub.
 *
 * The trapframe layout is the contract between the vector entry
 * (start.S's boot_sync_entry, which builds the frame) and this stub. It
 * mirrors the prior image's shape (itself from FreeBSD/fgdb): six saved
 * system registers first, then x0..x29, with LR kept separately so the C
 * struct reads naturally.
 */

#ifndef FREEWEBCAM_GDB_ARCH_H
#define FREEWEBCAM_GDB_ARCH_H

#include <stdint.h>

struct gdb_trapframe {
	unsigned long	sp;	/* 0x00  interrupted SP */
	unsigned long	lr;	/* 0x08  x30 */
	unsigned long	elr;	/* 0x10  ELR_EL1 at entry */
	unsigned long	spsr;	/* 0x18  SPSR_EL1 at entry */
	unsigned long	esr;	/* 0x20  ESR_EL1 */
	unsigned long	far;	/* 0x28  FAR_EL1 */
	unsigned long	x[30];	/* 0x30  x0..x29 */
};

#define GDB_TF_SIZE	sizeof(struct gdb_trapframe)

/* GDB register file indices. The register set is whatever the served
 * target description (gdb_main.c's target_xml) declares - exactly these 34
 * core registers, in exactly this order; the g packet must match it byte
 * for byte. The FP bank is not declared at all: this image is built
 * -mgeneral-regs-only and has no FP state to report (a longer packet with
 * 'x' placeholders makes GDB reject the reply outright). */
#define GDB_REG_X0		0
#define GDB_REG_LR		30
#define GDB_REG_SP		31
#define GDB_REG_PC		32
#define GDB_REG_CPSR		33
#define GDB_NREGS		34

/* The stub's reserved BRK immediate (fgdb convention, ISS field match). */
#define GDB_BRK_IMM		0x401

/* Signal the exception maps to (SIGTRAP for the debug classes, SIGSEGV/
 * SIGBUS/SIGILL for real faults - faults stop in the stub too). */
int gdb_cpu_signal(const struct gdb_trapframe *tf);

/* Debug-exception entry: owns the CPU until the host continues. The ONE
 * sync-exception sink on the gdb build - real faults included. */
void gdb_debug_interrupt(struct gdb_trapframe *tf);

#endif /* FREEWEBCAM_GDB_ARCH_H */
