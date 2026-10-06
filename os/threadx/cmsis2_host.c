/**
 * @file    cmsis2_host.c
 * @brief   Host lifecycle end for the ThreadX backend of the CMSIS-RTOS2 facade.
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
 */

#include <stdio.h>
#include <stdlib.h>

#include "os/cmsis2/cmsis2.h"

void cmsis2_host_end_scheduler( void )
{
    fflush( NULL );
    exit( 0 );
}
