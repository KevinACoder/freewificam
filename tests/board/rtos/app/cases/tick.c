/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   tick.c
 * @brief  Case tick: the kernel runs, the tick advances, two threads run.
 *
 * The ladder waits for three absolute tick targets and checks the system
 * counter against them, so a tick that runs fast or slow against the hardware
 * time base is visible (a kernel that merely increments a counter on time
 * would pass a self-consistency check).  The low-priority thread proves a
 * second thread is scheduled while the runner sleeps.
 */

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

#define TICK_FREQ_EXPECTED	1000u
#define LADDER_BEATS		3u
#define LADDER_TOLERANCE_MS	20

/* Platform capacity (throughput rounds). Every rate this platform reports is a
 * per-frame cost divided by a clock, and a ceiling is only interpretable next to
 * what one core does on this boot. Two shapes, both on a 32 KiB buffer that
 * stays in L1/L2, so the result is the core's byte rate and not memory
 * bandwidth:
 *   copy  - memcpy() source to destination;
 *   fill  - memset() over the same buffer.
 * 8 MiB per shape keeps the window in the milliseconds, which the microsecond
 * clock resolves well. The figure is published as KiB/s and is compared across
 * boots (the same image class), never against another platform. */
#define TICK_CPU_BUF		32768u
#define TICK_CPU_ROUNDS		256u

static uint8_t tick_cpu_src[TICK_CPU_BUF] __attribute__((aligned(64)));
static uint8_t tick_cpu_dst[TICK_CPU_BUF] __attribute__((aligned(64)));

static volatile uint32_t tick_lo_beats;

static void tick_lo(void *arg)
{
	(void)arg;

	for (;;) {
		(void)osDelay(500u);
		tick_lo_beats++;
		if (tick_lo_beats <= 4u) {
			CASE_ANCHOR("tick: lo n=%u\n",
				    (unsigned)tick_lo_beats);
		}
	}
}

int case_tick_run(void)
{
	const osThreadAttr_t lo_attr = {
		.name = "tick-lo",
		.priority = osPriorityLow,
		.stack_size = 2048U,
	};
	uint32_t freq = osKernelGetTickFreq();
	uint32_t t0;
	uint64_t us0;
	unsigned i;
	int ok = 1;
	long err_max = 0;

	if (freq != TICK_FREQ_EXPECTED) {
		CASE_ANCHOR("tick: FAIL freq=%lu want=%lu\n",
			    (unsigned long)freq,
			    (unsigned long)TICK_FREQ_EXPECTED);
		return -1;
	}

	if (osThreadNew(tick_lo, NULL, &lo_attr) == NULL) {
		CASE_ANCHOR("tick: FAIL lo thread create\n");
		return -1;
	}

	t0 = osKernelGetTickCount();
	us0 = rtos_clock_us();

	for (i = 1u; i <= LADDER_BEATS; i++) {
		uint32_t target = t0 + i * TICK_FREQ_EXPECTED;
		uint32_t now_tick;
		uint64_t now_us;
		uint32_t wall_ms;
		long err;

		for (;;) {
			uint32_t now = osKernelGetTickCount();
			int32_t remain = (int32_t)(target - now);

			if (remain <= 0) {
				break;
			}
			(void)osDelay((remain > 20) ? 10u : 1u);
		}

		now_tick = osKernelGetTickCount();
		now_us = rtos_clock_us();
		wall_ms = (uint32_t)((now_us - us0) / 1000u);
		err = (long)wall_ms - (long)(i * TICK_FREQ_EXPECTED);

		CASE_ANCHOR("tick: t=%lu wall_ms=%lu err_ms=%ld\n",
			    (unsigned long)now_tick,
			    (unsigned long)wall_ms, err);

		if (err < 0) {
			err = -err;
		}
		if (err > err_max) {
			err_max = err;
		}
		if (err > LADDER_TOLERANCE_MS) {
			ok = 0;
		}
	}

	CASE_ANCHOR("tick: ladder beats=%u err_max_ms=%ld tol_ms=%d\n",
		    (unsigned)LADDER_BEATS, err_max, LADDER_TOLERANCE_MS);

	if (tick_lo_beats < 2u) {
		CASE_ANCHOR("tick: FAIL lo thread beats=%u\n",
			    (unsigned)tick_lo_beats);
		ok = 0;
	}

	/* Platform capacity: what one core moves per second on this boot. A
	 * network ceiling quoted without it cannot be attributed between the CPU
	 * and the stack. */
	{
		uint64_t t0us;
		uint64_t us;
		unsigned r;

		(void)memset(tick_cpu_src, 0xa5, sizeof(tick_cpu_src));
		t0us = rtos_clock_us();
		for (r = 0u; r < TICK_CPU_ROUNDS; r++) {
			(void)memcpy(tick_cpu_dst, tick_cpu_src,
				     sizeof(tick_cpu_src));
		}
		us = rtos_clock_us() - t0us;
		CASE_ANCHOR("tick: cpu copy_kib_s=%lu bytes=%lu us=%lu\n",
			    (unsigned long)((us != 0u) ?
				(((uint64_t)TICK_CPU_BUF * TICK_CPU_ROUNDS *
				  1000u) / us) : 0u),
			    (unsigned long)((uint64_t)TICK_CPU_BUF *
					    TICK_CPU_ROUNDS),
			    (unsigned long)us);

		t0us = rtos_clock_us();
		for (r = 0u; r < TICK_CPU_ROUNDS; r++) {
			(void)memset(tick_cpu_dst, (int)(r & 0xffu),
				     sizeof(tick_cpu_dst));
		}
		us = rtos_clock_us() - t0us;
		CASE_ANCHOR("tick: cpu fill_kib_s=%lu bytes=%lu us=%lu\n",
			    (unsigned long)((us != 0u) ?
				(((uint64_t)TICK_CPU_BUF * TICK_CPU_ROUNDS *
				  1000u) / us) : 0u),
			    (unsigned long)((uint64_t)TICK_CPU_BUF *
					    TICK_CPU_ROUNDS),
			    (unsigned long)us);
	}

	CASE_ANCHOR("tick: %s\n", ok ? "PASS" : "FAIL");

	return ok ? 0 : -1;
}