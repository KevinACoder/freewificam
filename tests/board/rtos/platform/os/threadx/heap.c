/**
 * @file    heap.c
 * @brief   Host implementation of the pvPortMalloc/vPortFree seam.
 * @author  zhugengyu
 * @date    06.10.2026

 * @design proxy - ref linux:include/linux/dma-map-ops.h:16
 *
 * A thin pass-through to the C library allocator: on the host the C library already
 * provides a general heap, and a second allocator in front of it would only hide the
 * library's own diagnostics. Allocation failures return NULL, which every caller in
 * the CMSIS-RTOS2 layer already handles.
 */

#include <stdlib.h>

#include "heap.h"

void * pvPortMalloc( size_t size )
{
    return malloc( size );
}

void vPortFree( void * ptr )
{
    free( ptr );
}

void heap_get_usage( size_t * used, size_t * peak, size_t * total,
                     unsigned int * fail )
{
    if ( used != 0 ) *used = 0u;
    if ( peak != 0 ) *peak = 0u;
    if ( total != 0 ) *total = 0u;
    if ( fail != 0 ) *fail = 0u;
}
