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

#if defined(TX_EXECUTION_PROFILE_ENABLE)
/* Per-thread execution attribution from the upstream Execution Profile Kit
 * (third-party/threadx/utility/execution_profile_kit): its thread-enter/exit
 * and ISR-enter/exit hooks accumulate each control block's execution time
 * from the board's TX_EXECUTION_TIME_SOURCE (CNTVCT, 24 MHz). Walking the
 * created-thread list here keeps the tx_* surface inside this backend, the
 * same as the critical section above - the console reaches it through the
 * facade. Totals are read with interrupts masked so the list cannot rotate
 * mid-walk; the values themselves are stop-the-world snapshots, so the
 * running thread's own total lags by at most one switch. */
extern TX_THREAD *_tx_thread_created_ptr;
extern ULONG _tx_thread_created_count;
extern EXECUTION_TIME _tx_execution_isr_time_total;
extern EXECUTION_TIME _tx_execution_idle_time_total;

size_t osExecProfileGet(os_exec_profile_entry_t *entries, size_t max,
			uint64_t *isr_ticks, uint64_t *idle_ticks)
{
    TX_THREAD *thread;
    ULONG      count;
    size_t     written = 0u;
    UINT       saved = tx_interrupt_control(TX_INT_DISABLE);

    *isr_ticks = (uint64_t)_tx_execution_isr_time_total;
    *idle_ticks = (uint64_t)_tx_execution_idle_time_total;

    count = _tx_thread_created_count;
    thread = _tx_thread_created_ptr;
    while ((count-- > 0u) && (thread != TX_NULL) && (written < max)) {
        entries[written].name = thread->tx_thread_name;
        entries[written].ticks = (uint64_t)thread->tx_thread_execution_time_total;
        written++;
        thread = thread->tx_thread_created_next;
    }
    (void)tx_interrupt_control(saved);
    return written;
}
#else
size_t osExecProfileGet(os_exec_profile_entry_t *entries, size_t max,
			uint64_t *isr_ticks, uint64_t *idle_ticks)
{
    (void)entries;
    (void)max;
    *isr_ticks = 0u;
    *idle_ticks = 0u;
    return 0u;
}
#endif

void osMemoryGetStats( struct osMemoryStats * out )
{
    size_t used = 0u, peak = 0u, total = 0u;
    unsigned int fail = 0u;

    if ( out == 0 ) {
        return;
    }
    heap_get_usage( &used, &peak, &total, &fail );
    out->free_bytes = ( total > used ) ? ( total - used ) : 0u;
    out->min_free_bytes = ( total > peak ) ? ( total - peak ) : 0u;
    out->total_bytes = total;
    out->alloc_fail = fail;
}
