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
#include "semphr.h"

#include "os/cmsis2/cmsis2.h"
#include "cmsis_board_device.h"
#include "uart.h"

#if defined(CONFIG_F429_HEAP_SDRAM)
/* The kernel heap lives in the core board's FMC SDRAM (NOLOAD section, so the
 * startup .bss zeroing never touches it).  heap_4.c declares this array
 * extern when configAPPLICATION_ALLOCATED_HEAP is 1 and initialises it on the
 * first pvPortMalloc(); main() has already brought the controller up by then.
 * portBYTE_ALIGNMENT on the CM4F port is 8. */
uint8_t ucHeap[ configTOTAL_HEAP_SIZE ]
    __attribute__( ( used, section( ".sdram.heap" ), aligned( 8 ) ) );
#endif

/*
 * The Linux clock's tick supply.  liblinux80211's `jiffies` advances once per
 * RTOS tick and every mac80211 MLME deadline is a jiffies comparison; an image
 * that links the component but never advances the tick runs the MLME timer at
 * the port's 1 ms clamp forever, the iface work spins, and the association
 * request never leaves (2026-11-15 F429 association: 9153 `sta_work` entries in 2 s and
 * no assoc).  The component is not linked into every F429 image, so the
 * reference is weak; lkpi_tick_advance() is retained there for this.
 */
extern void lkpi_tick_advance( void ) __attribute__( ( weak ) );

/*
 * data-plane wedge sampler (diagnostic, polled UART, no locks): every half second the
 * tick hook prints the PC the tick interrupted.  A wedged loop that leaves
 * interrupts on keeps printing the same PC (which names the loop); a loop with
 * interrupts masked stops printing entirely (which names that class instead).
 * Printing from the tick hook bypasses the console outlet on purpose - its
 * lock is a blocking mutex and must never be taken from an exception.
 */
#if defined( CONFIG_F429_TICK_PC ) && CONFIG_F429_TICK_PC
static uint32_t read_psp( void )
{
    uint32_t v;

    __asm__ __volatile__( "mrs %0, psp" : "=r"( v ) );
    return v;
}

static uint32_t read_msp( void )
{
    uint32_t v;

    __asm__ __volatile__( "mrs %0, msp" : "=r"( v ) );
    return v;
}

static void tick_pc_sample( uint32_t tick )
{
    uint32_t psp_pc = *( volatile uint32_t * )( read_psp() + 0x18U );
    uint32_t msp_pc = *( volatile uint32_t * )( read_msp() + 0x18U );
    uint32_t pc = 0U;

    if ( psp_pc >= 0x08000000U && psp_pc < 0x08100000U )
        pc = psp_pc;
    else if ( msp_pc >= 0x08000000U && msp_pc < 0x08100000U )
        pc = msp_pc;
    uart_early_puts( "tickpc: tick=" );
    uart_early_put_hex32( tick );
    uart_early_puts( " pc=0x" );
    uart_early_put_hex32( pc );
    uart_early_putc( '\n' );
}
#endif

void vApplicationTickHook( void )
{
    if ( lkpi_tick_advance != 0 )
        lkpi_tick_advance();
#if defined( CONFIG_F429_TICK_PC ) && CONFIG_F429_TICK_PC
    {
        static uint32_t tick_n;

        tick_n++;
        if ( ( tick_n % 500U ) == 0U )
            tick_pc_sample( tick_n );
    }
#endif
}

#if defined(CONFIG_F429_LWIP_SDRAM) && CONFIG_F429_LWIP_SDRAM
/* lwIP's heap lives in the linker's .sdram.lwip reserve (stm32f429.ld:
 * __lwip_heap .. +256K).  lwipopts.h declares LWIP_RAM_HEAP_POINTER = this
 * pointer, so mem.c's ram_heap is that reserve instead of a 1 MB .bss array
 * the MCU does not have. */
extern unsigned char __lwip_heap[];
void *lkpi_lwip_heap_ptr = (void *)__lwip_heap;
#endif

/*
 * The USB completion wake.  On this board the DWC2 completion is produced in
 * the OTG_HS hard IRQ (third-party/cherryusb/port/dwc2/usb_hc_dwc2.c), so the
 * drain-thread semaphore must be given with the FromISR form: the plain
 * osSemaphoreRelease/xSemaphoreGive manipulates the ready lists and corrupts
 * the scheduler when called from an exception (the data-plane data-frame wedge).
 * The CMSIS-FreeRTOS adapter returns the FreeRTOS handle itself, so the cast
 * is the interface's own representation.
 */
void lkpi_usb_done_wake( osSemaphoreId_t sem );

void lkpi_usb_done_wake( osSemaphoreId_t sem )
{
    if ( __get_IPSR() != 0U ) {
        BaseType_t woken = pdFALSE;
        (void)xSemaphoreGiveFromISR( (SemaphoreHandle_t)sem, &woken );
        portYIELD_FROM_ISR( woken );
        return;
    }
    (void)osSemaphoreRelease( sem );
}

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

/* The USB host's completion path can call usb_osal_enter_critical_section()
 * from the OTG_HS interrupt (the DWC2 channel-free path does), and the
 * FreeRTOS CM4F port asserts if taskENTER_CRITICAL() is entered from an ISR.
 * The CMSIS-RTOS2 facade has no context argument, so the context is read from
 * IPSR and the matching FromISR pair is used. */
os_critical_state_t osCriticalEnter( void )
{
    if( __get_IPSR() != 0U )
    {
        return ( os_critical_state_t ) taskENTER_CRITICAL_FROM_ISR();
    }
    taskENTER_CRITICAL();
    return 0;
}

void osCriticalExit( os_critical_state_t state )
{
    if( __get_IPSR() != 0U )
    {
        taskEXIT_CRITICAL_FROM_ISR( state );
        return;
    }
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
