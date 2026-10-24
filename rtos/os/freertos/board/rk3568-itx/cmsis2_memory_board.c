/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   cmsis2_memory_board.c
 * @brief  Board-side heap face of the CMSIS-RTOS2 extension for FreeRTOS.
 *
 * The facade's allocator pair (osMemoryAlloc/osMemoryFree) is what the imported
 * storage stack's OS abstraction calls for its buffers and locks.  The FreeRTOS
 * backend's own heap is the kernel heap: heap_4 over the 64 KiB arena the board
 * configuration sizes, so the pair is the kernel's pvPortMalloc/vPortFree and
 * there is no second allocator to keep honest.
 *
 * The critical-section half of the extension lives in cmsis2_ext_board.c (the
 * file the reference board images already carry); this is the memory half,
 * added because this round is the first to put the storage stack on the
 * FreeRTOS backend.
 */

#include "FreeRTOS.h"
#include "task.h"

#include "os/cmsis2/cmsis2.h"

void * osMemoryAlloc( size_t size )
{
    return pvPortMalloc( size );
}

void osMemoryFree( void * ptr )
{
    vPortFree( ptr );
}