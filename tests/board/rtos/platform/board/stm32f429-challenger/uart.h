/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * uart.h - STM32F429 console UART (USART1) early polled path.
 *
 * The API is the one the shared app layer expects (platform/board/rk3568-itx/
 * drivers/uart.h), so app/main.c and the runner compile unchanged.
 */

#ifndef FREEWIFICAM_UART_H
#define FREEWIFICAM_UART_H

#include <stdbool.h>
#include <stdint.h>

void uart_early_init(void);
void uart_early_putc(char c);
void uart_early_puts(const char *s);
void uart_early_put_hex32(uint32_t value);
bool uart_early_try_getc(int *out);
void uart_early_panic(const char *message);

#endif /* FREEWIFICAM_UART_H */
