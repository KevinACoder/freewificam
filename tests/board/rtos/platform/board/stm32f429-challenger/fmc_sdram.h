/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * fmc_sdram.h - STM32F429 FMC SDRAM (W9825G6, 32 MB) bring-up.
 *
 * The core board carries the 32 MB W9825G6 on FMC SDRAM bank 2
 * (0xD0000000).  The bring-up is called from main() after the console is up
 * and before any FreeRTOS allocation, because the wireless profiles place the
 * kernel heap in this memory; a failure has to be spoken on the wire before
 * the first pvPortMalloc() can touch uninitialised SDRAM.
 *
 * The 128 MB NAND on the same core board shares the FMC data bus; its control
 * lines are driven to the deselected state before the SDRAM pins are claimed.
 */

#ifndef FREEWIFICAM_STM32F429_FMC_SDRAM_H
#define FREEWIFICAM_STM32F429_FMC_SDRAM_H

#include <stdint.h>

/* 32 MB, the W9825G6's density: 4 banks x 8192 rows x 512 columns x 16 bit. */
#define F429_SDRAM_BASE		0xD0000000UL
#define F429_SDRAM_SIZE		(32UL * 1024UL * 1024UL)

/*
 * Claim the NAND hold-off, the FMC clocks/pins and the SDRAM controller, run
 * the JEDEC initialisation sequence, program the refresh rate and run the
 * bounded self-test.  Prints the board anchors:
 *
 *   f429: sdram init ok
 *   f429: sdram test ok size=33554432
 *
 * or a named failure with the failing stage and offset.  Returns 0 on pass,
 * non-zero on the first failing stage.  Call once, from main(), after the
 * console's uart_early_init() and before the kernel heap is first used.
 */
int f429_memory_boot(void);

/* The init alone (no self-test, no console), for a caller that wants to run
 * its own test.  Returns 0 on pass. */
int f429_sdram_init(void);

#endif /* FREEWIFICAM_STM32F429_FMC_SDRAM_H */
