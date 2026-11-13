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

/*
 * The Linux clock's tick supply.  liblinux80211's jiffies advances once per
 * RTOS tick; the component is not linked into every profile, so the reference
 * is weak.  lkpi_tick_advance() is retained in the component, otherwise
 * --gc-sections drops it (a weak reference does not keep a section), the
 * pointer resolves to NULL and the CMSIS weak default hook silently wins.
 */
extern void lkpi_tick_advance(void) __attribute__((weak));

void vApplicationTickHook(void)
{
#if CONFIG_LKPI_CENSUS
	/* defined below, in the census block: the interrupted PC of this tick */
	extern void rtos_pc_note( void );

	rtos_pc_note();
#endif
	if (lkpi_tick_advance != NULL)
		lkpi_tick_advance();
}

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

#if CONFIG_LKPI_CENSUS && ( configUSE_TRACE_FACILITY == 1 ) && \
	( configGENERATE_RUN_TIME_STATS == 1 )
/*
 * The per-task run-time snapshot (platform/rtos_stats.h).  The kernel keeps the
 * counter per task because tasks.c was built with configGENERATE_RUN_TIME_STATS
 * and the port macro reads CNTVCT; uxTaskGetSystemState hands back the table in
 * one pass, so the reader pays one scheduler suspension, not one lock per task.
 */
#include "rtos_stats.h"

#define RTOS_STATS_TASKS	24u

unsigned int rtos_task_stats_get( struct rtos_task_stat * out, unsigned int max )
{
	static TaskStatus_t snap[ RTOS_STATS_TASKS ];
	UBaseType_t n;
	UBaseType_t i;

	if( ( out == NULL ) || ( max == 0u ) ) {
		return 0u;
	}

	n = uxTaskGetSystemState( snap, RTOS_STATS_TASKS, NULL );
	if( n > max ) {
		n = max;
	}

	for( i = 0u; i < n; i++ ) {
		out[ i ].name = snap[ i ].pcTaskName;
		out[ i ].number = ( uint32_t ) snap[ i ].xTaskNumber;
		out[ i ].runtime = ( uint32_t ) snap[ i ].ulRunTimeCounter;
		out[ i ].state = ( uint32_t ) snap[ i ].eCurrentState;
		out[ i ].prio = ( uint32_t ) snap[ i ].uxCurrentPriority;
		out[ i ].stack_free = ( uint32_t ) snap[ i ].usStackHighWaterMark *
			( uint32_t ) sizeof( StackType_t );
		out[ i ].handle = ( void * ) snap[ i ].xHandle;
	}

	return ( unsigned int ) n;
}

/*
 * The tick PC sampler.  One sample per tick, taken in the tick hook below:
 * the interrupted PC (ELR_EL1) and the task it belonged to.  A spin is then a
 * named address instead of an inferred mechanism - the freeze watchdog asks
 * for the pump's own samples and prints the top ones.
 */
#define RTOS_PC_RING	1024u

struct rtos_pc_sample {
	void		*task;
	uint64_t	pc;
};

static struct rtos_pc_sample rtos_pc_ring[ RTOS_PC_RING ];
static volatile uint32_t rtos_pc_head;

void rtos_pc_note( void )
{
	uint32_t head = rtos_pc_head;
	uint64_t pc;

	__asm__ __volatile__( "mrs %0, elr_el1" : "=r"( pc ) );
	rtos_pc_ring[ head % RTOS_PC_RING ].task = xTaskGetCurrentTaskHandle();
	rtos_pc_ring[ head % RTOS_PC_RING ].pc = pc;
	rtos_pc_head = head + 1u;
}

uint64_t rtos_task_saved_pc( void * task )
{
	TaskHandle_t h = ( TaskHandle_t ) task;
	uint64_t *sp;

	if( h == NULL ) {
		return 0u;
	}
	/* TCB_t's first member is pxTopOfStack; the frame's first word is the
	 * ELR portSAVE_CONTEXT pushed last (the pump uses no FPU context). */
	sp = ( uint64_t * ) ( *( void ** ) h );
	if( sp == NULL ) {
		return 0u;
	}
	return sp[ 0 ];
}

