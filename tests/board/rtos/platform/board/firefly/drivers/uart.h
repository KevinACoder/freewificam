/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   uart.h
 * @brief  Console UART (PL011) early polled path (firefly family interface).
 */

#ifndef FREEWIFICAM_UART_H
#define FREEWIFICAM_UART_H

#include <stdbool.h>
#include <stdint.h>

/* Fully re-program the UART for polled 115200 8N1. MUST run before the first
 * polled write: a UART inherited from the loader has unreliable FIFO flag
 * semantics. */
void uart_early_init(void);

/* Polled byte output; unbounded FIFO-room wait; expands a bare '\n' to CR LF
 * on the wire. */
void uart_early_putc(char c);
void uart_early_puts(const char *s);

/* 8 lowercase hex digits, no prefix. */
void uart_early_put_hex32(uint32_t value);

/* Polled RX: 1 with *out filled when a byte is waiting, 0 otherwise. The
 * console shell builds its blocking read on this by parking its task between
 * polls; nothing here waits. */
bool uart_early_try_getc(int *out);

/* Fault-path speaker: re-programs the UART UNCONDITIONALLY, then prints the
 * message. A fault handler that cannot speak is indistinguishable from a
 * dead board, and a fault may fire before any init. */
void uart_early_panic(const char *message);

#endif /* FREEWIFICAM_UART_H */
