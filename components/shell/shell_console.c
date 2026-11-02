/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   shell_console.c
 * @brief  CherrySH console backend: the board's polled UART2 console.
 *
 * The core's sput/sget pair is the whole byte-level porting face.  sput goes
 * through board_console_write_raw() so prompt bytes and ULOG lines share one
 * print lock; sget parks the shell task between polls of the polled RX
 * primitive, which is the blocking read the core's configuration expects.
 */

#include <stdint.h>

#include "os/cmsis2/cmsis2.h"

#include "csh.h"
#include "shell_port.h"

#include "log.h"
#include "uart.h"

uint16_t console_sput(chry_readline_t *rl, const void *data, uint16_t size)
{
	const char *p = (const char *)data;
	char out[128];
	unsigned int n = 0u;
	uint16_t i;

	(void)rl;

	if (data == NULL || size == 0u) {
		return 0u;
	}

	for (i = 0u; i < size; i++) {
		/* Drop a CR immediately before an LF: the raw console writer
		 * expands the LF to CR LF, and the core emits the pair as one
		 * string. */
		if (p[i] == '\r' && (i + 1u) < size && p[i + 1u] == '\n') {
			continue;
		}
		out[n++] = p[i];
		if (n == sizeof(out)) {
			board_console_write_raw(out, n);
			n = 0u;
		}
	}
	if (n != 0u) {
		board_console_write_raw(out, n);
	}
	return size;
}

uint16_t console_sget(chry_readline_t *rl, void *data, uint16_t size)
{
	int c;

	(void)rl;

	if (data == NULL || size == 0u) {
		return 0u;
	}

	/* Blocking configuration: park, do not spin. */
	for (;;) {
		if (uart_early_try_getc(&c)) {
			*(uint8_t *)data = (uint8_t)c;
			return 1u;
		}
		(void)osDelay(1u);
	}
}

/* The exit command runs inside the repl task; it must not tear the instance
 * down under the caller, so it only asks the repl loop to re-initialise the
 * session on its next iteration. */
void shell_port_backend_exit(void)
{
	shell_port_session_reset();
}
