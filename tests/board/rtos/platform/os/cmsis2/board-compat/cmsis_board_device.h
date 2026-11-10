/**
 * @file    cmsis_board_device.h
 * @brief   Board stand-in for the CMSIS device header (CMSIS_device_header).
 * @author  zhugengyu
 * @date    07.10.2026
 *
 * The CMSIS-FreeRTOS adapter requires CMSIS_device_header to be defined; on a
 * micro-controller that is the vendor device header. This board has no CMSIS
 * device layer, so this stand-in supplies exactly what the adapter reaches
 * for, expressed with this project's AArch64/GIC-600 primitives:
 *
 *  - the interrupt-state intrinsics: DAIF for "interrupts masked", the GIC's
 *    running priority for "inside an interrupt" (AArch64 has no IPSR, and the
 *    adapter's fallback branch reads one);
 *  - no SysTick: the tick comes from the generic timer through the board
 *    glue, and the adapter's SysTick_Handler block is compiled only under
 *    #if defined(SysTick).
 *
 * The interrupt masking goes through arch_asm.h (the arch layer owns the
 * instruction spelling), and the "inside an interrupt" probe through
 * gicv3.h - no raw system-register access here.
 */

#ifndef CMSIS_BOARD_DEVICE_H
#define CMSIS_BOARD_DEVICE_H

#include <stdint.h>

#include "arch_asm.h"
#include "gicv3.h"

/* Non-zero while an interrupt is being serviced: the GIC's running priority
 * reads 0xff (idle) outside one. */
__STATIC_INLINE uint32_t __get_IPSR( void )
{
	return ( gic_running_priority() != 0xffU ) ? 1U : 0U;
}

/* DAIF.I is this architecture's "interrupts masked" bit (bit 7 of DAIF). */
__STATIC_INLINE uint32_t __get_PRIMASK( void )
{
	uint64_t daif;

	ARCH_DAIF_GET( daif );
	return (uint32_t)( ( daif >> 7 ) & 1U );
}

__STATIC_INLINE void __disable_irq( void )
{
	ARCH_IRQ_MASK();
}

__STATIC_INLINE void __enable_irq( void )
{
	ARCH_IRQ_UNMASK();
}

#endif /* CMSIS_BOARD_DEVICE_H */
