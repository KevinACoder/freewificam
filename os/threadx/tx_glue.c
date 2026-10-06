/**
 * @file    tx_glue.c
 * @brief   Application-side entry point the ThreadX kernel asks for.
 * @author  zhugengyu
 * @date    06.10.2026
 *
 * The kernel calls tx_application_define() exactly once, from its own initialization
 * path, after the kernel objects' bookkeeping is up and before the scheduler starts.
 * That is the first moment ThreadX permits kernel objects to be created, so it is also
 * where the CMSIS-RTOS2 layer creates the threads that were registered while the kernel
 * was still inactive. With this file the kernel has everything it needs from the
 * application; the Linux port allocates the kernel memory itself.
 */

#include "tx_api.h"

void cmsis2_tx_application_define( void );

void tx_application_define( void * first_unused_memory )
{
    ( void ) first_unused_memory;

    cmsis2_tx_application_define();
}
