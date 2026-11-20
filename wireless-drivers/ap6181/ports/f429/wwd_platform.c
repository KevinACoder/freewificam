/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * wwd_platform.c - the WWD platform face: rail, reset, OOB line, MAC, context.
 *
 * Board facts come from sdio_board.c.  The vendor platform this port replaces
 * powered the module through two GPIO abstractions (reset and power pins); the
 * Challenger V2 has one rail, WL_REG_ON on PB13, so reset and power drive the
 * same pin and "power" is a no-op.  The vendor sequence is kept exactly:
 * assert reset, power on, delay, release reset, delay, enumerate -- which on
 * this board means rail off, rail on, 2 ms settle.
 *
 * The out-of-band interrupt is the module's line the vendor platform wired to
 * PA0 (WICED_WIFI_OOB_IRQ_GPIO_PIN = 0); it only exists when MCU powersave is
 * compiled in.  The WICED_AP6181_NO_OOB arm defines WICED_DISABLE_MCU_POWERSAVE
 * for the whole driver world, which compiles this arm and the core's use of it
 * out together.
 */

#include <stdint.h>
#include <string.h>

#include "wwd_constants.h"
#include "wwd_debug.h"
#include "network/wwd_buffer_interface.h"
#include "platform/wwd_platform_interface.h"
#include "internal/wwd_thread.h"

#include "wiced_port.h"
#include "wiced_port_config.h"

#include "sdio_board.h"

extern uint32_t SystemCoreClock;

/* --- EXTI0 (the module's OOB line) --------------------------------------- */

#define SYSCFG_EXTICR1		(*(volatile uint32_t *)0x40013808UL)
#define EXTI_IMR		(*(volatile uint32_t *)0x40013C00UL)
#define EXTI_RTSR		(*(volatile uint32_t *)0x40013C08UL)
#define EXTI_FTSR		(*(volatile uint32_t *)0x40013C0CUL)
#define EXTI_PR			(*(volatile uint32_t *)0x40013C14UL)

#define NVIC_ISER(n)		(*(volatile uint32_t *)(0xE000E100UL + 4UL * (n)))
#define NVIC_ICER(n)		(*(volatile uint32_t *)(0xE000E180UL + 4UL * (n)))
#define NVIC_ICPR(n)		(*(volatile uint32_t *)(0xE000E280UL + 4UL * (n)))
#define NVIC_IPR_BYTE(irq)	(*(volatile uint8_t *)(0xE000E400UL + (irq)))

#define EXTI0_IRQ		6U
#define OOB_IRQ_PRIORITY	6U

#define GPIOA_BASE		0x40020000UL
#define GPIOA_MODER		(*(volatile uint32_t *)GPIOA_BASE)
#define GPIOA_PUPDR		(*(volatile uint32_t *)(GPIOA_BASE + 0x0CUL))
#define RCC_AHB1ENR		(*(volatile uint32_t *)0x40023830UL)
#define RCC_AHB1ENR_GPIOA	(1UL << 0)
#define RCC_APB2ENR		(*(volatile uint32_t *)0x40023844UL)
#define RCC_APB2ENR_SYSCFGEN	(1UL << 14)

static inline uint32_t
read_ipsr(void)
{
	uint32_t ipsr;

	__asm__ __volatile__("mrs %0, ipsr" : "=r"(ipsr));
	return ipsr;
}

static void
settle_ms(uint32_t ms)
{
	/* A busy wait is right here: this runs before the scheduler's first
	 * thread that could block, inside the driver's power-on sequence. */
	volatile uint32_t per_ms = (SystemCoreClock / 1000UL) / 4UL;

	while (ms-- != 0U) {
		volatile uint32_t n = per_ms;

		while (n-- != 0U)
			__asm__ __volatile__("nop");
	}
}

wwd_result_t
host_platform_init(void)
{
	sdio_board_init();
	host_platform_reset_wifi(WICED_TRUE);
	return WWD_SUCCESS;
}

wwd_result_t
host_platform_deinit(void)
{
	host_platform_reset_wifi(WICED_TRUE);
	return WWD_SUCCESS;
}

void
host_platform_reset_wifi(wiced_bool_t reset_asserted)
{
	sdio_board_wl_reg_on(reset_asserted == WICED_TRUE ? false : true);
	/* The module's own boot window after WL_REG_ON rises: the vendor
	 * platform leaves 2 ms, but enumeration at 400 kHz is marginal with
	 * that (the first CMD3 answers 1-2 ms late, then the controller's
	 * budget expires).  20 ms is far inside the module's spec and costs
	 * nothing on a bench. */
	if (reset_asserted != WICED_TRUE)
		settle_ms(20U);
}

void
host_platform_power_wifi(wiced_bool_t power_enabled)
{
	/* One rail, driven by reset; nothing separate to power. */
	(void)power_enabled;
}

wwd_result_t
host_platform_get_mac_address(wiced_mac_t *mac)
{
	/* No DCT and no bootloader on this image: the chip's factory MAC is what
	 * the driver reads from the device, and MAC_ADDRESS_SET_BY_HOST is off. */
	if (mac != NULL)
		memset(mac->octet, 0, sizeof(mac->octet));
	return WWD_SUCCESS;
}

wwd_result_t
host_platform_init_wlan_powersave_clock(void)
{
	/* No 32 kHz clock path on this board (WICED_USE_WIFI_32K_CLOCK_MCO off). */
	return WWD_SUCCESS;
}

wwd_result_t
host_platform_deinit_wlan_powersave_clock(void)
{
	return WWD_SUCCESS;
}

wiced_bool_t
host_platform_is_in_interrupt_context(void)
{
	return (read_ipsr() != 0U) ? WICED_TRUE : WICED_FALSE;
}

#ifndef WICED_DISABLE_MCU_POWERSAVE

uint8_t
host_platform_get_oob_interrupt_pin(void)
{
	return 0U;
}

wwd_result_t
host_enable_oob_interrupt(void)
{
	RCC_AHB1ENR |= RCC_AHB1ENR_GPIOA;
	RCC_APB2ENR |= RCC_APB2ENR_SYSCFGEN;
	(void)RCC_AHB1ENR;
	(void)RCC_APB2ENR;

	/* PA0 input, no pull; the module drives it. */
	GPIOA_MODER &= ~3UL;
	GPIOA_PUPDR &= ~3UL;

	/* EXTI line 0 on port A, rising edge, unmasked. */
	SYSCFG_EXTICR1 &= ~0xFUL;
	EXTI_RTSR |= 1UL;
	EXTI_FTSR &= ~1UL;
	EXTI_PR = 1UL;
	EXTI_IMR |= 1UL;

	NVIC_ICPR(EXTI0_IRQ / 32U) = 1UL << (EXTI0_IRQ % 32U);
	NVIC_IPR_BYTE(EXTI0_IRQ) = (OOB_IRQ_PRIORITY << 4);
	NVIC_ISER(EXTI0_IRQ / 32U) = 1UL << (EXTI0_IRQ % 32U);
	return WWD_SUCCESS;
}

void
EXTI0_IRQHandler(void)
{
	if ((EXTI_PR & 1UL) != 0UL) {
		EXTI_PR = 1UL;
		ap6181_stats.oob_intrs++;
		wwd_thread_notify_irq();
	}
}

#endif /* !WICED_DISABLE_MCU_POWERSAVE */

void
host_platform_bus_buffer_freed(wwd_buffer_dir_t direction)
{
	/* The SDIO bus is credit-flow-controlled by SDPCM, not by host buffers. */
	(void)direction;
}