/**
 * @file    cmsis2_threadx.c
 * @brief   CMSIS-RTOS2 backend over the ThreadX kernel.
 * @author  zhugengyu
 * @date    06.10.2026

 * @design adapter - ref threadx:utility/rtos_compatibility_layers/FreeRTOS/tx_freertos.c:137
 * @design initcall/builder - ref linux:init/main.c:1422
 *
 * The application sees cmsis_os2.h only; this file is the ThreadX side of that
 * abstraction and the only place in the repository that reads ThreadX control blocks,
 * besides the kernel configuration in this directory.
 *
 * Structure:
 *   - fixed pools of thread and timer slots. A slot carries the kernel control block,
 *     the bookkeeping the CMSIS API needs and (for a thread) its per-thread event-flags
 *     group (CMSIS thread flags); the id handed to the application is the slot, which
 *     keeps that id stable for the object's lifetime.
 *   - deferred kernel object creation. The kernel does not exist until it is entered,
 *     so objects asked for before the kernel start call - threads, message queues,
 *     semaphores, mutexes, timers - are only recorded and are created by the replay in
 *     cmsis2_tx_application_define(); see the section on the deferred list below.
 *   - a priority map from the CMSIS bands (larger value = more urgent) onto the
 *     ThreadX ready-list order (smaller value = more urgent).
 *
 * Platform notes: this file compiles on both sides. Diagnostics go through
 * cmsis2_log() and the kernel entry through cmsis2_tx_kernel_enter() - both declared
 * in cmsis2_support.h and implemented per side (host: stderr and tx_kernel_enter(),
 * so nothing here prints to the stdout channel a test uses for its own results;
 * board: the stamped console sink and the SPSel hand-over stub). Critical sections
 * use the kernel's PUBLIC interrupt-control API (tx_interrupt_control), which both
 * ports implement - never the port-internal symbols.
 */

#include <stddef.h>
#include <stdint.h>

#include "cmsis2_support.h"

#include "tx_api.h"
#include "tx_thread.h"
#include "tx_event_flags.h"
#include "tx_mutex.h"
#include "tx_semaphore.h"
#include "tx_queue.h"
#include "tx_timer.h"

#include "cmsis_os2.h"

#include "heap.h"

/* Thread slots. The pool is fixed because the CMSIS thread id is the slot address and
 * has to stay stable for the lifetime of the thread. */
#ifndef CMSIS2_MAX_THREADS
#define CMSIS2_MAX_THREADS    16
#endif

/* Stack used when the caller leaves the attribute's stack size at zero. Must also hold
 * the kernel entry frame of the thread wrapper. */
#define CMSIS2_DEFAULT_STACK    4096U

#define CMSIS2_INFO_TAG    "threadx-host"

/* --- thread slot pool ---------------------------------------------------------- */

typedef enum {
    SLOT_FREE = 0,
    SLOT_PENDING,    /* claimed before the kernel ran, created at define time */
    SLOT_LIVE,       /* tx_thread_create() done */
} slot_state_t;

typedef struct {
    TX_THREAD           thread;
    TX_EVENT_FLAGS_GROUP flags;
    osThreadFunc_t      func;
    void *              arg;
    char                name[ 16 ];
    void *              stack;
    uint32_t            stack_size;
    UINT                tx_prio;
    slot_state_t        state;
} thread_slot_t;

static thread_slot_t slots[ CMSIS2_MAX_THREADS ];

/* Timer slots. A slot holds the kernel timer, the CMSIS callback and its argument, and
 * the CMSIS type; the id handed out is the slot, so the expiry trampoline reaches the
 * callback through the same value every API call uses. The pool is fixed for the same
 * reason as the thread pool: the CMSIS id has to stay valid for the timer's lifetime. */
#ifndef CMSIS2_MAX_TIMERS
#define CMSIS2_MAX_TIMERS    8
#endif

typedef struct {
    TX_TIMER        timer;
    osTimerFunc_t   func;
    void *          arg;
    osTimerType_t   type;
    char            name[ 16 ];
    slot_state_t    state;
} timer_slot_t;

static timer_slot_t timer_slots[ CMSIS2_MAX_TIMERS ];

/* Kernel lifecycle: inactive -> ready at initialize -> running at start, which does not
 * return on this port. */
static enum {
    K_INACTIVE = 0,
    K_READY,
    K_RUNNING,
} kernel_state;

/* --- priority mapping ---------------------------------------------------------- */

/* CMSIS priority bands are four steps wide inside eight-band groups; the ThreadX ready
 * list has TX_MAX_PRIORITIES levels and no band structure. One line per mapping is
 * printed so a run's log records the values that were actually used. */
static unsigned int map_priority( osPriority_t prio )
{
    unsigned int band;
    unsigned int mapped;

    if( ( unsigned int ) prio < 8U )
    {
        band = 0U;
    }
    else
    {
        band = ( ( unsigned int ) prio / 8U );
        if( band > 7U )
        {
            band = 7U;
        }
    }
    mapped = ( TX_MAX_PRIORITIES - 1U ) - band * 4U;
    cmsis2_log( "cmsis2: prio %d -> tx %u\n", ( int ) prio, mapped );
    return mapped;
}

/* --- deferred kernel object creation -------------------------------------------- */

