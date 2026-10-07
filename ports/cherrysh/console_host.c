/**
 * @file    console_host.c
 * @brief   Host console backend: stdout / stdin byte I/O for the shell port.
 * @author  zhugengyu
 * @date    07.10.2026
 * @design proxy - ref net80211:ports/net80211/osal/cmsis2/osal_cmsis2.c:20
 *         (one interface, the implementation picked per platform; the board
 *         backend is the sibling of this file)
 *
 * The backend implements the two byte callbacks the core needs and the exit
 * behaviour. Both host kernels drive their threads with signals (ThreadX
 * suspend/resume, the FreeRTOS POSIX tick), so a blocking read can return
 * early with EINTR - the loops retry on that, which is the whole reason they
 * are loops at all.
 */

#include <errno.h>
#include <unistd.h>

#include "os/cmsis2/cmsis2.h"
#include "csh.h"
#include "shell_port.h"

uint16_t console_sput( chry_readline_t * rl, const void * data, uint16_t size )
{
    const char * bytes = data;
    uint16_t written = 0U;

    ( void ) rl;

    while( written < size )
    {
        ssize_t n = write( STDOUT_FILENO, bytes + written, ( size_t ) ( size - written ) );

        if( n > 0 )
        {
            written = ( uint16_t ) ( written + ( uint16_t ) n );
        }
        else if( ( n < 0 ) && ( errno == EINTR ) )
        {
            continue;
        }
        else
        {
            break;
        }
    }

    return written;
}

uint16_t console_sget( chry_readline_t * rl, void * data, uint16_t size )
{
    ( void ) rl;

    for( ;; )
    {
        ssize_t n = read( STDIN_FILENO, data, ( size_t ) size );

        if( n > 0 )
        {
            return ( uint16_t ) n;
        }

        if( ( n < 0 ) && ( errno == EINTR ) )
        {
            continue;
        }

        /* End of input: no byte can ever arrive again. Park the task instead
         * of spinning - the suite's timeout is the honest verdict. */
        osDelay( 1U );
    }
}

void shell_port_backend_exit( void )
{
    /* Ends the run from this task: on the FreeRTOS host the scheduler start
     * call returns and main() exits with 0, on the ThreadX host the process
     * ends directly. Either way the console run is over. */
    cmsis2_host_end_scheduler();
}
