/**
 * @file    cmsis2.h
 * @brief   Single entry point of the CMSIS-RTOS2 abstraction: backend selection.
 * @author  zhugengyu
 * @date    06.10.2026
 *
 * Application and component code includes this header only. The CMSIS-RTOS2 API header
 * is the upstream one from CMSIS_6 (CMSIS/RTOS2/Include/cmsis_os2.h), never a local copy;
 * kernel headers stay inside their own port layer (os/freertos/, os/threadx/).
 *
 * Define exactly one backend:
 *   CMSIS2_BACKEND_FREERTOS - FreeRTOS kernel, served by the upstream CMSIS-FreeRTOS
 *                             adapter compiled into the target build;
 *   CMSIS2_BACKEND_THREADX  - ThreadX kernel, served by the backend compiled from
 *                             os/threadx/;
 *   CMSIS2_BACKEND_BAREMETAL - cooperative kernel with no tick and no
 *                             preemption, compiled from os/baremetal/metal_*.c.
 */

#ifndef CMSIS2_H
#define CMSIS2_H

#if defined( CMSIS2_BACKEND_FREERTOS ) && defined( CMSIS2_BACKEND_THREADX )
#error "define exactly one CMSIS2 backend"
#elif defined( CMSIS2_BACKEND_FREERTOS )
#include "cmsis_os2.h"
#elif defined( CMSIS2_BACKEND_THREADX )
#include "cmsis_os2.h"
#elif defined( CMSIS2_BACKEND_BAREMETAL )
#include "cmsis_os2.h"
#else
#error "no CMSIS2 backend selected: define CMSIS2_BACKEND_FREERTOS, CMSIS2_BACKEND_THREADX or CMSIS2_BACKEND_BAREMETAL"
#endif

/* The critical-section extension lives beside the facade so that application and
 * component code still reaches everything through this one header. */
#include "cmsis_os2_ext.h"

/**
 * @brief   End the scheduler from a task so that the scheduler-start call returns.
 *
 * The CMSIS-RTOS2 API has no kernel-stop call, but a host application needs one to
 * leave a run cleanly. The backend implements this seam in its own port layer, so the
 * caller never sees a kernel header: the FreeRTOS backend stops its scheduler so that
 * the kernel start call returns and main() can return, the ThreadX backend ends the
 * process because its scheduler has no way back. Either way the run ends with exit
 * code 0 and the caller's code is written the same way.
 * Must be called from a task.
 */
void cmsis2_host_end_scheduler( void );

#endif /* CMSIS2_H */
