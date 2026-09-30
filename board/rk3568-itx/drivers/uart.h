/*
 * @file   uart.h
 * @brief  Console UART2 early polled path (board interface).
 */

#ifndef FREEWEBCAM_UART_H
#define FREEWEBCAM_UART_H

/* Fully re-program the UART for polled 115200 8N1. MUST run before the first
 * polled write: a UART inherited from the loader has unreliable THRE
 * semantics (short bursts inside the FIFO get out, then bytes drop or the
 * poll wedges). */
void uart_early_init(void);

/* Polled byte output; unbounded THRE wait (a bounded timeout is actively
 * harmful here - see uart.c); expands a bare '\n' to CR LF on the wire. */
void uart_early_putc(char c);
void uart_early_puts(const char *s);

/* 8 lowercase hex digits, no prefix. */
void uart_early_put_hex32(uint32_t value);

/* Fault-path speaker: re-programs the UART UNCONDITIONALLY, then prints the
 * message. A fault handler that cannot speak is indistinguishable from a
 * dead board, and a fault may fire before any init. */
void uart_early_panic(const char *message);

#endif /* FREEWEBCAM_UART_H */
