/*
 * @file   uart.h
 * @brief  Host spelling: the wire the console outlet writes to.
 *
 * The outlet suite captures the bytes instead of driving a UART, so the line
 * assembly, the stamp and the repeat gate are judged on exactly what the board
 * would put on the wire. The board's header is
 * board/rk3568-itx/drivers/uart.h.
 */

#ifndef ULOG_LEVEL_TEST_UART_H
#define ULOG_LEVEL_TEST_UART_H

void uart_early_putc(char c);
void uart_early_puts(const char *s);

#endif /* ULOG_LEVEL_TEST_UART_H */
