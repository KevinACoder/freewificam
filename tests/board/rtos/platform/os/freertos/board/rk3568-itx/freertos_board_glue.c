/**
 * @file    freertos_board_glue.c
 * @brief   The ONE place the FreeRTOS port meets this board's base layer.
 * @author  zhugengyu
 * @date    07.10.2026
 * @design  adapter - two existing APIs meet here and neither is modified:
 *          the ARM_AARCH64_SRE port's supply face (its contract is the
 *          #error list in third-party/FreeRTOS-Kernel/portable/GCC/
 *          ARM_AARCH64_SRE/port.c:36-72) and the board layer's seam.
 *
 * The port requires the application to supply four things:
 * configSETUP_TICK_INTERRUPT(), configCLEAR_TICK_INTERRUPT(),
 * vApplicationIRQHandler() and _freertos_vector_table. The first three live
 * here, the table in freertos_board_vectors.S.
 *
 * EOI OWNERSHIP: the port's IRQ entry reads ICC_IAR1_EL1, calls
 * vApplicationIRQHandler(ulICCIAR) and then writes ICC_EOIR1_EL1 itself
 * (portASM.S). The glue dispatches on the INTID it is handed and must NOT
 * end the interrupt - ending it here as well would corrupt the
 * active-priority state.
 */

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "arch_asm.h"
#include "board_conf.h"
#include "generic_timer.h"
#include "gicv3.h"
#include "uart.h"

/* CNTV reload in counter ticks, computed once from the runtime counter
 * frequency (CNTFRQ is read, never assumed). */
static uint32_t tick_reload;

static void put_dec( unsigned long value );

/* The registry's handler shape carries an argument; the port's tick handler
 * takes none. A one-line wrapper keeps the registry type honest instead of
 * casting the function pointer. */
static void tick_irq_handler( void *arg )
{
	(void)arg;
	FreeRTOS_Tick_Handler();
}

/* --- tick supply (the port's configSETUP/CLEAR slots) ---------------------- */

void freertos_board_tick_setup( void )
{
	uint32_t freq = gtimer_freq_get();

	tick_reload = ( freq != 0U ) ? ( freq / configTICK_RATE_HZ ) : 0U;

	/* Program the counter first, stopped: the line must not be able to
	 * assert while the GIC side is half-wired. */
	gtimer_vtimer_ctl_set( 0U );
	gtimer_vtimer_tval_set( tick_reload );

	/* GIC side: bring the controller up, then wire INTID 27 to the port's
	 * tick handler. Register and configure before enabling; the
	 * single-bit read-modify-write inside irq_enable is the board-proven
	 * path (gicv3.c file header). */
	gicv3_init();
	(void)irq_register( BOARD_TICK_INTID, tick_irq_handler, 0 );
	(void)irq_set_mode_level( BOARD_TICK_INTID );
	(void)irq_set_priority( BOARD_TICK_INTID, BOARD_TICK_PRIORITY_RAW );
	(void)irq_clear_pending( BOARD_TICK_INTID );
	(void)irq_enable( BOARD_TICK_INTID );

	/* Start the counter: enable, interrupt unmasked. The first tick lands
	 * one period from now. */
	gtimer_vtimer_ctl_set( 1U );

	/* The tick's own report - this exact line is part of the board-test
	 * anchor contract. */
	uart_early_puts( "tick: source=cntv irq=" );
	put_dec( (unsigned long)BOARD_TICK_INTID );
	uart_early_puts( " freq=" );
	put_dec( (unsigned long)configTICK_RATE_HZ );
	uart_early_puts( "Hz\n" );
}

void freertos_board_tick_rearm( void )
{
	/* The CNTV line is level-sensitive and clears when TVAL is rewritten.
	 * The port calls this from the tick handler before its EOI - that
	 * order is the board-proven anti-storm sequence. */
	gtimer_vtimer_tval_set( tick_reload );
}

/* --- interrupt entry (the port's vApplicationIRQHandler slot) -------------- */

void vApplicationIRQHandler( uint32_t ulICCIAR )
{
	/* Dispatch only; the port ends the interrupt (see the file header). */
	gic_irq_dispatch( ulICCIAR );
}

/* --- fault reporting for the runtime vector table ------------------------- */

/* Print the exception kind and the three registers that turn a coin toss
 * into a diagnosis, then park with interrupts masked. Called from the
 * vector table's non-handler slots (freertos_board_vectors.S). */
void freertos_board_fault_park( const char *kind )
{
	uint64_t esr;
	uint64_t far;
	uint64_t elr;

	uart_early_panic( kind );
	uart_early_puts( "fatal: ESR_EL1=" );
	ARCH_MRS( esr_el1, esr );
	uart_early_put_hex32( (uint32_t)esr );
	uart_early_puts( " FAR_EL1=" );
	ARCH_MRS( far_el1, far );
	uart_early_put_hex32( (uint32_t)far );
	uart_early_puts( " ELR_EL1=" );
	ARCH_MRS( elr_el1, elr );
	uart_early_put_hex32( (uint32_t)elr );
	uart_early_puts( "\n" );
	uart_early_puts( "fatal: parked; power-cycle to recover\n" );

	ARCH_DAIF_SET( 0xf );
	for( ;; ) {
		__asm__ __volatile__( "wfe" );
	}
}

/* --- diagnostics hooks ---------------------------------------------------- */

static void put_dec( unsigned long value )
{
	char buf[20];
	int i = 0;

	if( value == 0UL ) {
		uart_early_putc( '0' );
		return;
	}
	while( ( value != 0UL ) && ( i < (int)sizeof( buf ) ) ) {
		buf[i++] = (char)( '0' + ( value % 10UL ) );
		value /= 10UL;
	}
	while( i > 0 ) {
		uart_early_putc( buf[--i] );
	}
}

void vAssertCalled( const char *pcFile, unsigned long ulLine )
{
	uart_early_puts( "assert: " );
	uart_early_puts( ( pcFile != 0 ) ? pcFile : "?" );
	uart_early_puts( ":" );
	put_dec( ulLine );
	uart_early_puts( "\n" );
	uart_early_puts( "fatal: parked; power-cycle to recover\n" );

	ARCH_DAIF_SET( 0xf );
	for( ;; ) {
		__asm__ __volatile__( "wfe" );
	}
}

void vApplicationMallocFailedHook( void )
{
	uart_early_puts( "fatal: malloc failed (heap exhausted)\n" );
	uart_early_puts( "fatal: parked; power-cycle to recover\n" );

	ARCH_DAIF_SET( 0xf );
	for( ;; ) {
		__asm__ __volatile__( "wfe" );
	}
}

void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName )
{
	(void)xTask;
	uart_early_puts( "fatal: stack overflow task=" );
	uart_early_puts( ( pcTaskName != 0 ) ? pcTaskName : "?" );
	uart_early_puts( "\n" );
	uart_early_puts( "fatal: parked; power-cycle to recover\n" );

	ARCH_DAIF_SET( 0xf );
	for( ;; ) {
		__asm__ __volatile__( "wfe" );
	}
}