/* Kernel objects may not be created before the kernel has been entered. The Linux
 * userspace port's critical section is a recursive mutex that only becomes recursive
 * when the port initializes itself inside the kernel start call, so a creation reached
 * from main() before that does not fail - it deadlocks on the nested lock that the
 * kernel's own interrupt disable/restore pair takes around the creation.
 *
 * One mechanism covers every object: the request is recorded here at the time it is
 * made, and the replay walks the records once at kernel entry, which is the first
 * moment the kernel allows creation. The same registration-then-replay shape as a
 * kernel's own staged initialization, scaled down to the single phase this kernel
 * offers.
 *
 * The control block is allocated up front, because the id handed back to the caller has
 * to stay valid across the kernel start call. Until the replay has run the control block
 * carries no kernel id, so calls that use it are refused by the id checks below. */

typedef enum {
    DEFER_THREAD,           /* creates the thread and its event-flags group */
    DEFER_QUEUE,
    DEFER_SEMAPHORE,
    DEFER_MUTEX,
    DEFER_TIMER,            /* creates the timer, which is born inactive */
} deferred_kind_t;

typedef struct deferred_item {
    struct deferred_item * next;
    deferred_kind_t        kind;
    void *                 object;      /* control block; also the id handed out */
    char                   name[ 16 ];
    ULONG                  arg0;        /* queue: message size in words; semaphore: count */
    ULONG                  arg1;        /* queue: message storage size in bytes */
} deferred_item_t;

static deferred_item_t * deferred_head;

/* Cannot be a kernel status, so that an unknown kind can never look like success. */
#define DEFER_UNKNOWN_KIND    ( ( UINT ) 0xFFU )

static void copy_name( char * dst, size_t dst_size, const char * src )
{
    size_t k;

    if( src == NULL )
    {
        src = "cmsis";
    }
    for( k = 0U; k < dst_size - 1U && src[ k ] != '\0'; k++ )
    {
        dst[ k ] = src[ k ];
    }
    dst[ k ] = '\0';
}

/* Record an object that has to be created at kernel entry, and return the record so
 * that the caller can fill in its parameters. NULL means out of memory. */
static deferred_item_t * defer_new( deferred_kind_t kind, void * object, const char * name )
{
    deferred_item_t * item = pvPortMalloc( sizeof( deferred_item_t ) );

    if( item == NULL )
    {
        return NULL;
    }

    item->kind = kind;
    item->object = object;
    item->arg0 = 0UL;
    item->arg1 = 0UL;
    copy_name( item->name, sizeof( item->name ), name );
    item->next = deferred_head;
    deferred_head = item;
    return item;
}

static UINT create_thread_object( thread_slot_t * slot );
static UINT create_timer_object( timer_slot_t * slot );

/* The kernel's create calls take a plain name pointer and only read it, so the record is
 * passed as it is rather than casting the constness away four times over. */
static UINT create_deferred_item( deferred_item_t * item )
{
    UINT rc = DEFER_UNKNOWN_KIND;

    switch( item->kind )
    {
    case DEFER_THREAD:
    {
        thread_slot_t * slot = item->object;

        rc = create_thread_object( slot );
        if( rc != TX_SUCCESS )
        {
            /* The kernel never took this slot, so it goes back to free. */
            vPortFree( slot->stack );
            slot->stack = NULL;
            slot->state = SLOT_FREE;
        }
        break;
    }

    case DEFER_QUEUE:
        /* The message storage sits directly behind the control block, the same layout
         * the non-deferred path uses. */
        rc = tx_queue_create( ( TX_QUEUE * ) item->object, item->name, ( UINT ) item->arg0,
                              ( void * ) ( ( uintptr_t ) item->object + sizeof( TX_QUEUE ) ),
                              ( UINT ) item->arg1 );
        break;

    case DEFER_SEMAPHORE:
        rc = tx_semaphore_create( ( TX_SEMAPHORE * ) item->object, item->name, item->arg0 );
        break;

    case DEFER_MUTEX:
        rc = tx_mutex_create( ( TX_MUTEX * ) item->object, item->name, TX_INHERIT );
        break;

    case DEFER_TIMER:
    {
        timer_slot_t * slot = item->object;

        rc = create_timer_object( slot );
        if( rc != TX_SUCCESS )
        {
            /* The kernel never took this slot, so it goes back to free. */
            slot->state = SLOT_FREE;
        }
        break;
    }
    }

    return rc;
}

static void replay_deferred_objects( void )
{
    while( deferred_head != NULL )
    {
        deferred_item_t * item = deferred_head;

        deferred_head = item->next;

        if( create_deferred_item( item ) != TX_SUCCESS )
        {
            cmsis2_log( "cmsis2: deferred create failed for '%s'\n", item->name );
        }
        vPortFree( item );
    }
}

/* --- thread creation ------------------------------------------------------------ */

static void slot_entry( ULONG input )
{
    thread_slot_t * slot = &slots[ input ];

    slot->func( slot->arg );

    /* A CMSIS thread function returning is the same as terminating itself. The slot
     * stays LIVE and keeps owning its stack, so the thread parks instead of running
     * off the end. The park has to go through the kernel: a returned thread that
     * simply spins stays the running thread at its priority and starves every thread
     * below it, including the ones the system needs. Self-suspend takes it out of the
     * scheduler; the sleep below only matters if something resumes it later. */
    ( void ) tx_thread_suspend( &slot->thread );
    for( ; ; )
    {
        ( void ) tx_thread_sleep( TX_WAIT_FOREVER );
    }
}

