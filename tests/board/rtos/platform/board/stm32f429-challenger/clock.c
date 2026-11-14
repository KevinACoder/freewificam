/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * clock.c - STM32F429 clock tree: 25 MHz HSE -> 168 MHz, or HSI 16 MHz.
 *
 * SystemInit is called from the arch Reset_Handler before main().  The clock
 * is the only board fact the rest of the platform needs at that point: UART1
 * and the DWT microsecond clock both derive from SystemCoreClock.
 */

#include <stdint.h>

#include "board.h"
#include "board_conf.h"

#define RCC_BASE	0x40023800UL
#define RCC_CR		(*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_PLLCFGR	(*(volatile uint32_t *)(RCC_BASE + 0x04))
#define RCC_CFGR	(*(volatile uint32_t *)(RCC_BASE + 0x08))
#define RCC_APB1ENR	(*(volatile uint32_t *)(RCC_BASE + 0x40))

#define PWR_BASE	0x40007000UL
#define PWR_CR		(*(volatile uint32_t *)(PWR_BASE + 0x00))

#define FLASH_ACR	(*(volatile uint32_t *)0x40023C00UL)

#define SCB_CPACR	(*(volatile uint32_t *)0xE000ED88UL)
#define SCB_VTOR	(*(volatile uint32_t *)0xE000ED08UL)
#define SCB_AIRCR	(*(volatile uint32_t *)0xE000ED0CUL)
#define SCB_ACTLR	(*(volatile uint32_t *)0xE000E008UL)
#define SCB_SHCSR	(*(volatile uint32_t *)0xE000ED24UL)

#define RCC_CR_HSEON	(1UL << 16)
#define RCC_CR_HSERDY	(1UL << 17)
#define RCC_CR_PLLON	(1UL << 24)
#define RCC_CR_PLLRDY	(1UL << 25)
#define RCC_CFGR_SW_PLL	2UL
#define RCC_CFGR_SWS_PLL (2UL << 2)

uint32_t SystemCoreClock = 16000000UL;
uint32_t g_clock_fallback = 1;

static int wait_bits(volatile uint32_t *reg, uint32_t mask, uint32_t timeout)
{
	while (timeout--) {
		if ((*reg & mask) == mask) {
			return 1;
		}
	}
	return 0;
}

static void use_hsi(void)
{
	RCC_CFGR &= ~3UL;
	SystemCoreClock = 16000000UL;
	g_clock_fallback = 1;
}

void SystemInit(void)
{
	/* Four priority bits, FPU on, vector table at the F429 flash base. */
	SCB_AIRCR = (0x5FAUL << 16) | (3UL << 8);
	SCB_CPACR |= (0xFUL << 20);
	SCB_VTOR = BOARD_FLASH_BASE;

	/* Make bus faults precise and give MemManage/BusFault/UsageFault their
	 * own vectors instead of escalating everything to HardFault.  This is
	 * diagnostic value, not a workaround: an imprecise store fault reports
	 * the next instruction and hides the address that was written. */
	SCB_ACTLR |= (1UL << 1);
	SCB_SHCSR |= (1UL << 16) | (1UL << 17) | (1UL << 18);

	RCC_CR |= RCC_CR_HSEON;
	if (!wait_bits(&RCC_CR, RCC_CR_HSERDY, 2000000UL)) {
		use_hsi();
		return;
	}

	/* VOS scale 1, 5 wait states, prefetch + I-cache. */
	RCC_APB1ENR |= (1UL << 28);
	PWR_CR = (PWR_CR & ~(3UL << 14)) | (1UL << 14);
	FLASH_ACR = 5UL | (1UL << 8) | (1UL << 9);

	/* AHB /1, APB1 /4 (42 MHz), APB2 /2 (84 MHz). */
	RCC_CFGR &= ~((0xFUL << 4) | (0x7UL << 10) | (0x7UL << 13) | 3UL);
	RCC_CFGR |= (5UL << 10) | (4UL << 13);

	/* 25 MHz HSE / 25 * 336 / 2 = 168 MHz; /7 = 48 MHz USB. */
	RCC_PLLCFGR = 25UL | (336UL << 6) | (0UL << 16) | (1UL << 22) | (7UL << 24);
	RCC_CR |= RCC_CR_PLLON;
	if (!wait_bits(&RCC_CR, RCC_CR_PLLRDY, 2000000UL)) {
		use_hsi();
		return;
	}

	RCC_CFGR = (RCC_CFGR & ~3UL) | RCC_CFGR_SW_PLL;
	if (!wait_bits(&RCC_CFGR, RCC_CFGR_SWS_PLL, 2000000UL)) {
		use_hsi();
		return;
	}

	SystemCoreClock = 168000000UL;
	g_clock_fallback = 0;
}
