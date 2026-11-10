/**
 * @file    cmsis_host_device.h
 * @brief   Host stand-in for the CMSIS device header (CMSIS_device_header).
 * @author  zhugengyu
 * @date    06.10.2026
 *
 * The CMSIS-FreeRTOS adapter requires CMSIS_device_header to be defined. On a
 * micro-controller that is the vendor device header; a host has no device, so this
 * stand-in supplies exactly what the adapter reaches for: the interrupt-state
 * intrinsics.
 *
 * SysTick is deliberately not defined. The adapter's SysTick_Handler block is compiled
 * only under #if defined(SysTick), and the host port generates its tick from a timer
 * thread (POSIX SIGALRM), not from a SysTick peripheral.
 */

#ifndef CMSIS_HOST_DEVICE_H
#define CMSIS_HOST_DEVICE_H

#include <stdint.h>

#include "cmsis_compiler.h"

/* Host threads never run in an exception or interrupt context. */
__STATIC_INLINE uint32_t __get_IPSR( void )
{
    return 0U;
}

/* The port masks signals rather than the ARM PRIMASK register: "not masked" is reported
 * here, so critical sections inside the port are not seen as interrupt context. */
__STATIC_INLINE uint32_t __get_PRIMASK( void )
{
    return 0U;
}

/* No interrupt state to change: signal masking is owned by the port. */
__STATIC_INLINE void __disable_irq( void )
{
}

__STATIC_INLINE void __enable_irq( void )
{
}

#endif /* CMSIS_HOST_DEVICE_H */