static UINT slot_tx_create( thread_slot_t * slot )
{
    return tx_thread_create( &slot->thread, slot->name, slot_entry,
                             ( ULONG ) ( slot - slots ), slot->stack,
                             slot->stack_size, slot->tx_prio,
                             slot->tx_prio, TX_NO_TIME_SLICE,
                             TX_AUTO_START );
}

/* The kernel objects of a thread slot: its event-flags group first (the thread can set
 * its own flags from the moment it runs), then the thread itself. The running path and
 * the replay both come through here, so a slot is built one way only. */
static UINT create_thread_object( thread_slot_t * slot )
{
    UINT rc = tx_event_flags_create( &slot->flags, slot->name );

    if( rc != TX_SUCCESS )
    {
        return rc;
    }

    rc = slot_tx_create( slot );
    if( rc != TX_SUCCESS )
    {
        tx_event_flags_delete( &slot->flags );
        return rc;
    }

    slot->thread.tx_thread_cmsis_slot = slot;
    slot->state = SLOT_LIVE;
    return TX_SUCCESS;
}

osThreadId_t osThreadNew( osThreadFunc_t func, void * argument, const osThreadAttr_t * attr )
{
    thread_slot_t * slot = NULL;
    unsigned int    i;
    uint32_t        stack_size;
    UINT            rc;
    osPriority_t    prio = ( attr != NULL ) ? attr->priority : osPriorityNormal;

    /* Legal CMSIS band is 1..osPriorityISR; the header marks an illegal priority as
     * osPriorityError, which is negative and therefore not an upper bound. */
    if( func == NULL || prio <= osPriorityNone || prio > osPriorityISR )
    {
        return NULL;
    }

    for( i = 0U; i < ( unsigned ) CMSIS2_MAX_THREADS; i++ )
    {
        if( slots[ i ].state == SLOT_FREE )
        {
            slot = &slots[ i ];
            break;
        }
    }

    if( slot == NULL )
    {
        cmsis2_log( "cmsis2: slot pool exhausted (%u)\n", ( unsigned ) CMSIS2_MAX_THREADS );
        return NULL;
    }

    /* Claim the slot before anything else. tx_thread_create() with TX_AUTO_START can
     * switch into the new thread before returning, and that thread may create another
     * one - an unclaimed slot would be handed out twice, and the second thread would
     * find the first thread's event-flags group already in the kernel's created list. */
    slot->state = SLOT_PENDING;

    stack_size = ( attr != NULL && attr->stack_size != 0U ) ? attr->stack_size
                                                            : CMSIS2_DEFAULT_STACK;
    stack_size = ( stack_size + 15U ) & ~15U;

    slot->stack = pvPortMalloc( stack_size );
    if( slot->stack == NULL )
    {
        cmsis2_log( "cmsis2: stack alloc failed (%lu bytes)\n", ( unsigned long ) stack_size );
        slot->state = SLOT_FREE;
        return NULL;
    }

    slot->func = func;
    slot->arg = argument;
    slot->tx_prio = ( UINT ) map_priority( prio );
    slot->stack_size = stack_size;
    copy_name( slot->name, sizeof( slot->name ), ( attr != NULL ) ? attr->name : NULL );

    if( kernel_state != K_RUNNING )
    {
        /* The kernel is not running yet: record the slot and leave it claimed until the
         * replay builds it. */
        if( defer_new( DEFER_THREAD, slot, slot->name ) == NULL )
        {
            cmsis2_log( "cmsis2: no memory to defer '%s'\n", slot->name );
            vPortFree( slot->stack );
            slot->stack = NULL;
            slot->state = SLOT_FREE;
            return NULL;
        }
        slot->state = SLOT_PENDING;
    }
    else if( ( rc = create_thread_object( slot ) ) != TX_SUCCESS )
    {
        cmsis2_log( "cmsis2: thread create failed for '%s' (tx rc=%u, "
                    "stack=%u)\n", slot->name, ( unsigned ) rc,
                    ( unsigned ) slot->stack_size );
        vPortFree( slot->stack );
        slot->stack = NULL;
        slot->state = SLOT_FREE;
        return NULL;
    }

    return ( osThreadId_t ) slot;
}

/* The name record the slot already holds, reached from the CMSIS id. A hang
 * report needs to name the thread that did not come back; CMSIS-RTOS2 offers no
 * query, so this answers from the same record osThreadNew() filled in. */
const char * osThreadGetName( void * thread_id )
{
    thread_slot_t * slot = ( thread_slot_t * ) thread_id;
    size_t i;

    if( slot == NULL )
    {
        return NULL;
    }

    for( i = 0; i < CMSIS2_MAX_THREADS; i++ )
    {
        if( &slots[ i ] == slot )
        {
            return ( slots[ i ].state == SLOT_FREE ) ? NULL : slots[ i ].name;
        }
    }

    return NULL;
}

/* Create everything that was registered before the kernel started. Called from
 * tx_application_define(), the first moment the kernel allows creation. */
void cmsis2_tx_application_define( void )
{
    replay_deferred_objects();
}

