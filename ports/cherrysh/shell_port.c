/**
 * @file    shell_port.c
 * @brief   CherrySH port for this project: instance wiring and the repl task.
 * @author  zhugengyu
 * @date    07.10.2026
 * @design adapter - ref cherrysh:README.md:59 (the sput/sget callbacks are the
 *         whole porting face); template method - ref
 *         cherrysh:samples/hpm/barebone_uart/src/shell.c:44 (init shape) and
 *         cherrysh:samples/hpm/barebone_uart/src/shell.c:135 (repl task shape)
 *
 * Everything around the upstream core lives here: the static instance, the
 * buffer wiring and the repl loop. The console byte I/O and the exit
 * behaviour are the console backend's (console_host.c on the host, the board
 * backend later) - this file stays platform calls free apart from the CMSIS
 * facade's headers, so the same shell runs on every backend.
 */

#include "csh.h"
#include "shell_port.h"

#define SHELL_PROMPT_BUFFER_SIZE 128U
#define SHELL_HISTORY_BUFFER_SIZE 128U
#define SHELL_LINE_BUFFER_SIZE 256U

/* The prompt's host field; the board port uses its own constant. */
#ifndef SHELL_PORT_HOST
#define SHELL_PORT_HOST "freewebcam"
#endif

static chry_shell_t csh;

static char csh_prompt_buffer[SHELL_PROMPT_BUFFER_SIZE];
static char csh_history_buffer[SHELL_HISTORY_BUFFER_SIZE];
static char csh_line_buffer[SHELL_LINE_BUFFER_SIZE];

/* Command and variable tables: the linker symbols of the FSymTab / VSymTab
 * sections (the boundary snippet lives in the build: symtab.ld on the host,
 * the board linker script on the board). */
extern const chry_syscall_t __fsymtab_start[];
extern const chry_syscall_t __fsymtab_end[];
extern const chry_sysvar_t __vsymtab_start[];
extern const chry_sysvar_t __vsymtab_end[];

int shell_port_init( void )
{
    chry_shell_init_t init = { 0 };

    init.sput = console_sput;
    init.sget = console_sget;

    init.command_table_beg = __fsymtab_start;
    init.command_table_end = __fsymtab_end;
    init.variable_table_beg = __vsymtab_start;
    init.variable_table_end = __vsymtab_end;

    init.prompt_buffer = csh_prompt_buffer;
    init.prompt_buffer_size = sizeof( csh_prompt_buffer );
    init.history_buffer = csh_history_buffer;
    init.history_buffer_size = sizeof( csh_history_buffer );
    init.line_buffer = csh_line_buffer;
    init.line_buffer_size = sizeof( csh_line_buffer );

    init.uid = 0;
    init.host = SHELL_PORT_HOST;
    init.user[0] = "user";
    /* No login on this console; the field must still be a valid string. */
    init.hash[0] = "";

    return chry_shell_init( &csh, &init );
}

void shell_port_repl_task( void * argument )
{
    ( void ) argument;

    for( ;; )
    {
        chry_shell_task_exec( &csh );

        /* One call reads one line and runs it: 0 after a line, 1 for
         * "continue", -1 for a terminal read failure. Every outcome loops
         * back and the next read prints the prompt again. */
        ( void ) chry_shell_task_repl( &csh );
    }
}

/* The exit command ends the console session; what that means is the console
 * backend's call (the host ends the run through the CMSIS host seam). */
static int csh_exit( int argc, char ** argv )
{
    ( void ) argc;
    ( void ) argv;

    shell_port_backend_exit();

    return 0;
}

CSH_SCMD_EXPORT_ALIAS_FULL( csh_exit, exit,
    "leave the shell",
    "exit\r\n"
    "    - leave the shell\r\n" );

/* The command lookup walks the environment PATH for every non-absolute name,
 * so this variable is a hard dependency of command execution. */
#define __ENV_PATH "/sbin:/bin"
const char ENV_PATH[] = __ENV_PATH;
CSH_RVAR_EXPORT( ENV_PATH, PATH, sizeof( __ENV_PATH ) );

#define __ENV_ZERO ""
const char ENV_ZERO[] = __ENV_ZERO;
CSH_RVAR_EXPORT( ENV_ZERO, ZERO, sizeof( __ENV_ZERO ) );
