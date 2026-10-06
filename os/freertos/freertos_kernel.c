/**
 * @file    freertos_kernel.c
 * @brief   Kernel lifecycle entry points of the FreeRTOS port layer.
 * @author  zhugengyu
 * @date    06.10.2026
 */

#include "FreeRTOS.h"
#include "task.h"

#include "freertos_kernel.h"

void freertos_kernel_stop( void )
{
    vTaskEndScheduler();
}