osStatus_t osThreadTerminate( osThreadId_t thread_id )
{
    thread_slot_t * slot = ( thread_slot_t * ) thread_id;

    if( slot == NULL || slot->state != SLOT_LIVE )
    {
        return osErrorParameter;
    }

    if( tx_thread_terminate( &slot->thread ) != TX_SUCCESS )
    {
        return osErrorResource;
    }

    if( tx_thread_identify() == &slot->thread )
    {
        /* Terminating itself: the caller never comes back, so its stack and control
         * block cannot be freed here. The entry wrapper owns what is left. */
        return osOK;
    }

    tx_event_flags_delete( &slot->flags );
    /*
     * Release the kernel's own thread object as well.  A slot that goes back to
     * free while its TX_THREAD still carries the created id cannot be created
     * again: tx_thread_create answers TX_THREAD_ERROR for a control block it
     * believes is live, which is how a second controller bring-up fails on the
     * thread the first one left behind (observed on the board as
     * "thread create failed for 'usbh_hub1' (tx rc=14)").
     */
    ( void ) tx_thread_delete( &slot->thread );
    vPortFree( slot->stack );
    slot->stack = NULL;
    slot->state = SLOT_FREE;
    return osOK;
}

osThreadId_t osThreadGetId( void )
{
    TX_THREAD * current = tx_thread_identify();

    if( current == NULL )
    {
        return NULL;
    }
    return ( osThreadId_t ) current->tx_thread_cmsis_slot;
}

osStatus_t osThreadYield( void )
{
    tx_thread_relinquish();
    return osOK;
}

osStatus_t osThreadSetPriority( osThreadId_t thread_id, osPriority_t priority )
{
    thread_slot_t * slot = ( thread_slot_t * ) thread_id;
    UINT            prio;
    UINT            old;

    if( slot == NULL || slot->state != SLOT_LIVE )
    {
        return osErrorParameter;
    }
    /* the same legal band osThreadNew checks: 1..osPriorityISR */
    if( priority <= osPriorityNone || priority > osPriorityISR )
    {
        return osErrorParameter;
    }

    prio = ( UINT ) map_priority( priority );
    if( tx_thread_priority_change( &slot->thread, prio, &old ) != TX_SUCCESS )
    {
        return osErrorResource;
    }
    slot->tx_prio = prio;
    return osOK;
}

/* --- thread flags ---------------------------------------------------------------- */

static TX_EVENT_FLAGS_GROUP * flags_of( osThreadId_t thread_id )
{
    thread_slot_t * slot = ( thread_slot_t * ) thread_id;

    if( slot == NULL || slot->state != SLOT_LIVE )
    {
        return NULL;
    }
    return &slot->flags;
}

uint32_t osThreadFlagsSet( osThreadId_t thread_id, uint32_t flags )
{
    TX_EVENT_FLAGS_GROUP * group = flags_of( thread_id );
    UINT rc;

    if( group == NULL )
    {
        return ( uint32_t ) osFlagsErrorParameter;
    }
    rc = tx_event_flags_set( group, ( ULONG ) flags, TX_OR );
    return rc == TX_SUCCESS ? flags : ( uint32_t ) osFlagsErrorUnknown;
}

uint32_t osThreadFlagsSetFromISR( osThreadId_t thread_id, uint32_t flags )
{
    /* Setting flags never blocks, so the non-waiting kernel call is interrupt-safe. */
    return osThreadFlagsSet( thread_id, flags );
}

uint32_t osThreadFlagsWait( uint32_t flags, uint32_t options, uint32_t timeout )
{
    TX_EVENT_FLAGS_GROUP * group = flags_of( osThreadGetId() );
    ULONG actual = 0U;
    UINT get_option;
    UINT rc;

    if( group == NULL )
    {
        return ( uint32_t ) osFlagsErrorParameter;
    }

    /* CMSIS semantics: the wait clears by default, and "wait all" means the kernel
     * must see every requested bit. */
    if( ( options & osFlagsNoClear ) != 0U )
    {
        get_option = ( ( options & osFlagsWaitAll ) != 0U ) ? TX_AND : TX_OR;
    }
    else
    {
        get_option = ( ( options & osFlagsWaitAll ) != 0U ) ? TX_AND_CLEAR : TX_OR_CLEAR;
    }

    rc = tx_event_flags_get( group, ( ULONG ) flags, get_option, &actual, ( ULONG ) timeout );

    if( rc == TX_SUCCESS )
    {
        return ( uint32_t ) actual;
    }
    if( rc == TX_NO_EVENTS )
    {
        return ( uint32_t ) osFlagsErrorTimeout;
    }
    return ( uint32_t ) osFlagsErrorResource;
}

/* --- kernel lifecycle ------------------------------------------------------------ */

osStatus_t osKernelInitialize( void )
{
    if( kernel_state != K_INACTIVE )
    {
        return osError;
    }
    kernel_state = K_READY;
    return osOK;
}

osStatus_t osKernelStart( void )
{
    if( kernel_state != K_READY )
    {
        return osError;
    }

    /* Never returns on this port: the kernel takes over the calling thread as its
     * scheduler and only leaves it when the process ends. */
    kernel_state = K_RUNNING;
    cmsis2_tx_kernel_enter();
    return osOK;
}

