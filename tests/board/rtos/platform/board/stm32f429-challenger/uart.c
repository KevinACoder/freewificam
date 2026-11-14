/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * uart.c - STM32F429 USART1 polled console.
 *
 * USART1 is on PA9 (TX) / PA10 (RX), AF7, APB2.  The board wires those pins to
 * the onboard CH340 USB-TTL, which is the same port the lab opens as a serial
 * console.  Polled only: no interrupt, no DMA, no CMSIS driver.
 */

#include <stdbool.h>
#include <stdint.h>

#include "board_conf.h"
#include "uart.h"

#include "board.h"

#define RCC_BASE	0x40023800UL
#define RCC_AHB1ENR	(*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB2ENR	(*(volatile uint32_t *)(RCC_BASE + 0x44))

#define GPIOA_BASE	0x40020000UL
#define GPIOA_MODER	(*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_OSPEEDR	(*(volatile uint32_t *)(GPIOA_BASE + 0x08))
#define GPIOA_AFR	((volatile uint32_t *)(GPIOA_BASE + 0x20))

#define USART1_BASE	BOARD_UART_BASE
#define USART1_SR	(*(volatile uint32_t *)(USART1_BASE + 0x00))
#define USART1_DR	(*(volatile uint32_t *)(USART1_BASE + 0x04))
#define USART1_BRR	(*(volatile uint32_t *)(USART1_BASE + 0x08))
#define USART1_CR1	(*(volatile uint32_t *)(USART1_BASE + 0x0C))

#define USART_SR_TXE	(1UL << 7)
#define USART_SR_RXNE	(1UL << 5)
#define USART_CR1_UE	(1UL << 13)
#define USART_CR1_TE	(1UL << 3)
#define USART_CR1_RE	(1UL << 2)

static void uart_program(void)
{
	RCC_AHB1ENR |= (1UL << 0);
	RCC_APB2ENR |= (1UL << 4);

	GPIOA_MODER &= ~((3UL << 18) | (3UL << 20));
	GPIOA_MODER |= ((2UL << 18) | (2UL << 20));
	GPIOA_OSPEEDR |= ((3UL << 18) | (3UL << 20));
	GPIOA_AFR[1] &= ~((0xFUL << 4) | (0xFUL << 8));
	GPIOA_AFR[1] |= ((7UL << 4) | (7UL << 8));

	uint32_t pclk2 = (SystemCoreClock > 16000000UL) ? (SystemCoreClock / 2UL) : 16000000UL;
	USART1_BRR = (pclk2 + BOARD_UART_BAUD / 2UL) / BOARD_UART_BAUD;
	USART1_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void uart_early_init(void)
{
	uart_program();
}

void uart_early_putc(char c)
{
	if (c == '\n') {
		uart_early_putc('\r');
	}
	uint32_t guard = 1000000UL;
	while ((USART1_SR & USART_SR_TXE) == 0 && guard-- != 0) {
	}
	USART1_DR = (uint8_t)c;
}

void uart_early_puts(const char *s)
{
	while (*s) {
		uart_early_putc(*s++);
	}
}

void uart_early_put_hex32(uint32_t value)
{
	static const char hex[] = "0123456789abcdef";
	for (int shift = 28; shift >= 0; shift -= 4) {
		uart_early_putc(hex[(value >> shift) & 0xF]);
	}
}

bool uart_early_try_getc(int *out)
{
	if ((USART1_SR & USART_SR_RXNE) == 0) {
		return false;
	}
	if (out) {
		*out = (int)(USART1_DR & 0xFF);
	}
	return true;
}

void uart_early_panic(const char *message)
{
	uart_program();
	uart_early_puts(message);
}
