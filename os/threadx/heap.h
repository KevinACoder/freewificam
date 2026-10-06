/**
 * @file    heap.h
 * @brief   Allocation face of the ThreadX host layer behind the pvPortMalloc seam.
 * @author  zhugengyu
 * @date    06.10.2026
 *
 * Thread stacks and kernel object control blocks that the CMSIS-RTOS2 layer owns come
 * from here. The seam is deliberately narrow and named after its counterpart in the
 * other backend: the memory subsystem milestone replaces the allocator behind these
 * two functions, and nothing else in the layer has to move.
 */

#ifndef OS_THREADX_HEAP_H
#define OS_THREADX_HEAP_H

#include <stddef.h>

void * pvPortMalloc( size_t size );
void vPortFree( void * ptr );

#endif /* OS_THREADX_HEAP_H */
