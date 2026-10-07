/*
 * @file
 * @brief ipl(9) shim for the FreeRTOS port.
 *
 * The usbdi shim protects its completion ring with ipl_save/restore;
 * on this port that maps onto the port's ISR-grade interrupt mask
 * (ICCRPMR based), so the ring stays consistent whether the post
 * happens from the CherryUSB ISR or a worker task.
 *
 * @author zhugengyu
 * @date 10.09.2026
 */

#ifndef _COMPAT_HAL_IPL_H_
#define _COMPAT_HAL_IPL_H_

#include "FreeRTOS.h"
#include "task.h"

typedef UBaseType_t ipl_t;

#define IPL_USB 0

static inline ipl_t ipl_save(void) {
	return (ipl_t) portSET_INTERRUPT_MASK_FROM_ISR();
}

static inline void ipl_restore(ipl_t ipl) {
	portCLEAR_INTERRUPT_MASK_FROM_ISR(ipl);
}

#endif /* _COMPAT_HAL_IPL_H_ */
