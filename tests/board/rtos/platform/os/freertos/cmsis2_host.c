/**
 * @file    cmsis2_host.c
 * @brief   FreeRTOS-side implementation of the CMSIS2 host exit seam.
 * @author  zhugengyu
 * @date    06.10.2026
 *
 * cmsis2_host_end_scheduler() is declared by the CMSIS-RTOS2 facade (os/cmsis2/cmsis2.h)
 * and implemented here, in the layer that owns kernel knowledge, so that no application
 * or facade file has to include a FreeRTOS kernel header.
 */

#include "FreeRTOS.h"
#include "task.h"

#include "os/cmsis2/cmsis2.h"

void cmsis2_host_end_scheduler( void )
{
    vTaskEndScheduler();
}
