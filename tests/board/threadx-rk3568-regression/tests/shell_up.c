/**
 * @file    shell_up.c
 * @brief   Case shell-up: bring the console shell up (the interaction face).
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  case body of the regression table (tests/board/common/board_suite.h);
 *          the check is the shell image's own fatal path turned into a verdict
 *          (tests/board/cherrysh-threadx-rk3568-smoke/main.c:70-76).
 *
 * This case runs first: the host runner sends the interaction steps right after
 * the start line, so the REPL has to be live by then.
 */

#include "board_suite.h"

#include "os/cmsis2/cmsis2.h"
#include "ports/cherrysh/shell_port.h"

static const osThreadAttr_t shell_thread_attr = {
	.name = "shell",
	.priority = osPriorityNormal,
	.stack_size = 4096U,
};

static void shell_up( void )
{
	if( shell_port_init() != 0 ) {
		board_case_fail( "shell_port_init failed" );
		return;
	}
	if( osThreadNew( shell_port_repl_task, 0, &shell_thread_attr ) == 0 ) {
		board_case_fail( "repl thread create failed" );
	}
}

BOARD_TEST( "shell-up", shell_up );