osKernelState_t osKernelGetState( void )
{
    switch( kernel_state )
    {
    case K_READY:
        return osKernelReady;
    case K_RUNNING:
        return osKernelRunning;
    default:
        return osKernelInactive;
    }
}

osStatus_t osKernelGetInfo( osVersion_t * version, char * id_buf, uint32_t id_buf_len )
{
    static const char id[] = CMSIS2_INFO_TAG;
    uint32_t i;

    if( version != NULL )
    {
        version->api = 0x00020003U;       /* CMSIS-RTOS2 API V2.3 */
        version->kernel = 0x06050102U;    /* ThreadX 6.5.1 */
    }
    if( id_buf != NULL && id_buf_len != 0U )
    {
        for( i = 0U; i < id_buf_len - 1U && id[ i ] != '\0'; i++ )
        {
            id_buf[ i ] = id[ i ];
        }
        id_buf[ i ] = '\0';
    }
    return osOK;
}

/* --- time ------------------------------------------------------------------------ */

/* The tick is the kernel's own time base. The CMSIS system timer has a higher
 * resolution than a tick on most targets; on this port there is no such counter, so
 * both readings come from the tick and the system timer frequency reports the tick
 * frequency instead of inventing a resolution that does not exist. */
uint32_t osKernelGetTickCount( void )
{
    return ( uint32_t ) tx_time_get();
}

uint32_t osKernelGetTickFreq( void )
{
    return ( uint32_t ) TX_TIMER_TICKS_PER_SECOND;
}

uint32_t osKernelGetSysTimerCount( void )
{
    return ( uint32_t ) tx_time_get();
}

uint32_t osKernelGetSysTimerFreq( void )
{
    return ( uint32_t ) TX_TIMER_TICKS_PER_SECOND;
}

/* --- scheduler locking ----------------------------------------------------------- */

#define LOCK_NEST_MAX    8U

static uint32_t     lock_depth;
static unsigned int lock_posture[ LOCK_NEST_MAX ];

/* CMSIS contract: report the previous lock state (1 = locked, 0 = not locked, negative
 * = error). The depth stack is what makes nesting honest - only the outermost
 * transition reaches the kernel's critical section, and it is entered and left exactly
 * as often as it was asked for. */
int32_t osKernelLock( void )
{
    int32_t prev;

    if( lock_depth >= LOCK_NEST_MAX )
    {
        return ( int32_t ) osErrorResource;
    }
    prev = ( lock_depth == 0U ) ? 0 : 1;
    lock_posture[ lock_depth ] = tx_interrupt_control( TX_INT_DISABLE );
    lock_depth++;
    return prev;
}

int32_t osKernelUnlock( void )
{
    int32_t prev;

    if( lock_depth == 0U )
    {
        return 0;
    }
    prev = ( lock_depth == 1U ) ? 0 : 1;
    lock_depth--;
    ( void ) tx_interrupt_control( ( UINT ) lock_posture[ lock_depth ] );
    return prev;
}

int32_t osKernelRestoreLock( int32_t lock )
{
    while( lock_depth != 0U )
    {
        lock_depth--;
        ( void ) tx_interrupt_control( ( UINT ) lock_posture[ lock_depth ] );
    }
    if( lock == 1 )
    {
        lock_posture[ lock_depth ] = tx_interrupt_control( TX_INT_DISABLE );
        lock_depth = 1U;
    }
    return ( lock_depth != 0U ) ? 1 : 0;
}

/* --- delay ------------------------------------------------------------------------ */

osStatus_t osDelay( uint32_t ticks )
{
    if( ticks == 0U )
    {
        return osOK;
    }
    return tx_thread_sleep( ( ULONG ) ticks ) == TX_SUCCESS ? osOK : osError;
}

/* --- mutex ------------------------------------------------------------------------ */

/* Control blocks come off the same heap seam as the thread stacks, so the memory
 * subsystem milestone reaches them too. Priority inheritance is on: that is what a
 * mutex protecting a shared bus or a driver state machine wants, and the recursive
 * CMSIS attribute costs nothing extra because ThreadX mutexes are recursive by nature. */

osMutexId_t osMutexNew( const osMutexAttr_t * attr )
{
    TX_MUTEX *   mutex = pvPortMalloc( sizeof( TX_MUTEX ) );
    const char * name = ( attr != NULL && attr->name != NULL ) ? attr->name : "cmsis_mutex";

    if( mutex == NULL )
    {
        return NULL;
    }

    if( kernel_state != K_RUNNING )
    {
        if( defer_new( DEFER_MUTEX, mutex, name ) == NULL )
        {
            vPortFree( mutex );
            return NULL;
        }
        return ( osMutexId_t ) mutex;
    }

    if( tx_mutex_create( mutex, ( CHAR * ) ( uintptr_t ) name, TX_INHERIT ) != TX_SUCCESS )
    {
        vPortFree( mutex );
        return NULL;
    }
    return ( osMutexId_t ) mutex;
}

