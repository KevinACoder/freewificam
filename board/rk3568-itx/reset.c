/*
 * @file   reset.c
 * @brief  System reset through the board's PSCI conduit (S4, spec §3.7.2).
 *
 * The board discipline prefers a warm reset over a power cycle: the UStone
 * switch is the only cold path, and switching power is the slowest and most
 * wearing way back to U-Boot, so a running image should be able to put
 * itself back at the firmware prompt. On this SoC that is PSCI
 * SYSTEM_RESET - the same path U-Boot's own `reset` command takes
 * (PSCI_0_2_FN_SYSTEM_RESET, ARM DEN 0022).
 *
 * Task-context callable (the shell's reboot command). A reset that works
 * never returns; a return value therefore means the firmware REFUSED the
 * call (PSCI_NOT_SUPPORTED for an unimplemented function). Callers print
 * it and stay up.
 *
 * The call runs with IRQs masked (daifset): between the SMC and the SoC
 * reset the CPU must not take an interrupt, or a console RX interrupt
 * would re-enter the shell adapter on a system that is already going
 * down. Console draining is the caller's job - this layer has no kernel
 * clock to wait on.
 */

#include <stdint.h>

/* PSCI 1.x function ids (SMC calling convention, ARM DEN 0022).
 * SYSTEM_RESET exists only in the SMC32 form. */
#define PSCI_SYSTEM_RESET	0x84000009UL

static long psci_call_reset(uint64_t fid)
{
	register uint64_t r0 __asm__("x0") = fid;

	__asm__ __volatile__("msr daifset, #2" ::: "memory");
	__asm__ __volatile__("smc #0" : "+r"(r0) : : "memory", "cc");
	return (long)r0;
}

/* 0 on an accepted call (the board is already resetting at that point in
 * the successful case), or the firmware's refusal code. */
int board_system_reset(void)
{
	return (int)psci_call_reset(PSCI_SYSTEM_RESET);
}
