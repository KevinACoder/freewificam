/*
 * freertos_board_glue.c - STM32F429 FreeRTOS + CMSIS-RTOS2 board glue.
 *
 * The CM4F port owns SysTick and the SVCall/PendSV vectors; this file supplies
 * the application hooks the kernel and the CMSIS adapter require, and the
 * critical-section/allocator seam the shared CMSIS2 facade calls.
 */

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "os/cmsis2/cmsis2.h"
#include "uart.h"

void vAssertCalled( const char *file, unsigned long line )
{
    uart_early_panic( "assert: " );
    uart_early_puts( ( file != 0 ) ? file : "?" );
    uart_early_putc( ':' );
    uart_early_put_hex32( (uint32_t)line );
    uart_early_puts( "\nfatal: parked; power-cycle to recover\n" );
    for ( ;; ) {
        __asm__ __volatile__( "wfe" );
    }
}

void vApplicationMallocFailedHook( void )
{
    uart_early_panic( "fatal: malloc failed (heap exhausted)\n" );
    for ( ;; ) {
        __asm__ __volatile__( "wfe" );
    }
}

void vApplicationStackOverflowHook( TaskHandle_t task, char *name )
{
    (void)task;
    uart_early_panic( "fatal: stack overflow " );
    uart_early_puts( ( name != 0 ) ? name : "?" );
    uart_early_puts( "\n" );
    for ( ;; ) {
        __asm__ __volatile__( "wfe" );
    }
}

os_critical_state_t osCriticalEnter( void )
{
    taskENTER_CRITICAL();
    return 0;
}

void osCriticalExit( os_critical_state_t state )
{
    (void)state;
    taskEXIT_CRITICAL();
}

void * osMemoryAlloc( size_t size )
{
    return pvPortMalloc( size );
}

void osMemoryFree( void *ptr )
{
    vPortFree( ptr );
}

size_t osExecProfileGet( os_exec_profile_entry_t *entries, size_t max,
                         uint64_t *isr_ticks, uint64_t *idle_ticks )
{
    (void)entries;
    (void)max;
    *isr_ticks = 0u;
    *idle_ticks = 0u;
    return 0u;
}

/* The xPack newlib has no libatomic multilib.  The only 64-bit atomic in the
 * shared log sink is a single-core counter; the sink's critical section already
 * excludes the other context, so the read/update/write pair is the whole
 * operation. */
uint64_t __atomic_fetch_add_8( volatile void *ptr, uint64_t value, int memorder )
{
    (void)memorder;
    volatile uint64_t *counter = (volatile uint64_t *)ptr;
    uint64_t old = *counter;

    *counter = old + value;
    return old;
}
