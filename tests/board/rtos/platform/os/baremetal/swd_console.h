/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   swd_console.h
 * @brief  Console mirror in RAM, readable over SWD when the serial path fails.
 *
 * The board's only console is the onboard CH340; when that link dies the
 * firmware can still be alive and talking, with nothing on the wire.  Every
 * character the polled console writes is also appended here, and a host with
 * the debug probe reads the head and the ring from target memory.  The ring is
 * single-writer, so the reader only needs the monotonically growing head.
 */
#ifndef FREEWIFICAM_SWD_CONSOLE_H
#define FREEWIFICAM_SWD_CONSOLE_H

#include <stdint.h>

#define SWD_CONSOLE_LOG2	13u
#define SWD_CONSOLE_SIZE	(1u << SWD_CONSOLE_LOG2)

struct swd_console_block {
	volatile uint32_t head;		/* total characters written */
	uint32_t size;			/* SWD_CONSOLE_SIZE */
	char data[SWD_CONSOLE_SIZE];	/* ring, head & (size - 1) */
};

extern struct swd_console_block g_swd_console;

void swd_console_init(void);
void swd_console_putc(char c);

#endif
