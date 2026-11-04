/*
 * @file   uart.h
 * @brief  Console UART face of the Zephyr backend (board interface).
 *
 * The board images drive UART2 themselves: the polled early path is what makes
 * the boot banner and every anchor appear before a kernel exists.  On Zephyr the
 * console is a driver, so this face forwards to printk() and there is exactly one
 * writer of the console register on the image.
 *
 * The line discipline still belongs to the caller: log.c assembles whole lines,
 * stamps them and ends them with '\n', which is what the console driver turns
 * into CR LF.  Nothing here adds a second newline.
 */

#ifndef FREEWIFICAM_UART_H
#define FREEWIFICAM_UART_H

#include <stdbool.h>
#include <stdint.h>

/* Nothing to program: the console driver owns the UART.  Kept because the
 * image's entry sequence calls it first, before anything may print. */
void uart_early_init(void);

/* Byte and string output.  A bare '\n' reaches the wire as CR LF because the
 * Zephyr console driver expands it. */
void uart_early_putc(char c);
void uart_early_puts(const char *s);

/* 8 lowercase hex digits, no prefix. */
void uart_early_put_hex32(uint32_t value);

/* Polled RX.  The console input path on Zephyr is the console driver's, so a
 * polled read reports "nothing waiting"; the shell is not part of this image. */
bool uart_early_try_getc(int *out);

/* Fault-path speaker: print, then park.  A fault that cannot speak is
 * indistinguishable from a dead board. */
void uart_early_panic(const char *message);

#endif /* FREEWIFICAM_UART_H */
