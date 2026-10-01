/*
 * @file   iperf3_port.h
 * @brief  OS/timing glue for the vendored iperf3_embedded client on
 *         ThreadX SMP.
 *
 * Upstream targets FreeRTOS; the vendored file swaps its whole FreeRTOS
 * include block for this header - the only structural adjustment, see
 * third-party/iperf3_embedded/. The library's OS surface is small enough
 * to map one-to-one onto CMSIS-RTOS2:
 *
 *   TickType_t / xTaskGetTickCount()  uint32_t / osKernelGetTickCount().
 *                                     The tick glue runs at 1000 Hz
 *                                     (tx_user.h TX_TIMER_TICKS_PER_SECOND),
 *                                     so ticks ARE milliseconds and
 *                                     portTICK_PERIOD_MS is 1.
 *   pdMS_TO_TICKS / vTaskDelay        identity / osDelay.
 *   xTaskCreate(depth, prio)          osThreadNew. The depth parameter keeps
 *                                     its upstream meaning - FreeRTOS
 *                                     aarch64 StackType_t words (8 bytes) -
 *                                     and the priority maps onto the CMSIS
 *                                     band the lwIP adapter uses for the
 *                                     same numbers (AboveNormal for >= 4).
 *   vTaskDelete(NULL)                 osThreadTerminate(osThreadGetId());
 *                                     the CMSIS adapter reaps the zombie on
 *                                     the next thread-slot allocation.
 *   IPERF3_PRINTF                     lwip_arch_diag, the locked console
 *                                     path ping and lwIP diagnostics use.
 *                                     The LOG_* macros are pre-defined so
 *                                     the handshake stays visible on the
 *                                     console while it is unproven.
 *
 * errno comes from lwip/errno.h (LWIP_PROVIDE_ERRNO); the variable is
 * defined once in the adapter (lwip_diag.c). Newlib's <errno.h> must not
 * be mixed into translation units that see this header.
 *
 * @author zhugengyu
 * @date   22.09.2026
 */

#ifndef FREEWEBCAMERA_IPERF3_PORT_H
#define FREEWEBCAMERA_IPERF3_PORT_H

#include <stdint.h>

#include "lwip/arch.h"		/* LWIP_PLATFORM_DIAG -> lwip_arch_diag */
#include "lwip/errno.h"		/* errno constants + the variable */

#ifdef THREADX_BUILD

/*
 * ThreadX SMP: map the FreeRTOS surface onto CMSIS-RTOS2. The shims live
 * in iperf3_port.c.
 */

typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef int BaseType_t;

#define pdPASS			1
#define portTICK_PERIOD_MS	1U
#define pdMS_TO_TICKS(ms)	(ms)

BaseType_t xTaskCreate(void (*task_fn)(void *), const char *name,
		       uint32_t stack_depth_words, void *arg, uint32_t prio,
		       TaskHandle_t *task_out);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(uint32_t ms);
uint32_t xTaskGetTickCount(void);
#define taskYIELD()		osThreadYield()

#else /* !THREADX_BUILD */

/*
 * FreeRTOS line: the kernel's own headers provide the task surface the
 * vendored client was written against - verbatim semantics, no shims.
 * The tick glue runs at 1000 Hz here too, so the comment above holds.
 */

#include "FreeRTOS.h"
#include "task.h"

#endif /* THREADX_BUILD */

/* Shared, both kernels: printf() does not exist in this image, so every
 * report goes out the locked diag console. Offered before the vendored
 * file's #ifndef defaults; info lines stay on while the protocol
 * handshake is unproven. */
#define IPERF3_PRINTF(fmt, ...) \
	do { LWIP_PLATFORM_DIAG((fmt, ##__VA_ARGS__)); } while (0)
#define IPERF3_LOG_ERR(fmt, ...) \
	IPERF3_PRINTF("[iperf3] ERROR: " fmt "\n", ##__VA_ARGS__)
#define IPERF3_LOG_WARN(fmt, ...) \
	IPERF3_PRINTF("[iperf3] WARN:  " fmt "\n", ##__VA_ARGS__)
#define IPERF3_LOG_INFO(fmt, ...) \
	IPERF3_PRINTF("[iperf3] info:  " fmt "\n", ##__VA_ARGS__)

#endif /* FREEWEBCAMERA_IPERF3_PORT_H */
