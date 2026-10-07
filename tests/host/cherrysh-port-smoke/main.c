/**
 * @file    main.c
 * @brief   Host smoke: the CherrySH console shell on a CMSIS-RTOS2 host runtime.
 * @author  zhugengyu
 * @date    07.10.2026
 *
 * Brings the kernel up, wires the shell port and hands the process to the repl
 * task. The whole session then happens on the shell's own byte stream: the
 * suite drives it by feeding tests/host/cherrysh-port-smoke/input.txt on stdin
 * and asserting on stdout (tools/mock/cherrysh-port-smoke/check.sh). The run
 * ends through the `exit` command, which the port's host backend maps to
 * cmsis2_host_end_scheduler().
 *
 * This file sees the CMSIS-RTOS2 API only: os/cmsis2/cmsis2.h is the single
 * entry point, and the shell port reaches the console through its backend.
 */

#include <stdio.h>

#include "os/cmsis2/cmsis2.h"
#include "ports/cherrysh/shell_port.h"

#define THREAD_STACK_SIZE 4096U

#define TAG "cherrysh-port-smoke: "

int main( void )
{
    static const osThreadAttr_t xShellAttr = {
        .name       = "shell",
        .priority   = osPriorityNormal,
        .stack_size = THREAD_STACK_SIZE,
    };

    if( osKernelInitialize() != osOK )
    {
        fputs( TAG "FAIL kernel init\n", stderr );
        return 1;
    }

    if( shell_port_init() != 0 )
    {
        fputs( TAG "FAIL shell init\n", stderr );
        return 1;
    }

    if( osThreadNew( shell_port_repl_task, NULL, &xShellAttr ) == NULL )
    {
        fputs( TAG "FAIL thread create\n", stderr );
        return 1;
    }

    /* On the ThreadX backend this call does not come back; on FreeRTOS it
     * returns once the exit command ended the scheduler. Either way the shell
     * run is over when this returns. */
    if( osKernelStart() != osOK )
    {
        fputs( TAG "FAIL kernel start\n", stderr );
        return 1;
    }

    return 0;
}
