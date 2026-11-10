/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   main.c
 * @brief  The host semantic bench: one binary, N cases, one anchor per case.
 *
 * The binary links the component's real kernel-API bodies, the upstream
 * FreeRTOS POSIX port and the CMSIS-RTOS2 adapter; it runs on the host kernel
 * with no hardware behind it.  It is a *model* of the target's execution
 * context (x86, SMP, no interrupt masking) - tests/host/kpi/README.md lists
 * what that model cannot prove.
 *
 * Anchors (the contract tools/rtos/judge.py-style readers grep):
 *   kpi-test: <case> pass
 *   kpi-test: <case> fail <detail>
 *   kpi-test: <case> skip <reason>
 *   kpi-test: result pass <n>/<m> skipped=<k> failures=<f>
 *
 * Exit code is 0 iff there were no failures and the negative control behaved.
 */
#include <stdio.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_core.h"
#include "linuxkpi_impl.h"
#include "ulog.h"

#include "kpi_test.h"
#include "clock.h"

extern void host_log_sink(ulog_level_t level, const char *line);

int kpi_break;

uint64_t kpi_now_ms(void)
{
	return rtos_clock_us() / 1000u;
}

void kpi_delay_ms(uint32_t ms)
{
	(void)osDelay(ms);
}

int kpi_fail(const char *file, int line, const char *what, const char *detail)
{
	printf("kpi-test:   assertion failed at %s:%d: %s (%s)\n",
	       file, line, what, detail != NULL ? detail : "");
	return KPI_FAIL;
}

int kpi_skip(const char *file, int line, const char *reason)
{
	printf("kpi-test:   skip at %s:%d: %s\n", file, line, reason);
	return KPI_SKIP;
}

/* cases */
int kpi_case_work_order(void);
int kpi_case_work_coalesce(void);
int kpi_case_work_flush(void);
int kpi_case_work_cancel(void);
int kpi_case_timer_due(void);
int kpi_case_timer_rearm(void);
int kpi_case_timer_cancel(void);
int kpi_case_jiffies_tick(void);
int kpi_case_sync_completion(void);
int kpi_case_sync_mutex(void);
int kpi_case_sync_semaphore(void);
int kpi_case_skb_bounds(void);
int kpi_case_skb_pool(void);
int kpi_case_idr_rb(void);
int kpi_case_ctx_tasklet_thread(void);
int kpi_case_ctx_fifo(void);
int kpi_case_ctx_coalesce(void);
int kpi_case_ctx_kill_running(void);
int kpi_case_ctx_disable_enable(void);
int kpi_case_ctx_bh_defers(void);
int kpi_case_ctx_nesting(void);
int kpi_case_ctx_in_softirq(void);
int kpi_case_ctx_spin_bh(void);
int kpi_case_ctx_rcu(void);
int kpi_case_selftest(void);

static const struct kpi_case cases[] = {
	{ "work_order", kpi_case_work_order },
	{ "work_coalesce", kpi_case_work_coalesce },
	{ "work_flush", kpi_case_work_flush },
	{ "work_cancel", kpi_case_work_cancel },
	{ "timer_due", kpi_case_timer_due },
	{ "timer_rearm", kpi_case_timer_rearm },
	{ "timer_cancel", kpi_case_timer_cancel },
	{ "jiffies_tick", kpi_case_jiffies_tick },
	{ "sync_completion", kpi_case_sync_completion },
	{ "sync_mutex", kpi_case_sync_mutex },
	{ "sync_semaphore", kpi_case_sync_semaphore },
	{ "skb_bounds", kpi_case_skb_bounds },
	{ "skb_pool", kpi_case_skb_pool },
	{ "idr_rb", kpi_case_idr_rb },
	{ "ctx_tasklet_thread", kpi_case_ctx_tasklet_thread },
	{ "ctx_fifo", kpi_case_ctx_fifo },
	{ "ctx_coalesce", kpi_case_ctx_coalesce },
	{ "ctx_kill_running", kpi_case_ctx_kill_running },
	{ "ctx_disable_enable", kpi_case_ctx_disable_enable },
	{ "ctx_bh_defers", kpi_case_ctx_bh_defers },
	{ "ctx_nesting", kpi_case_ctx_nesting },
	{ "ctx_in_softirq", kpi_case_ctx_in_softirq },
	{ "ctx_spin_bh", kpi_case_ctx_spin_bh },
	{ "ctx_rcu", kpi_case_ctx_rcu },
	{ "selftest", kpi_case_selftest },
};

static int failures;
static int skipped;
static int passed;

static void run_all(void)
{
	const unsigned total = (unsigned)(sizeof(cases) / sizeof(cases[0]));
	unsigned i;

	lkpi_core_init();

	for (i = 0; i < total; i++) {
		int rc;

		rc = cases[i].run();

		switch (rc) {
		case KPI_PASS:
			printf("kpi-test: %s pass\n", cases[i].name);
			passed++;
			break;
		case KPI_SKIP:
			printf("kpi-test: %s skip\n", cases[i].name);
			skipped++;
			break;
		default:
			printf("kpi-test: %s fail\n", cases[i].name);
			failures++;
			break;
		}
	}

	printf("kpi-test: result pass %d/%u skipped=%d failures=%d\n",
	       passed, total, skipped, failures);
	(void)fflush(stdout);

	/* Stop the scheduler from a task so main()'s osKernelStart() returns; the
	 * POSIX port then deletes this thread (it must not return on its own). */
	cmsis2_host_end_scheduler();
}

int main(int argc, char **argv)
{
	osThreadAttr_t attr;
	osThreadId_t runner;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--break") == 0)
			kpi_break = 1;
	}

	if (osKernelInitialize() != osOK) {
		fprintf(stderr, "kpi-test: osKernelInitialize failed\n");
		return 2;
	}
	lkpi_log_sink_register(host_log_sink);
	rtos_clock_init();

	memset(&attr, 0, sizeof(attr));
	attr.name = "runner";
	attr.stack_size = 32u * 1024u;
	attr.priority = osPriorityNormal;
	runner = osThreadNew((osThreadFunc_t)run_all, NULL, &attr);
	if (runner == NULL) {
		fprintf(stderr, "kpi-test: runner thread not created\n");
		return 2;
	}
	if (osKernelStart() != osOK) {
		fprintf(stderr, "kpi-test: osKernelStart failed\n");
		return 2;
	}
	return failures == 0 ? 0 : 1;
}
