/**
 * @file    freertos_kernel.h
 * @brief   Kernel lifecycle entry points of the FreeRTOS port layer.
 * @author  zhugengyu
 * @date    06.10.2026
 *
 * The CMSIS-RTOS2 API has no kernel-stop call, so an application that must leave the
 * scheduler (a host test that wants main() to return, for example) needs a port-layer
 * entry point. It lives here, in the layer that owns kernel knowledge, so that neither
 * application code nor os/cmsis2/ has to include a FreeRTOS kernel header.
 */

#ifndef FREERTOS_KERNEL_H
#define FREERTOS_KERNEL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Stop the scheduler and let the scheduler-start call return.
 *
 * Must be called from a FreeRTOS task. On the POSIX port the calling task parks while the
 * scheduler unwinds, and the process leaves through the caller of osKernelStart().
 */
void freertos_kernel_stop( void );

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_KERNEL_H */