unsigned int rtos_task_frame_scan( void * task, uint64_t * vals,
	uint32_t * offs, unsigned int max )
{
	TaskHandle_t h = ( TaskHandle_t ) task;
	uint64_t * sp;
	unsigned int i, n = 0u;

	if( h == NULL || vals == NULL || offs == NULL ) {
		return 0u;
	}
	sp = ( uint64_t * ) ( *( void ** ) h );
	if( sp == NULL ) {
		return 0u;
	}
	/* 0x300 bytes covers the GPRs, SPSR/ELR and the FPU indicator. */
	for( i = 0u; i < 0x300u / 8u && n < max; i++ ) {
		uint64_t v = sp[ i ];

		if( v >= 0xa0000000ull && v < 0xa2000000ull ) {
			vals[ n ] = v;
			offs[ n ] = i * 8u;
			n++;
		}
	}
	return n;
}

unsigned int rtos_task_frame_raw( void * task, uint32_t off, uint64_t * out,
	unsigned int n )
{
	TaskHandle_t h = ( TaskHandle_t ) task;
	uint64_t * sp;
	unsigned int i;

	if( h == NULL || out == NULL ) {
		return 0u;
	}
	sp = ( uint64_t * ) ( *( void ** ) h );
	if( sp == NULL ) {
		return 0u;
	}
	for( i = 0u; i < n; i++ ) {
		out[ i ] = sp[ ( off / 8u ) + i ];
	}
	return n;
}

uint64_t rtos_pc_last( void * task, uint32_t * ago_ms )
{
	unsigned int i;
	uint32_t head = rtos_pc_head;

	if( ago_ms != NULL ) {
		*ago_ms = 0u;
	}
	for( i = 0; i < RTOS_PC_RING; i++ ) {
		uint32_t at = ( head + RTOS_PC_RING - 1u - i ) % RTOS_PC_RING;
		struct rtos_pc_sample * s = &rtos_pc_ring[ at ];

		if( s->task == task ) {
			if( ago_ms != NULL ) {
				*ago_ms = i;
			}
			return s->pc;
		}
	}
	return 0u;
}

unsigned int rtos_pc_top( void * task, struct rtos_pc_hit * out, unsigned int max )
{
	uint32_t head = rtos_pc_head;
	uint32_t n = ( head < RTOS_PC_RING ) ? head : RTOS_PC_RING;
	uint32_t i, j, used = 0u;

	if( ( task == NULL ) || ( out == NULL ) || ( max == 0u ) ) {
		return 0u;
	}
	for( i = 0u; i < n; i++ ) {
		uint32_t idx = ( head + RTOS_PC_RING - n + i ) % RTOS_PC_RING;
		uint64_t pc = rtos_pc_ring[ idx ].pc;

		if( rtos_pc_ring[ idx ].task != task ) {
			continue;
		}
		for( j = 0u; j < used; j++ ) {
			if( out[ j ].pc == pc ) {
				out[ j ].count++;
				break;
			}
		}
		if( j == used ) {
			if( used < max ) {
				out[ used ].pc = pc;
				out[ used ].count = 1u;
				used++;
			}
		}
	}
	/* selection sort, top first: the table is at most a few entries */
	for( i = 0u; i < used; i++ ) {
		uint32_t best = i;
		struct rtos_pc_hit t;

		for( j = i + 1u; j < used; j++ ) {
			if( out[ j ].count > out[ best ].count ) {
				best = j;
			}
		}
		t = out[ i ];
		out[ i ] = out[ best ];
		out[ best ] = t;
	}
	return used;
}
#endif /* CONFIG_LKPI_CENSUS && trace facility && run-time stats */
