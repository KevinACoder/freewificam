/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * sdio_board.c - the F429 SDIO socket: pins, rail, clock, interrupt priority.
 *
 * Board facts (V2 base schematic, the module socket the withdraw SDIO line
 * used): PC8-PC11 = D0-D3, PC12 = CLK, PD2 = CMD, all AF12 with internal
 * pull-ups; PB13 = the AP6181's WL_REG_ON / WIFI_EN.  The SDIO peripheral's
 * input clock is PLL48CLK (48 MHz at this platform's 168 MHz / PLLQ=7 tree),
 * so the controller's divider gives 400 kHz for enumeration and 24 MHz for
 * the transfer state.
 *
 * The peripheral's register block is programmed here only to a known-off
 * state; the port's sdio_host.c owns every transfer-state register.
 */

#include "sdio_board.h"

#include <stddef.h>

/* --- RCC ------------------------------------------------------------------ */

#define RCC_BASE		0x40023800UL
#define RCC_AHB1ENR		(*(volatile uint32_t *)(RCC_BASE + 0x30UL))
#define RCC_APB2ENR		(*(volatile uint32_t *)(RCC_BASE + 0x44UL))

#define RCC_AHB1ENR_GPIOB	(1UL << 1)
#define RCC_AHB1ENR_GPIOC	(1UL << 2)
#define RCC_AHB1ENR_GPIOD	(1UL << 3)
#define RCC_AHB1ENR_DMA2EN	(1UL << 22)
#define RCC_APB2ENR_SDIOEN	(1UL << 11)
#define RCC_AHB1ENR_MASK	0x3FFFFFUL
#define RCC_APB2ENR_MASK	0x07FFFFFFUL

/* --- GPIO ----------------------------------------------------------------- */

#define GPIOB_BASE		0x40020400UL
#define GPIOC_BASE		0x40020800UL
#define GPIOD_BASE		0x40020C00UL

#define GPIO_MODER(base)	(*(volatile uint32_t *)((base) + 0x00UL))
#define GPIO_OSPEEDR(base)	(*(volatile uint32_t *)((base) + 0x08UL))
#define GPIO_PUPDR(base)	(*(volatile uint32_t *)((base) + 0x0CUL))
#define GPIO_OTYPER(base)	(*(volatile uint32_t *)((base) + 0x04UL))
#define GPIO_BSRR(base)		(*(volatile uint32_t *)((base) + 0x18UL))
#define GPIO_AFR(base)		((volatile uint32_t *)((base) + 0x20UL))

#define GPIO_MODE_INPUT		0UL
#define GPIO_MODE_OUTPUT	1UL
#define GPIO_MODE_AF		2UL
#define GPIO_SPEED_VERY_HIGH	3UL
#define GPIO_SPEED_LOW		2UL
#define GPIO_PULL_NONE		0UL
#define GPIO_PULL_UP		1UL
#define SDIO_AF			12UL
#define WL_REG_ON_PIN		13UL

/* --- SDIO controller (known-off state only) ------------------------------- */

#define SDIO_BASE		0x40012C00UL
#define SDIO_POWER		(*(volatile uint32_t *)(SDIO_BASE + 0x00UL))
#define SDIO_CLKCR		(*(volatile uint32_t *)(SDIO_BASE + 0x04UL))
#define SDIO_DCTRL		(*(volatile uint32_t *)(SDIO_BASE + 0x2CUL))
#define SDIO_DTIMER		(*(volatile uint32_t *)(SDIO_BASE + 0x24UL))
#define SDIO_DLEN		(*(volatile uint32_t *)(SDIO_BASE + 0x28UL))
#define SDIO_ICR		(*(volatile uint32_t *)(SDIO_BASE + 0x38UL))
#define SDIO_MASK		(*(volatile uint32_t *)(SDIO_BASE + 0x3CUL))

/* --- NVIC (no CMSIS device header on this platform) ----------------------- */

#define NVIC_ISER(n)		(*(volatile uint32_t *)(0xE000E100UL + 4UL * (n)))
#define NVIC_ICER(n)		(*(volatile uint32_t *)(0xE000E180UL + 4UL * (n)))
#define NVIC_ICPR(n)		(*(volatile uint32_t *)(0xE000E280UL + 4UL * (n)))
#define NVIC_IPR_BYTE(irq)	(*(volatile uint8_t *)(0xE000E400UL + (irq)))

#define SDIO_IRQ		49U
#define SDIO_DMA_IRQ		59U
/* The F429 implements four priority bits; FreeRTOS's syscall ceiling is 5, so
 * the bus sits at 6 and its ISR may call the FromISR entry points. */
#define SDIO_IRQ_PRIORITY	6U
#define SDIO_IRQ_PRIO_BYTE	(SDIO_IRQ_PRIORITY << 4)

static bool s_wl_reg_on;

static void
gpio_mode(uint32_t base, uint32_t pin, uint32_t mode)
{
	GPIO_MODER(base) = (GPIO_MODER(base) & ~(3UL << (pin * 2U))) |
			   (mode << (pin * 2U));
}

static void
gpio_speed(uint32_t base, uint32_t pin, uint32_t speed)
{
	GPIO_OSPEEDR(base) = (GPIO_OSPEEDR(base) & ~(3UL << (pin * 2U))) |
			     (speed << (pin * 2U));
}

static void
gpio_pull(uint32_t base, uint32_t pin, uint32_t pull)
{
	GPIO_PUPDR(base) = (GPIO_PUPDR(base) & ~(3UL << (pin * 2U))) |
			   (pull << (pin * 2U));
}

static void
gpio_af(uint32_t base, uint32_t pin, uint32_t af)
{
	uint32_t shift = (pin & 7U) * 4U;

	GPIO_AFR(base)[pin >> 3U] = (GPIO_AFR(base)[pin >> 3U] &
				     ~(0xFUL << shift)) | (af << shift);
}

static void
sdio_pin(uint32_t base, uint32_t pin)
{
	gpio_mode(base, pin, GPIO_MODE_AF);
	gpio_speed(base, pin, GPIO_SPEED_VERY_HIGH);
	gpio_pull(base, pin, GPIO_PULL_UP);
	gpio_af(base, pin, SDIO_AF);
}

static void
wl_reg_on_pin(void)
{
	RCC_AHB1ENR |= RCC_AHB1ENR_GPIOB;
	(void)RCC_AHB1ENR;

	gpio_mode(GPIOB_BASE, WL_REG_ON_PIN, GPIO_MODE_OUTPUT);
	gpio_speed(GPIOB_BASE, WL_REG_ON_PIN, GPIO_SPEED_LOW);
	gpio_pull(GPIOB_BASE, WL_REG_ON_PIN, GPIO_PULL_NONE);
	GPIO_OTYPER(GPIOB_BASE) &= ~(1UL << WL_REG_ON_PIN);
}

void
sdio_board_wl_reg_on(bool on)
{
	wl_reg_on_pin();
	GPIO_BSRR(GPIOB_BASE) = on ? (1UL << WL_REG_ON_PIN)
				   : (1UL << (WL_REG_ON_PIN + 16U));
	s_wl_reg_on = on;
}

bool
sdio_board_wl_reg_state(void)
{
	return s_wl_reg_on;
}

void
sdio_board_init(void)
{
	RCC_AHB1ENR |= RCC_AHB1ENR_GPIOB | RCC_AHB1ENR_GPIOC |
		       RCC_AHB1ENR_GPIOD | RCC_AHB1ENR_DMA2EN;
	RCC_APB2ENR |= RCC_APB2ENR_SDIOEN;
	(void)RCC_AHB1ENR;
	(void)RCC_APB2ENR;

	/* Rail off until the driver asks for it; the socket is shared with the
	 * module that occupied it before this line. */
	wl_reg_on_pin();
	GPIO_BSRR(GPIOB_BASE) = 1UL << (WL_REG_ON_PIN + 16U);
	s_wl_reg_on = false;

	sdio_pin(GPIOC_BASE, 8U);	/* D0 */
	sdio_pin(GPIOC_BASE, 9U);	/* D1 */
	sdio_pin(GPIOC_BASE, 10U);	/* D2 */
	sdio_pin(GPIOC_BASE, 11U);	/* D3 */
	sdio_pin(GPIOC_BASE, 12U);	/* CLK */
	sdio_pin(GPIOD_BASE, 2U);	/* CMD */

	/* Controller in a known state: clock off, data path off, all flags
	 * cleared, no interrupt source armed. */
	SDIO_POWER = 0U;
	SDIO_CLKCR = 0U;
	SDIO_DCTRL = 0U;
	SDIO_DTIMER = 0U;
	SDIO_DLEN = 0U;
	SDIO_ICR = 0xFFFFFFFFUL;
	SDIO_MASK = 0U;

	sdio_board_irq_enable(false);
	sdio_board_dma_irq_enable(false);
}

uint32_t
sdio_board_source_hz(void)
{
	/* PLL48CLK: the clock tree's 168 MHz PLL with Q = 7. */
	return 48000000UL;
}

void
sdio_board_irq_enable(bool enable)
{
	NVIC_ICPR(SDIO_IRQ / 32U) = 1UL << (SDIO_IRQ % 32U);
	NVIC_IPR_BYTE(SDIO_IRQ) = SDIO_IRQ_PRIO_BYTE;
	if (enable)
		NVIC_ISER(SDIO_IRQ / 32U) = 1UL << (SDIO_IRQ % 32U);
	else
		NVIC_ICER(SDIO_IRQ / 32U) = 1UL << (SDIO_IRQ % 32U);
}

void
sdio_board_dma_irq_enable(bool enable)
{
	NVIC_ICPR(SDIO_DMA_IRQ / 32U) = 1UL << (SDIO_DMA_IRQ % 32U);
	NVIC_IPR_BYTE(SDIO_DMA_IRQ) = SDIO_IRQ_PRIO_BYTE;
	if (enable)
		NVIC_ISER(SDIO_DMA_IRQ / 32U) = 1UL << (SDIO_DMA_IRQ % 32U);
	else
		NVIC_ICER(SDIO_DMA_IRQ / 32U) = 1UL << (SDIO_DMA_IRQ % 32U);
}