/*
 * @file   heap.h
 * @brief  The system allocation face (os interface, SPEC S3 §3.4.3).
 *
 * tlsf-backed arena over the linker heap reservation; thread stacks and
 * later component allocations come from here. The UP critical section
 * lives inside the implementation.
 */

#ifndef FREEWEBCAM_HEAP_H
#define FREEWEBCAM_HEAP_H

#include <stddef.h>

void *pvPortMalloc(size_t size);
void vPortFree(void *ptr);
size_t xPortGetFreeHeapSize(void);

#endif /* FREEWEBCAM_HEAP_H */