osStatus_t osMutexAcquire( osMutexId_t mutex_id, uint32_t timeout )
{
    TX_MUTEX * mutex = ( TX_MUTEX * ) mutex_id;

    if( mutex == NULL || mutex->tx_mutex_id != TX_MUTEX_ID )
    {
        return osErrorParameter;
    }

    if( tx_thread_identify() == NULL )
    {
        /* No thread context: only a non-blocking acquire is meaningful, because there
         * is no thread the kernel could suspend. */
        if( timeout != 0U )
        {
            return osErrorParameter;
        }
        return tx_mutex_get( mutex, TX_NO_WAIT ) == TX_SUCCESS ? osOK : osErrorResource;
    }

    return tx_mutex_get( mutex, ( ULONG ) timeout ) == TX_SUCCESS ? osOK : osErrorResource;
}

osStatus_t osMutexRelease( osMutexId_t mutex_id )
{
    TX_MUTEX * mutex = ( TX_MUTEX * ) mutex_id;

    if( mutex == NULL || mutex->tx_mutex_id != TX_MUTEX_ID )
    {
        return osErrorParameter;
    }
    return tx_mutex_put( mutex ) == TX_SUCCESS ? osOK : osErrorResource;
}

osStatus_t osMutexDelete( osMutexId_t mutex_id )
{
    TX_MUTEX * mutex = ( TX_MUTEX * ) mutex_id;

    if( mutex == NULL || mutex->tx_mutex_id != TX_MUTEX_ID )
    {
        return osErrorParameter;
    }
    if( tx_mutex_delete( mutex ) != TX_SUCCESS )
    {
        return osErrorResource;
    }
    vPortFree( mutex );
    return osOK;
}

/* --- semaphores ------------------------------------------------------------------- */

/* ThreadX semaphores have no upper bound, so the CMSIS maximum count is accepted and
 * ignored - callers that ask for an unbounded ceiling get exactly that.
 *
 * Note that CMSIS osWaitForever and TX_WAIT_FOREVER are the same value, so the timeout
 * passes through unchanged except for the zero case, which means "do not block". */

osSemaphoreId_t osSemaphoreNew( uint32_t max_count, uint32_t initial_count,
                                const osSemaphoreAttr_t * attr )
{
    TX_SEMAPHORE * sem = pvPortMalloc( sizeof( TX_SEMAPHORE ) );

    ( void ) max_count;
    ( void ) attr;

    if( sem == NULL )
    {
        return NULL;
    }

    if( kernel_state != K_RUNNING )
    {
        deferred_item_t * item = defer_new( DEFER_SEMAPHORE, sem, "cmsis-sem" );

        if( item == NULL )
        {
            vPortFree( sem );
            return NULL;
        }
        item->arg0 = ( ULONG ) initial_count;
        return ( osSemaphoreId_t ) sem;
    }

    if( tx_semaphore_create( sem, ( CHAR * ) "cmsis-sem", ( ULONG ) initial_count ) != TX_SUCCESS )
    {
        vPortFree( sem );
        return NULL;
    }
    return ( osSemaphoreId_t ) sem;
}



osStatus_t osSemaphoreAcquire( osSemaphoreId_t semaphore_id, uint32_t timeout )
{
    TX_SEMAPHORE * sem = ( TX_SEMAPHORE * ) semaphore_id;
    UINT           wait;
    UINT           ret;

    if( sem == NULL || sem->tx_semaphore_id != TX_SEMAPHORE_ID )
    {
        return osErrorParameter;
    }

    wait = ( timeout == 0U ) ? TX_NO_WAIT : ( UINT ) timeout;
    ret = tx_semaphore_get( sem, wait );

    return ( ret == TX_SUCCESS ) ? osOK
                                 : ( wait == TX_NO_WAIT ) ? osErrorResource : osErrorTimeout;
}

osStatus_t osSemaphoreRelease( osSemaphoreId_t semaphore_id )
{
    TX_SEMAPHORE * sem = ( TX_SEMAPHORE * ) semaphore_id;

    if( sem == NULL || sem->tx_semaphore_id != TX_SEMAPHORE_ID )
    {
        return osErrorParameter;
    }
    return tx_semaphore_put( sem ) == TX_SUCCESS ? osOK : osErrorResource;
}

osStatus_t osSemaphoreDelete( osSemaphoreId_t semaphore_id )
{
    TX_SEMAPHORE * sem = ( TX_SEMAPHORE * ) semaphore_id;

    if( sem == NULL || sem->tx_semaphore_id != TX_SEMAPHORE_ID )
    {
        return osErrorParameter;
    }
    if( tx_semaphore_delete( sem ) != TX_SUCCESS )
    {
        return osErrorResource;
    }
    vPortFree( sem );
    return osOK;
}

uint32_t osSemaphoreGetCount( osSemaphoreId_t semaphore_id )
{
    TX_SEMAPHORE * sem = ( TX_SEMAPHORE * ) semaphore_id;

    if( sem == NULL || sem->tx_semaphore_id != TX_SEMAPHORE_ID )
    {
        return 0U;
    }
    return ( uint32_t ) sem->tx_semaphore_count;
}

/* --- message queues ---------------------------------------------------------------- */

/* A ThreadX queue has a fixed message size counted in 32-bit words, so a byte size is
 * rounded up to whole words and anything beyond the kernel's limit is refused rather
 * than silently truncated. Message priorities are ignored: the queue is FIFO. */

