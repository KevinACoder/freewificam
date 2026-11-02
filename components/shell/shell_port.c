/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   shell_port.c
 * @brief  CherrySH instance wiring, the board repl task and the exit hook.
 *
 * Port shape follows the 0.1.0 reference (ports/cherrysh/shell_port.c) and
 * the upstream bare-metal sample: one static instance, a blocking console
 * backend, one repl task.  The command bodies are in shell_cmds.c; the byte
 * I/O is in shell_console.c.
 */

#include "csh.h"
#include "shell_port.h"
#include "shell_cmds.h"

#include "os/cmsis2/cmsis2.h"

#define SHELL_PROMPT_BUFFER_SIZE 128u
#define SHELL_HISTORY_BUFFER_SIZE 128u
#define SHELL_LINE_BUFFER_SIZE 256u

#ifndef SHELL_TASK_STACK
#define SHELL_TASK_STACK 16384u
#endif

#ifndef SHELL_PORT_HOST
#define SHELL_PORT_HOST "rk3568"
#endif

static chry_shell_t csh;

static char csh_prompt_buffer[SHELL_PROMPT_BUFFER_SIZE];
static char csh_history_buffer[SHELL_HISTORY_BUFFER_SIZE];
static char csh_line_buffer[SHELL_LINE_BUFFER_SIZE];

static volatile int session_reset_requested;

extern const chry_syscall_t __fsymtab_start[];
extern const chry_syscall_t __fsymtab_end[];
extern const chry_sysvar_t __vsymtab_start[];
extern const chry_sysvar_t __vsymtab_end[];

int shell_port_init(void)
{
	chry_shell_init_t init = { 0 };

	init.sput = console_sput;
	init.sget = console_sget;

	init.command_table_beg = __fsymtab_start;
	init.command_table_end = __fsymtab_end;
	init.variable_table_beg = __vsymtab_start;
	init.variable_table_end = __vsymtab_end;

	init.prompt_buffer = csh_prompt_buffer;
	init.prompt_buffer_size = sizeof(csh_prompt_buffer);
	init.history_buffer = csh_history_buffer;
	init.history_buffer_size = sizeof(csh_history_buffer);
	init.line_buffer = csh_line_buffer;
	init.line_buffer_size = sizeof(csh_line_buffer);

	init.uid = 0u;
	init.host = SHELL_PORT_HOST;
	init.user[0] = "user";
	init.hash[0] = "";

	return chry_shell_init(&csh, &init);
}

void shell_port_session_reset(void)
{
	session_reset_requested = 1;
}

void shell_port_repl_task(void *argument)
{
	(void)argument;

	for (;;) {
		/* The reset must run outside chry_shell_task_repl's stack:
		 * the exit command executes there and re-initialising the
		 * instance inside it would pull the ground out from under the
		 * call. */
		if (session_reset_requested) {
			session_reset_requested = 0;
			(void)shell_port_init();
		}

		chry_shell_task_exec(&csh);
		(void)chry_shell_task_repl(&csh);
	}
}

static int csh_exit(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	shell_port_backend_exit();
	return 0;
}

CSH_SCMD_EXPORT_ALIAS_FULL(csh_exit, exit,
	"leave the shell",
	"exit\r\n"
	"    - reset the console session\r\n");

/* The command lookup walks the environment PATH for every non-absolute name,
 * so this variable is a hard dependency of command execution. */
#define __ENV_PATH "/sbin:/bin"
const char ENV_PATH[] = __ENV_PATH;
CSH_RVAR_EXPORT(ENV_PATH, PATH, sizeof(__ENV_PATH));

#define __ENV_ZERO ""
const char ENV_ZERO[] = __ENV_ZERO;
CSH_RVAR_EXPORT(ENV_ZERO, ZERO, sizeof(__ENV_ZERO));

static void shell_task(void *argument)
{
	(void)shell_port_init();
	shell_autorun();
	shell_port_repl_task(argument);
}

static const osThreadAttr_t shell_attr = {
	.name = "shell",
	.priority = osPriorityNormal,
	.stack_size = SHELL_TASK_STACK,
};

int rtos_shell_start(void)
{
	return (osThreadNew(shell_task, NULL, &shell_attr) != NULL) ? 0 : -1;
}
