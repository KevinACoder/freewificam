/**
 * @file    shell_port.h
 * @brief   Port entry points of the CherrySH console shell.
 * @author  zhugengyu
 * @date    07.10.2026
 * @design adapter - ref cherrysh:README.md:59 (sput/sget is the whole porting
 *         face); template method - ref cherrysh:samples/hpm/barebone_uart/src/shell.c:135
 *         (the repl task shell repeats the core's exec/repl calls)
 *
 * The port owns everything around the upstream core: the configuration header
 * (csh_config.h), the instance wiring and the repl task, and one console
 * backend per platform. Kernel API use is CMSIS-RTOS2 only; the console
 * backends are the only files that touch platform I/O.
 */

#ifndef SHELL_PORT_H
#define SHELL_PORT_H

#include <stdint.h>

#include "csh.h"

/**
 * @brief Fill the shell instance from the port's static state.
 *
 * Call once, before creating the repl task.
 * @retval 0 on success, -1 when the core rejects the configuration.
 */
int shell_port_init( void );

/**
 * @brief The repl loop; create it as a CMSIS-RTOS2 thread.
 */
void shell_port_repl_task( void * argument );

/**
 * @brief Console backend: write bytes to the console output.
 */
uint16_t console_sput( chry_readline_t * rl, const void * data, uint16_t size );

/**
 * @brief Console backend: block until at least one byte of input arrives.
 */
uint16_t console_sget( chry_readline_t * rl, void * data, uint16_t size );

/**
 * @brief Console backend: the exit command's platform behaviour.
 *
 * The host backend ends the run through the CMSIS-RTOS2 host seam; the board
 * backend resets the session instead (the shell comes back with a fresh
 * prompt, the image keeps running).
 */
void shell_port_backend_exit( void );

/**
 * @brief Ask the repl loop to reset the shell session.
 *
 * For backends whose exit means "start over" rather than "end the run": the
 * flag is consumed by the repl loop, which re-initialises the instance and
 * prints the prompt again.
 */
void shell_port_session_reset( void );

#endif /* SHELL_PORT_H */
