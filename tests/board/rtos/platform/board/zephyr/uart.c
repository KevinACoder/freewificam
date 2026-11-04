/*
 * @file   uart.c
 * @brief  Console UART face of the Zephyr backend, over printk().
 *
 * Why printk() and not the console driver's API: this face is called from the
 * fault path, from an ISR (CherryUSB's console sink is reachable from the
 * completion handler) and from threads, and printk() is the only Zephyr
 * primitive that is legal in all three.  The console driver underneath is the
 * board's UART2, configured by the devicetree overlay, so the bytes still come
 * out of the same pin.
 *
 * The one thing this file deliberately does NOT do is buffer: log.c already
 * assembles a line and hands it over whole, and a second buffer would only move
 * the interleaving problem one layer down.
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "uart.h"

void uart_early_init(void)
{
	/* The console driver is up before main() runs. */
}

void uart_early_putc(char c)
{
	printk("%c", c);
}

void uart_early_puts(const char *s)
{
	if (s == NULL) {
		return;
	}
	printk("%s", s);
}

void uart_early_put_hex32(uint32_t value)
{
	printk("%08x", value);
}

bool uart_early_try_getc(int *out)
{
	(void)out;
	return false;
}

void uart_early_panic(const char *message)
{
	printk("!!! %s\n", (message != NULL) ? message : "(null)");

	/* Park with interrupts masked: a fault handler that returns into the
	 * context it faulted in reports the same fault forever. */
	(void)irq_lock();
	for (;;) {
		;
	}
}
