/**
 * @file    cmsis2_host.c
 * @brief   Host side of the CMSIS-RTOS2 backend: lifecycle end and the two seams.
 * @author  zhugengyu
 * @date    06.10.2026

 * @design proxy - ref linux:include/linux/dma-map-ops.h:16
 *
 * CMSIS-RTOS2 has no kernel-stop call, and this kernel has no way back out of its
 * scheduler: the Linux port's scheduler is the process main thread in an endless loop,
 * so a host run can only end by ending the process. Flushing first keeps the output of
 * the run complete, and exit(0) runs the atexit handlers and tears down the remaining
 * pthreads. The FreeRTOS backend reaches the same contract by ending its scheduler and
 * returning from the kernel start call; callers see one function either way.
 *
 * The two support seams declared in cmsis2_support.h are implemented here on the host
 * side: diagnostics keep going to stderr (stdout is the smokes' clean result channel,
 * and the bytes are unchanged from before the seam existed) and the kernel entry stays
 * the port's own tx_kernel_enter() - the linux port has no SPSel concept.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "tx_api.h"

#include "os/cmsis2/cmsis2.h"

#include "cmsis2_support.h"

void cmsis2_host_end_scheduler( void )
{
    fflush( NULL );
    exit( 0 );
}

void cmsis2_log( const char * fmt, ... )
{
    va_list ap;

    va_start( ap, fmt );
    ( void ) vfprintf( stderr, fmt, ap );
    va_end( ap );
}

void cmsis2_tx_kernel_enter( void )
{
    tx_kernel_enter();
}