osMessageQueueId_t osMessageQueueNew( uint32_t msg_count, uint32_t msg_size,
                                      const osMessageQueueAttr_t * attr )
{
    TX_QUEUE * queue;
    void *     storage;
    ULONG      words;
    UINT       q_bytes;

    ( void ) attr;

    if( msg_count == 0U || msg_size == 0U )
    {
        return NULL;
    }

    words = ( msg_size + 3U ) / 4U;
    if( words > 16U )
    {
        return NULL;
    }

    /* Control block and message storage in one allocation: the storage is handed to the
     * kernel as the queue area directly behind the control block. */
    q_bytes = sizeof( TX_QUEUE ) + msg_count * words * sizeof( ULONG );
    queue = pvPortMalloc( q_bytes );
    if( queue == NULL )
    {
        return NULL;
    }

    storage = ( void * ) ( ( uintptr_t ) queue + sizeof( TX_QUEUE ) );

    if( kernel_state != K_RUNNING )
    {
        deferred_item_t * item = defer_new( DEFER_QUEUE, queue, "cmsis-q" );

        if( item == NULL )
        {
            vPortFree( queue );
            return NULL;
        }
        item->arg0 = words;
        item->arg1 = msg_count * words * sizeof( ULONG );
        return ( osMessageQueueId_t ) queue;
    }

    if( tx_queue_create( queue, ( CHAR * ) "cmsis-q", ( UINT ) words, storage,
                         msg_count * words * sizeof( ULONG ) ) != TX_SUCCESS )
    {
        vPortFree( queue );
        return NULL;
    }
    return ( osMessageQueueId_t ) queue;
}

osStatus_t osMessageQueuePut( osMessageQueueId_t mq_id, const void * msg_ptr,
                              uint8_t msg_prio, uint32_t timeout )
{
    TX_QUEUE * queue = ( TX_QUEUE * ) mq_id;
    UINT       wait;
    UINT       ret;

    ( void ) msg_prio;

    if( queue == NULL || queue->tx_queue_id != TX_QUEUE_ID || msg_ptr == NULL )
    {
        return osErrorParameter;
    }

    wait = ( timeout == 0U ) ? TX_NO_WAIT : ( UINT ) timeout;
    ret = tx_queue_send( queue, ( void * ) msg_ptr, wait );

    return ( ret == TX_SUCCESS ) ? osOK
                                 : ( wait == TX_NO_WAIT ) ? osErrorResource : osErrorTimeout;
}

osStatus_t osMessageQueueGet( osMessageQueueId_t mq_id, void * msg_ptr,
                              uint8_t * msg_prio, uint32_t timeout )
{
    TX_QUEUE * queue = ( TX_QUEUE * ) mq_id;
    UINT       wait;
    UINT       ret;

    ( void ) msg_prio;

    if( queue == NULL || queue->tx_queue_id != TX_QUEUE_ID || msg_ptr == NULL )
    {
        return osErrorParameter;
    }

    wait = ( timeout == 0U ) ? TX_NO_WAIT : ( UINT ) timeout;
    ret = tx_queue_receive( queue, msg_ptr, wait );

    return ( ret == TX_SUCCESS ) ? osOK
                                 : ( wait == TX_NO_WAIT ) ? osErrorResource : osErrorTimeout;
}

uint32_t osMessageQueueGetCount( osMessageQueueId_t mq_id )
{
    TX_QUEUE * queue = ( TX_QUEUE * ) mq_id;

    if( queue == NULL || queue->tx_queue_id != TX_QUEUE_ID )
    {
        return 0U;
    }
    return ( uint32_t ) queue->tx_queue_enqueued;
}

uint32_t osMessageQueueGetSpace( osMessageQueueId_t mq_id )
{
    TX_QUEUE * queue = ( TX_QUEUE * ) mq_id;

    if( queue == NULL || queue->tx_queue_id != TX_QUEUE_ID )
    {
        return 0U;
    }
    return ( uint32_t ) queue->tx_queue_available_storage;
}

osStatus_t osMessageQueueDelete( osMessageQueueId_t mq_id )
{
    TX_QUEUE * queue = ( TX_QUEUE * ) mq_id;

    if( queue == NULL || queue->tx_queue_id != TX_QUEUE_ID )
    {
        return osErrorParameter;
    }
    if( tx_queue_delete( queue ) != TX_SUCCESS )
    {
        return osErrorResource;
    }
    vPortFree( queue );
    return osOK;
}

/* --- timers ----------------------------------------------------------------------- */

/* ThreadX expresses one-shot vs periodic as the timer's reschedule ticks: zero means
 * the timer goes inactive when it expires, non-zero means the kernel reloads that value
 * and keeps the timer running. That one number is the whole distinction, so the CMSIS
 * type stays a property of the slot and each start arms the kernel with the matching
 * reschedule value - no second kernel object and no second trampoline.
 *
 * The route taken is therefore: a timer is created inactive with the smallest legal
 * placeholder interval (the error-checking kernel refuses a zero initial expiration;
 * the inactive timer never reads the placeholder), and a start writes the caller's
 * interval with tx_timer_change() before activating - tx_timer_activate() alone would
 * fire with the ticks recorded at create time. The kernel's change call writes an
 * inactive timer only and its activate call refuses an active one, so arming from any
 * state is deactivate-then-change-then-activate: the kernel's own change-period
 * sequence, and exactly what makes osTimerStart() the CMSIS "start or restart".
 *
 * The callback runs in the kernel's timer thread - TX_TIMER_PROCESS_IN_ISR is refused
 * by tx_api.h, so the timer thread always exists - which is the context CMSIS
 * documents for a timer callback. All in-kernel state changes are the kernel calls
 * above; no separate critical section is needed here. */

