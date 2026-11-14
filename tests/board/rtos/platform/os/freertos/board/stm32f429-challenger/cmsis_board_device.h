/*
 * cmsis_board_device.h - CM4 interrupt-state intrinsics for the CMSIS adapter.
 *
 * The Cortex-M has real PRIMASK/BASEPRI/IPSR registers, so this is the small
 * vendor-header stand-in the adapter's IS_IRQ_MASKED()/IS_IRQ_MODE() need.
 */

#ifndef CMSIS_BOARD_DEVICE_H
#define CMSIS_BOARD_DEVICE_H

#include <stdint.h>

#include "cmsis_compiler.h"

__STATIC_INLINE uint32_t __get_IPSR( void )
{
    uint32_t value;
    __ASM volatile( "mrs %0, ipsr" : "=r"( value ) );
    return value;
}

__STATIC_INLINE uint32_t __get_PRIMASK( void )
{
    uint32_t value;
    __ASM volatile( "mrs %0, primask" : "=r"( value ) );
    return value;
}

__STATIC_INLINE uint32_t __get_BASEPRI( void )
{
    uint32_t value;
    __ASM volatile( "mrs %0, basepri" : "=r"( value ) );
    return value;
}

__STATIC_INLINE void __set_BASEPRI( uint32_t value )
{
    __ASM volatile( "msr basepri, %0" :: "r"( value ) : "memory" );
}

__STATIC_INLINE void __disable_irq( void )
{
    __ASM volatile( "cpsid i" ::: "memory" );
}

__STATIC_INLINE void __enable_irq( void )
{
    __ASM volatile( "cpsie i" ::: "memory" );
}

__STATIC_INLINE uint32_t __get_CONTROL( void )
{
    uint32_t value;
    __ASM volatile( "mrs %0, control" : "=r"( value ) );
    return value;
}

__STATIC_INLINE void __set_CONTROL( uint32_t value )
{
    __ASM volatile( "msr control, %0" :: "r"( value ) : "memory" );
    __ASM volatile( "isb" ::: "memory" );
}

#endif /* CMSIS_BOARD_DEVICE_H */
