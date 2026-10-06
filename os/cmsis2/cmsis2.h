/**
 * @file    cmsis2.h
 * @brief   Single entry point of the CMSIS-RTOS2 abstraction: backend selection.
 * @author  zhugengyu
 * @date    06.10.2026
 *
 * Application and component code includes this header only. The CMSIS-RTOS2 API header
 * is the upstream one from CMSIS_6 (CMSIS/RTOS2/Include/cmsis_os2.h), never a local copy;
 * FreeRTOS kernel headers stay inside os/freertos/.
 *
 * Define exactly one backend:
 *   CMSIS2_BACKEND_FREERTOS - FreeRTOS kernel, served by the upstream CMSIS-FreeRTOS
 *                             adapter compiled into the target build;
 *   CMSIS2_BACKEND_THREADX  - ThreadX kernel; selection point only, not implemented yet.
 */

#ifndef CMSIS2_H
#define CMSIS2_H

#if defined( CMSIS2_BACKEND_FREERTOS ) && defined( CMSIS2_BACKEND_THREADX )
#error "define exactly one CMSIS2 backend"
#elif defined( CMSIS2_BACKEND_FREERTOS )
#include "cmsis_os2.h"
#elif defined( CMSIS2_BACKEND_THREADX )
#error "the CMSIS2 ThreadX backend is not implemented yet"
#else
#error "no CMSIS2 backend selected: define CMSIS2_BACKEND_FREERTOS or CMSIS2_BACKEND_THREADX"
#endif

/**
 * @brief   End the scheduler from a task so that the scheduler-start call returns.
 *
 * The CMSIS-RTOS2 API has no kernel-stop call, but a host application needs one to
 * let main() return. The backend implements this seam in its own port layer (the
 * FreeRTOS backend in os/freertos/), so the caller never sees a kernel header.
 * Must be called from a task.
 */
void cmsis2_host_end_scheduler( void );

#endif /* CMSIS2_H */
