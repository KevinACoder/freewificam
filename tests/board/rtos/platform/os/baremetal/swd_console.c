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