static void timer_expired( ULONG input )
{
    timer_slot_t * slot = &timer_slots[ input ];

    /* Both fields are read before the call; the callback may delete the timer, which
     * only releases the slot and never rewrites what is already being used. */
    slot->func( slot->arg );
}

/* The running path and the deferred replay both come through here, so a timer is built
 * one way only, as with the thread slot above. */
static UINT create_timer_object( timer_slot_t * slot )
{
    UINT rc = tx_timer_create( &slot->timer, slot->name, timer_expired,
                               ( ULONG ) ( slot - timer_slots ), 1UL, 0UL, TX_NO_ACTIVATE );

    if( rc != TX_SUCCESS )
    {
        return rc;
    }

    slot->state = SLOT_LIVE;
    return TX_SUCCESS;
}

osTimerId_t osTimerNew( osTimerFunc_t func, osTimerType_t type, void * argument,
                        const osTimerAttr_t * attr )
{
    timer_slot_t * slot = NULL;
    unsigned int   i;

    if( func == NULL )
    {
        return NULL;
    }

    for( i = 0U; i < ( unsigned ) CMSIS2_MAX_TIMERS; i++ )
    {
        if( timer_slots[ i ].state == SLOT_FREE )
        {
            slot = &timer_slots[ i ];
            break;
        }
    }

    if( slot == NULL )
    {
        cmsis2_log( "cmsis2: timer pool exhausted (%u)\n", ( unsigned ) CMSIS2_MAX_TIMERS );
        return NULL;
    }

    /* Claim the slot before anything else: the same first step as the thread path, so a
     * second request cannot find this slot free. */
    slot->state = SLOT_PENDING;
    slot->func = func;
    slot->arg = argument;
    slot->type = ( type == osTimerOnce ) ? osTimerOnce : osTimerPeriodic;
    copy_name( slot->name, sizeof( slot->name ), ( attr != NULL ) ? attr->name : NULL );

    if( kernel_state != K_RUNNING )
    {
        /* The kernel is not running yet: the slot is recorded and the replay builds it
         * at kernel entry, like every other deferred object. */
        if( defer_new( DEFER_TIMER, slot, slot->name ) == NULL )
        {
            cmsis2_log( "cmsis2: no memory to defer '%s'\n", slot->name );
            slot->state = SLOT_FREE;
            return NULL;
        }
    }
    else if( create_timer_object( slot ) != TX_SUCCESS )
    {
        cmsis2_log( "cmsis2: timer create failed for '%s'\n", slot->name );
        slot->state = SLOT_FREE;
        return NULL;
    }

    return ( osTimerId_t ) slot;
}

osStatus_t osTimerStart( osTimerId_t timer_id, uint32_t ticks )
{
    timer_slot_t * slot = ( timer_slot_t * ) timer_id;
    ULONG          reschedule;

    if( slot == NULL || slot->state != SLOT_LIVE )
    {
        return osErrorParameter;
    }

    if( ticks == 0U )
    {
        /* The kernel refuses a zero-tick activation (its error-checking calls answer
         * TX_TICK_ERROR); CMSIS has no separate spelling for "expire now". */
        return osErrorParameter;
    }

    /* Start or restart: leave any previous activation first. */
    ( void ) tx_timer_deactivate( &slot->timer );

    /* Zero for one-shot, the interval for periodic, which the kernel reloads after
     * every expiration. */
    reschedule = ( slot->type == osTimerPeriodic ) ? ( ULONG ) ticks : 0UL;

    if( tx_timer_change( &slot->timer, ( ULONG ) ticks, reschedule ) != TX_SUCCESS )
    {
        return osError;
    }

    return tx_timer_activate( &slot->timer ) == TX_SUCCESS ? osOK : osError;
}

osStatus_t osTimerStop( osTimerId_t timer_id )
{
    timer_slot_t * slot = ( timer_slot_t * ) timer_id;
    UINT           active = TX_FALSE;

    if( slot == NULL || slot->state != SLOT_LIVE )
    {
        return osErrorParameter;
    }

    /* The kernel's deactivate is a harmless no-op for an inactive timer, so the state
     * is read first: CMSIS answers osErrorResource when there was nothing to stop. */
    if( tx_timer_info_get( &slot->timer, NULL, &active, NULL, NULL, NULL ) != TX_SUCCESS ||
        active != TX_TRUE )
    {
        return osErrorResource;
    }

    return tx_timer_deactivate( &slot->timer ) == TX_SUCCESS ? osOK : osError;
}

osStatus_t osTimerDelete( osTimerId_t timer_id )
{
    timer_slot_t * slot = ( timer_slot_t * ) timer_id;

    if( slot == NULL || slot->state != SLOT_LIVE )
    {
        return osErrorParameter;
    }

    /* The kernel deactivates an active timer as part of the delete; it refuses the call
     * from its own timer thread - the context a CMSIS callback runs in - which leaves
     * the timer as it was and the slot live. */
    if( tx_timer_delete( &slot->timer ) != TX_SUCCESS )
    {
        return osErrorResource;
    }

    slot->state = SLOT_FREE;
    return osOK;
}
