/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * sdio_board.c - the F429 SDIO socket bring-up.
 *
 * Pins follow the vendor SD example (bsp_sdio_sd.c) and the board schematic:
 * PC8-PC11 D0-D3, PC12 CLK, PD2 CMD, AF12, internal pull-ups.  The same
 * socket carries the 8189FTV module (the WiFi line) or a plain SD memory card
 * (the SD card line).  The socket has no card-detect pin; the host glue
 * reports the card as present and the command path is the detector.  PB13 is
 * the AP6181's WIFI_EN and is held low for both lines.
 */

#include "sdio_board.h"

#include "stm32f4xx.h"

#define SDIO_AF			12U
#define SDIO_GPIO_PULL_UP	1U
#define SDIO_GPIO_VERY_HIGH	3U

static void
gpio_mode(GPIO_TypeDef *gpio, uint32_t pin, uint32_t mode)
{
	gpio->MODER = (gpio->MODER & ~(3UL << (pin * 2U))) |
		      (mode << (pin * 2U));
}

static void
gpio_speed(GPIO_TypeDef *gpio, uint32_t pin, uint32_t speed)
{
	gpio->OSPEEDR = (gpio->OSPEEDR & ~(3UL << (pin * 2U))) |
			(speed << (pin * 2U));
}

static void
gpio_pull(GPIO_TypeDef *gpio, uint32_t pin, uint32_t pull)
{
	gpio->PUPDR = (gpio->PUPDR & ~(3UL << (pin * 2U))) |
		      (pull << (pin * 2U));
}

static void
gpio_af(GPIO_TypeDef *gpio, uint32_t pin, uint32_t af)
{
	uint32_t shift = (pin & 7U) * 4U;

	gpio->AFR[pin >> 3U] = (gpio->AFR[pin >> 3U] &
				~(0xFUL << shift)) | (af << shift);
}

static void
sdio_pin(GPIO_TypeDef *gpio, uint32_t pin)
{
	gpio_mode(gpio, pin, 2U);	/* alternate function */
	gpio_speed(gpio, pin, SDIO_GPIO_VERY_HIGH);
	gpio_pull(gpio, pin, SDIO_GPIO_PULL_UP);
	gpio_af(gpio, pin, SDIO_AF);
}

void
sdio_board_wl_reg_on(bool on)
{
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
	(void)RCC->AHB1ENR;

	gpio_mode(GPIOB, 13U, 1U);	/* output */
	gpio_speed(GPIOB, 13U, 2U);
	gpio_pull(GPIOB, 13U, 0U);
	GPIOB->OTYPER &= ~(1UL << 13U);
	GPIOB->BSRR = on ? (1UL << 13U) : (1UL << (13U + 16U));
}

void
sdio_board_init(void)
{
	/* Hold the AP6181 off before the shared bus is touched. */
	sdio_board_wl_reg_on(false);

	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIODEN;
	RCC->APB2ENR |= RCC_APB2ENR_SDIOEN;
	(void)RCC->AHB1ENR;
	(void)RCC->APB2ENR;

	sdio_pin(GPIOC, 8U);
	sdio_pin(GPIOC, 9U);
	sdio_pin(GPIOC, 10U);
	sdio_pin(GPIOC, 11U);
	sdio_pin(GPIOC, 12U);
	sdio_pin(GPIOD, 2U);

	/* Controller in a known state: clock off, data path off, all flags
	 * cleared.  The host glue programs the clock and power through the ST
	 * LL driver. */
	SDIO->POWER = 0U;
	SDIO->CLKCR = 0U;
	SDIO->DCTRL = 0U;
	SDIO->DTIMER = 0U;
	SDIO->DLEN = 0U;
	SDIO->ICR = 0xFFFFFFFFUL;

	NVIC_DisableIRQ(SDIO_IRQn);
	NVIC_ClearPendingIRQ(SDIO_IRQn);
	NVIC_SetPriority(SDIO_IRQn, 6U);
}

uint32_t
sdio_board_source_hz(void)
{
	return 48000000U;
}

void
sdio_board_irq_enable(bool enable)
{
	if (enable) {
		NVIC_ClearPendingIRQ(SDIO_IRQn);
		NVIC_EnableIRQ(SDIO_IRQn);
	} else {
		NVIC_DisableIRQ(SDIO_IRQn);
	}
}
