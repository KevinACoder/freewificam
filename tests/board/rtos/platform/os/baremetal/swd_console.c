/* SPDX-License-Identifier: BSD-3-Clause */
#include "swd_console.h"

__attribute__((section(".swd_console"), used, aligned(8)))
struct swd_console_block g_swd_console;

void swd_console_init(void)
{
	g_swd_console.head = 0u;
	g_swd_console.size = SWD_CONSOLE_SIZE;
	g_swd_console.boot++;
}

void swd_console_putc(char c)
{
	uint32_t head = g_swd_console.head;

	if (g_swd_console.size == 0u)
		g_swd_console.size = SWD_CONSOLE_SIZE;
	g_swd_console.data[head & (SWD_CONSOLE_SIZE - 1u)] = c;
	g_swd_console.head = head + 1u;
}

/*
 * Panic path.  The fault handlers must not depend on anything that needs
 * initialisation - not the UART, not the console lock, not the allocator - so
 * they append straight into the ring.  The ring lives at a fixed address and
 * its size field is re-established here, which makes a crash after a failed
 * boot readable over SWD even when the serial link is dead.
 */
void swd_console_panic_puts(const char *s)
{
	if (g_swd_console.size == 0u)
		g_swd_console.size = SWD_CONSOLE_SIZE;
	while (*s != '\0')
		swd_console_putc(*s++);
}

void swd_console_panic_hex(uint32_t value)
{
	static const char digits[] = "0123456789abcdef";
	int shift;

	swd_console_panic_puts("0x");
	for (shift = 28; shift >= 0; shift -= 4)
		swd_console_putc(digits[(value >> shift) & 0xfu]);
}
