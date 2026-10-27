/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   reset.c
 * @brief  System reset through the board's PSCI conduit (firefly family).
 *
 * A warm reset is the cheap way back to the firmware prompt: the lab's cold
 * path is the switched power supply, so a running image should be able to
 * put itself back to U-Boot. On these SoCs that is PSCI SYSTEM_RESET (ARM
 * DEN 0022) - the same call the resident firmware serves for U-Boot's own
 * `reset` (every firefly device tree's enable-method is "psci").
 *
 * Callable from task context (the console's reboot command). A reset that
 * works never returns; a return value therefore means the firmware REFUSED
 * the call. Callers report it and stay up.
 *
 * The call runs with IRQs masked: between the SMC and the SoC reset the CPU
 * must not take an interrupt, or a console RX interrupt would re-enter the
 * shell on a system that is already going down. Draining the console line
 * is the caller's job - this layer has no clock to wait on.
 *
 * @design arch-module rule of this repository: bare system-register and SMC
 * sequences live in one place, not scattered through callers. The PSCI
 * calling convention is ARM DEN 0022; SYSTEM_RESET exists only in the SMC32
 * form.
 */

#include <stdint.h>

#include "reset.h"

/* PSCI 1.x function ids (SMC calling convention, ARM DEN 0022). */
#define PSCI_SYSTEM_RESET	0x84000009UL

static long psci_call_reset(uint64_t fid)
{
	register uint64_t r0 __asm__("x0") = fid;

	__asm__ __volatile__("msr daifset, #2" ::: "memory");
	__asm__ __volatile__("smc #0" : "+r"(r0) : : "memory", "cc");
	return (long)r0;
}

int board_system_reset(void)
{
	return (int)psci_call_reset(PSCI_SYSTEM_RESET);
}
