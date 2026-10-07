/**
 * @file    console_board.c
 * @brief   Console backend: the board's polled UART2 console.
 * @author  zhugengyu
 * @date    07.10.2026
 * @design adapter - the shell's sput/sget callbacks meet the board's polled
 *         UART seam; ref ports/cherrysh/console_host.c (the same port's other
 *         backend) and board/rk3568-itx/drivers/uart.h (the seam).
 *
 * The console is the polled UART2 path: no interrupts, IER stays 0. The
 * blocking read the shell's configuration expects (CONFIG_CSH_NOBLOCK=0) is
 * built here by parking the task between polls, so the shell task yields
 * while idle instead of spinning - the console backends are the only files in
 * this port that touch platform I/O.
 *
 * Newline contract: the shell's CONFIG_CSH_NEWLINE is "\r\n" (the same text
 * the host backend writes verbatim), while the UART seam expands a bare '\n'
 * into CR LF on the wire. A carriage return that immediately precedes a line
 * feed is therefore dropped here - otherwise every shell line would reach the
 * wire as CR CR LF.
 */

#include <stdint.h>

#include "os/cmsis2/cmsis2.h"
#include "csh.h"
#include "shell_port.h"
#include "uart.h"

uint16_t console_sput( chry_readline_t * rl, const void * data, uint16_t size )
{
    const char *p = (const char *)data;
    uint16_t i;

    ( void ) rl;

    if( data == 0 || size == 0U )
    {
        return 0U;
    }

    for( i = 0U; i < size; i++ )
    {
        /* Drop a CR that precedes an LF in the same call: the seam turns the
         * LF into CR LF. The core emits the pair inside one call (the newline
         * is one string), so the look-ahead covers every case that occurs. */
        if( p[i] == '\r' && ( i + 1U ) < size && p[i + 1U] == '\n' )
        {
            continue;
        }
        uart_early_putc( p[i] );
    }

    return size;
}

uint16_t console_sget( chry_readline_t * rl, void * data, uint16_t size )
{
    int c;

    ( void ) rl;

    if( data == 0 || size == 0U )
    {
        return 0U;
    }

    /* Block until a byte is available, as the blocking configuration
     * requires. The blocking is a park, not a spin: the task sleeps between
     * polls, so lower-priority tasks keep running while the shell waits. */
    for( ;; )
    {
        if( uart_early_try_getc( &c ) )
        {
            *(uint8_t *)data = (uint8_t)c;
            return 1U;
        }
        osDelay( 1U );
    }
}

void shell_port_backend_exit( void )
{
    /* Board semantics: the session resets and the prompt returns - there is
     * no "end the run" for an image that has to keep running. The reset
     * itself happens in the repl loop, outside this call (the exit command
     * runs inside the core's stack). */
    shell_port_session_reset();
}
