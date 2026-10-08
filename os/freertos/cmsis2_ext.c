/**
 * @file    cmsis2_ext.c
 * @brief   FreeRTOS-side critical section for the CMSIS-RTOS2 extension.
 * @author  zhugengyu
 * @date    07.10.2026
 *
 * @design adapter - ref linux:kernel/locking/spinlock.c:88 (irqsave take/release)
 *
 * The port here is the POSIX host port: its interrupt masks block signals in the
 * calling thread and cannot exclude the other pthreads that are this kernel's
 * tasks, so the section must be a real lock. One process-wide pthread mutex,
 * taken recursively through a thread-local depth counter, gives the exclusion
 * the contract asks for; the taking is short and never waits on a kernel object.
 */

#include <pthread.h>

#include "FreeRTOS.h"

#include "os/cmsis2/cmsis2.h"

static pthread_mutex_t   cmsis2_ext_lock = PTHREAD_MUTEX_INITIALIZER;
static __thread uint32_t cmsis2_ext_depth;

os_critical_state_t osCriticalEnter( void )
{
    if ( cmsis2_ext_depth++ == 0u )
    {
        ( void ) pthread_mutex_lock( &cmsis2_ext_lock );
    }

    return ( os_critical_state_t ) cmsis2_ext_depth;
}

void osCriticalExit( os_critical_state_t state )
{
    ( void ) state;

    if ( cmsis2_ext_depth != 0u && --cmsis2_ext_depth == 0u )
    {
        ( void ) pthread_mutex_unlock( &cmsis2_ext_lock );
    }
}

/**
 * The heap of this backend: the kernel's own heap (heap_4 in this build), which is
 * where the kernel allocates its objects from as well.
 */
void * osMemoryAlloc( size_t size )
{
    return pvPortMalloc( size );
}

void osMemoryFree( void * ptr )
{
    vPortFree( ptr );
}
