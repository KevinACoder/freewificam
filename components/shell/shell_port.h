/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   shell_port.h
 * @brief  Board console port of the vendored CherrySH core.
 *
 * The core is byte-identical upstream (third-party/cherrysh); this port owns
 * the static instance, the UART callbacks and the repl task.  The command
 * table itself lives in shell_cmds.c and registers through FSymTab/VSymTab.
 */

#ifndef FREEWIFICAM_SHELL_PORT_H
#define FREEWIFICAM_SHELL_PORT_H

#include <stdint.h>

#include "csh.h"

/* Fill the static shell instance and wire the command/variable tables.
 * Returns 0 on success. */
int shell_port_init(void);

/* The repl loop; create it through rtos_shell_start(), not directly. */
void shell_port_repl_task(void *argument);

/* Start the shell task.  Returns 0 when the thread was created. */
int rtos_shell_start(void);

/* Console byte callbacks (shell_console.c). */
uint16_t console_sput(chry_readline_t *rl, const void *data, uint16_t size);
uint16_t console_sget(chry_readline_t *rl, void *data, uint16_t size);

/* The exit command's board behaviour: reset the session, keep the image. */
void shell_port_backend_exit(void);

/* Ask the repl loop to re-initialise the instance. */
void shell_port_session_reset(void);

#endif /* FREEWIFICAM_SHELL_PORT_H */
