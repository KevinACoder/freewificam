/**
 * @file    cmsis2_ext.c
 * @brief   ThreadX-side critical section for the CMSIS-RTOS2 extension.
 * @author  zhugengyu
 * @date    07.10.2026
 *
 * @design adapter - ref linux:kernel/locking/spinlock.c:88 (irqsave take/release)
 * @design adapter - ref threadx:ports/linux/gnu/example_build/sample_threadx.c:79
 *         (the port's own entry pattern for kernel primitives)
 *
 * On this single-core target an interrupt mask IS the whole mutual exclusion:
 * no interrupt can fire, so no other context can be scheduled into the section.
 * tx_interrupt_control returns the previous mask state, which is the saved state
 * the CMSIS extension hands back to the caller; nesting restores naturally.
 */

#include "tx_api.h"

#include "os/cmsis2/cmsis2.h"
#include "os/threadx/heap.h"

os_critical_state_t osCriticalEnter( void )
{
    return ( os_critical_state_t ) tx_interrupt_control( TX_INT_DISABLE );
}

void osCriticalExit( os_critical_state_t state )
{
    ( void ) tx_interrupt_control( ( UINT ) state );
}

/**
 * The heap of this backend: the board's TLSF pool, reached through the layer's own
 * pvPortMalloc seam so the allocator can be replaced behind two functions.
 */
void * osMemoryAlloc( size_t size )
{
    return pvPortMalloc( size );
}

void osMemoryFree( void * ptr )
{
    vPortFree( ptr );
}
