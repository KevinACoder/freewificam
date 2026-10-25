/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   cmsis2_event.c
 * @brief  The CMSIS-RTOS2 event-flag group API for the ThreadX backend.
 *
 * The backend's per-thread flags (`osThreadFlags*`) ride a per-thread
 * event-flags group and answer only for the calling thread; a group whose
 * owner is not the caller (the common "producer sets, consumer waits" shape)
 * needs the group API.  ThreadX's event-flag groups are exactly that object,
 * so the mapping is direct:
 *
 *   osEventFlagsSet(id, bits)            tx_event_flags_set(..., TX_OR)
 *   osEventFlagsWait(id, bits, opts, t)  tx_event_flags_get with TX_OR_CLEAR /
 *                                        TX_AND_CLEAR (or the no-clear forms)
 *   osEventFlagsClear(id, bits)          a non-blocking get over the mask
 *   osEventFlagsGet(id)                  tx_event_flags_info_get's value
 *
 * `Get` deliberately asks the kernel instead of keeping a shadow: the Linux
 * userspace port does not publish the running thread to
 * `tx_interrupt_control()`, so a shadow guarded by the interrupt lock would
 * fault on the host tier - and a shadow is a second source of truth for a
 * value the kernel already exposes.
 *
 * Context: the whole backend is thread-context only; interrupt context defers
 * to a thread through a semaphore or a thread flag (the rule the port layer
 * records).
 */

#include "tx_api.h"

#include "heap.h"

#include "os/cmsis2/cmsis2.h"

typedef struct
{
    TX_EVENT_FLAGS_GROUP    group;
    int                     live;
} event_slot_t;

osEventFlagsId_t osEventFlagsNew( const osEventFlagsAttr_t * attr )
{
    event_slot_t * slot = pvPortMalloc( sizeof( event_slot_t ) );
    const char *   name = ( attr != NULL && attr->name != NULL ) ? attr->name
                                                                : "cmsis_event";

    if( slot == NULL )
    {
        return NULL;
    }
    if( tx_event_flags_create( &slot->group, ( CHAR * ) ( uintptr_t ) name )
        != TX_SUCCESS )
    {
        vPortFree( slot );
        return NULL;
    }
    slot->live = 1;

    return ( osEventFlagsId_t ) slot;
}

const char * osEventFlagsGetName( osEventFlagsId_t ef_id )
{
    /* CMSIS makes the query optional, and answer is a stable pointer: the
     * kernel's own name would have to be copied through
     * tx_event_flags_info_get into a caller buffer, and this backend keeps no
     * second record. */
    (void)ef_id;
    return NULL;
}

uint32_t osEventFlagsSet( osEventFlagsId_t ef_id, uint32_t flags )
{
    event_slot_t * slot = ( event_slot_t * ) ef_id;

    if( slot == NULL || !slot->live )
    {
        return ( uint32_t ) osFlagsErrorParameter;
    }
    if( tx_event_flags_set( &slot->group, ( ULONG ) flags, TX_OR ) != TX_SUCCESS )
    {
        return ( uint32_t ) osFlagsErrorUnknown;
    }

    return flags;
}

uint32_t osEventFlagsWait( osEventFlagsId_t ef_id, uint32_t flags, uint32_t options,
                           uint32_t timeout )
{
    event_slot_t * slot = ( event_slot_t * ) ef_id;
    ULONG          actual = 0U;
    UINT           get_option;
    UINT           rc;

    if( slot == NULL || !slot->live )
    {
        return ( uint32_t ) osFlagsErrorParameter;
    }

    if( ( options & osFlagsNoClear ) != 0U )
    {
        get_option = ( ( options & osFlagsWaitAll ) != 0U ) ? TX_AND : TX_OR;
    }
    else
    {
        get_option = ( ( options & osFlagsWaitAll ) != 0U ) ? TX_AND_CLEAR
                                                            : TX_OR_CLEAR;
    }

    rc = tx_event_flags_get( &slot->group, ( ULONG ) flags, get_option, &actual,
                             ( ULONG ) timeout );

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

uint32_t osEventFlagsClear( osEventFlagsId_t ef_id, uint32_t flags )
{
    event_slot_t * slot = ( event_slot_t * ) ef_id;
    ULONG          previous = 0U;
    ULONG          actual = 0U;

    if( slot == NULL || !slot->live )
    {
        return ( uint32_t ) osFlagsErrorParameter;
    }

    ( void ) tx_event_flags_info_get( &slot->group, TX_NULL, &previous,
                                      TX_NULL, TX_NULL, TX_NULL );

    /* ThreadX clears bits only by consuming them: a non-blocking OR get over
     * the mask consumes every set bit it asks for and touches nothing else. */
    ( void ) tx_event_flags_get( &slot->group, ( ULONG ) flags, TX_OR_CLEAR,
                                 &actual, TX_NO_WAIT );

    return ( uint32_t ) previous;
}

uint32_t osEventFlagsGet( osEventFlagsId_t ef_id )
{
    event_slot_t * slot = ( event_slot_t * ) ef_id;
    ULONG          value = 0U;

    if( slot == NULL || !slot->live )
    {
        return 0U;
    }

    if( tx_event_flags_info_get( &slot->group, TX_NULL, &value,
                                 TX_NULL, TX_NULL, TX_NULL ) != TX_SUCCESS )
    {
        return 0U;
    }

    return ( uint32_t ) value;
}

osStatus_t osEventFlagsDelete( osEventFlagsId_t ef_id )
{
    event_slot_t * slot = ( event_slot_t * ) ef_id;

    if( slot == NULL || !slot->live )
    {
        return osErrorParameter;
    }

    ( void ) tx_event_flags_delete( &slot->group );
    slot->live = 0;
    vPortFree( slot );

    return osOK;
}