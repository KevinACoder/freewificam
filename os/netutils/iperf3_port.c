/*
 * @file   iperf3_port.c
 * @brief  ThreadX SMP side of the iperf3_embedded port glue.
 *
 * Maps the FreeRTOS surface the vendored client uses onto CMSIS-RTOS2
 * (the mapping table is in iperf3_port.h) and provides the two platform
 * hooks the library expects from the integrator: a microsecond clock and
 * a sub-millisecond busy wait. Both are backed by CNTVCT_EL0 - the
 * virtual counter, like lwip_platform_rand in arch/cc.h, because the
 * physical counter is gated behind CNTHCTL_EL2.EL1PCEN on this boot path.
 *
 * @author zhugengyu
 * @date   22.09.2026
 */

#include <string.h>
#include <stdint.h>

#include "iperf3_port.h"

#ifdef THREADX_BUILD

#include "cmsis_os2.h"

/* The CMSIS adapter bands lwIP priority numbers 1:1 onto osPriority* /8;
 * prio 4 (the vendored IPERF3_TASK_PRIORITY) is osPriorityAboveNormal,
 * the same band as the tcpip thread's peers - above rx threads, below
 * the shell. Anything lower drops to Normal. */
static osPriority_t iperf3_port_band(uint32_t prio)
{
	return (prio >= 4U) ? osPriorityAboveNormal : osPriorityNormal;
}

BaseType_t xTaskCreate(void (*task_fn)(void *), const char *name,
		       uint32_t stack_depth_words, void *arg, uint32_t prio,
		       TaskHandle_t *task_out)
{
	osThreadAttr_t attr;
	osThreadId_t id;

	memset(&attr, 0, sizeof(attr));
	attr.name = name;
	/* Upstream counts FreeRTOS aarch64 StackType_t words (8 bytes). */
	attr.stack_size = stack_depth_words * 8U;
	attr.priority = iperf3_port_band(prio);

	id = osThreadNew((osThreadFunc_t) task_fn, arg, &attr);
	if (id == NULL) {
		return 0;
	}
	if (task_out != NULL) {
		*task_out = (TaskHandle_t) id;
	}
	return pdPASS;
}

void vTaskDelete(TaskHandle_t task)
{
	if (task == NULL) {
		task = osThreadGetId();
	}
	(void) osThreadTerminate(task);
}

void vTaskDelay(uint32_t ms)
{
	if (ms == 0U) {
		(void) osThreadYield();
		return;
	}
	(void) osDelay(ms);
}

uint32_t xTaskGetTickCount(void)
{
	return (uint32_t) osKernelGetTickCount();
}

#endif /* THREADX_BUILD */

/* strtoul: provided image-wide by os/baremetal/minilibc (the S7b FR-10
 * gap-filling round chose minilibc over this file's local copy - one
 * definition, one semantics). The vendored client parses the server's
 * JSON result with it.
 */
/* --- CNTVCT-backed microsecond time -------------------------------------- */

static uint64_t iperf3_cntfrq(void)
{
	static uint64_t freq;
	uint64_t f;

	if (freq == 0U) {
		__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(f));
		freq = (f != 0U) ? f : 1000000U;
	}
	return freq;
}

static inline uint64_t iperf3_cntvct(void)
{
	uint64_t v;

	__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(v));
	return v;
}

uint64_t iperf3_platform_get_time_us(void)
{
	uint64_t freq = iperf3_cntfrq();
	uint64_t cnt = iperf3_cntvct();

	return (cnt / freq) * 1000000U + (cnt % freq) * 1000000U / freq;
}

void iperf3_platform_delay_us(uint32_t us)
{
	uint64_t freq = iperf3_cntfrq();
	uint64_t start = iperf3_cntvct();
	uint64_t wait = ((uint64_t) us * freq + 999999U) / 1000000U;

	while ((iperf3_cntvct() - start) < wait) {
		/* busy-wait by contract */
	}
}
