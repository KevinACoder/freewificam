/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * main.c - the urb stress harness entry point.
 *
 * Modes:
 *   (default)                 the 28-cell scenario matrix, then the matrix line
 *   --fuzz                    the matrix, then the seeded stream for seeds
 *                             1, 2, 3 (the --iters value, default 200000)
 *   --fuzz --seed N --iters M one seeded run
 *   --replay <seed> <n>       the first n operations of that seed: it stops at
 *                             the same first-witness index as the full run (or
 *                             completes with the same digest)
 *   --fault double-complete   the must-go-red self-proof: one transfer is given
 *                             a second giveback at the seam, the exactly-once
 *                             rule must name exactly that urb and exit non-zero
 *   --cell <id>               one matrix cell (debugging aid)
 *   --list-cells              the cell ids
 *
 * One process, one run: the environment wrapper brings up the runtime, the
 * synthetic controller and scripted device model of the sibling host suite, and
 * the clean-room command-driver stand-in; the matrix or the stream then drives
 * it.  The harness task runs above the engine's drain thread, so a completion
 * committed by the harness is delivered while the harness is inside a blocking
 * wait - the cells assert only against a settled engine.
 *
 * Exit status is 0 only when every cell and every fuzz block held; a judgement
 * violation exits 1 through the judge (after printing the first witness), a
 * sanitizer report exits non-zero through the sanitizer runtime.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"

#include "engine_env.h"
#include "judge.h"

/* The matrix, the stream and the one-cell mode live in their own units. */
int	matrix_run_all(void);
int	matrix_run_cell(const char *id);
void	matrix_list_cells(void);
int	matrix_run_double_complete_fault(void);
unsigned int	matrix_cell_count(void);
int	fuzz_run(unsigned int seed, unsigned int iters);

/* The one-shot process leaves the kernel objects, the controller's worker and
 * the driver stand-in's static tables in place at exit; those are fixtures, not
 * leaks, so the leak detector is off while every other ASan/UBSan check stays
 * on (a sanitizer *report* still fails the run). */
const char *
__asan_default_options(void)
{

	/* detect_leaks=0: the one-shot process leaves the kernel objects and the
	 * fixtures in place at exit.
	 * allocator_may_return_null=1: the harness deliberately asks the seam's
	 * allocators for sizes the host cannot satisfy (the alloc-fail cell), and
	 * the seam's contract is a NULL return plus its named counter - not a
	 * sanitizer abort.  Every other ASan/UBSan check stays on and a report is
	 * still a failure. */
	return "detect_leaks=0:allocator_may_return_null=1";
}

static int g_failures;
static int g_fuzz_mode;
static unsigned int g_fuzz_seeds[4] = { 1, 2, 3, 0 };
static unsigned int g_seed_count = 3;
static unsigned int g_iters = 200000;
static unsigned int g_replay_seed;
static unsigned int g_replay_n;
int g_replay_mode;		/* fuzz.c's replay anchor (see fuzz_run) */
static const char *g_cell;
static const char *g_fault;

static void
app_task(void *arg)
{
	(void)arg;
	if (g_cell == NULL && !g_replay_mode && g_fault == NULL)
		printf("URBSTRESS: matrix start cells=%u fuzz=%d\n",
		    matrix_cell_count(), g_fuzz_mode);
	if (env_init() != 0) {
		printf("URBSTRESS: environment bring-up failed\n");
		g_failures++;
		cmsis2_host_end_scheduler();
		return;
	}
	if (g_fault != NULL) {
		/* --fault double-complete: the self-proof must go red inside the
		 * judge (which exits 1 itself); reaching here means the fault was
		 * absorbed and the harness would be lying about its own proof */
		(void)matrix_run_double_complete_fault();
		printf("URBSTRESS: fault-scenario absorbed ok=0\n");
		g_failures++;
	} else if (g_replay_mode) {
		judge_repro_set("linuxkpi-usb-urb-stress --replay");
		g_failures += fuzz_run(g_replay_seed, g_replay_n);
	} else if (g_cell != NULL) {
		g_failures += matrix_run_cell(g_cell) != 0;
	} else {
		g_failures += matrix_run_all() != 0;
		if (g_fuzz_mode) {
			unsigned int i;

			for (i = 0; i < g_seed_count; i++)
				g_failures += fuzz_run(g_fuzz_seeds[i], g_iters) != 0;
		}
	}
	fflush(stdout);
	cmsis2_host_end_scheduler();
}

int
main(int argc, char **argv)
{
	osThreadId_t t;
	osThreadAttr_t attr = { 0 };
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--fuzz") == 0) {
			g_fuzz_mode = 1;
		} else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
			g_fuzz_seeds[0] = (unsigned int)strtoul(argv[++i], NULL,
			    10);
			g_seed_count = 1;
		} else if (strcmp(argv[i], "--iters") == 0 && i + 1 < argc) {
			g_iters = (unsigned int)strtoul(argv[++i], NULL, 10);
		} else if (strcmp(argv[i], "--replay") == 0 && i + 2 < argc) {
			g_replay_mode = 1;
			g_replay_seed = (unsigned int)strtoul(argv[++i], NULL,
			    10);
			g_replay_n = (unsigned int)strtoul(argv[++i], NULL, 10);
		} else if (strcmp(argv[i], "--fault") == 0 && i + 1 < argc) {
			g_fault = argv[++i];
		} else if (strcmp(argv[i], "--cell") == 0 && i + 1 < argc) {
			g_cell = argv[++i];
		} else if (strcmp(argv[i], "--list-cells") == 0) {
			matrix_list_cells();
			return 0;
		} else {
			printf("URBSTRESS: usage: %s [--fuzz [--seed N] "
			    "[--iters M]] [--replay <seed> <n>] "
			    "[--fault double-complete] [--cell <id>] "
			    "[--list-cells]\n", argv[0]);
			return 2;
		}
	}
	if (g_fault != NULL && strcmp(g_fault, "double-complete") != 0) {
		printf("URBSTRESS: unknown fault scenario '%s'\n", g_fault);
		return 2;
	}

	if (osKernelInitialize() != osOK) {
		printf("URBSTRESS: kernel init failed\n");
		return 2;
	}
	/* the harness runs in the same priority band as the engine's drain thread
	 * and the controller's worker: a release from the drain does not preempt
	 * the harness (equal priority), so a semaphore wake-up means the drain has
	 * finished the whole delivery - the settlement the cells assert against.
	 * The window cell (B3.4) raises its killer task above this band. */
	attr.name = "urb-stress";
	/* the port's real stack is the pthread default; this value only sizes the
	 * FreeRTOS heap reservation, and the host heap here is the shared 64 KiB
	 * pool the sibling suites also draw from */
	attr.stack_size = 16384u;
	attr.priority = osPriorityNormal;
	t = osThreadNew(app_task, NULL, &attr);
	if (t == NULL) {
		printf("URBSTRESS: cannot create the application task\n");
		return 2;
	}
	if (osKernelStart() != osOK)
		return 2;
	/* the harness proper (environment bring-up included) ran on app_task; the
	 * scheduler returns here when it ended */
	return g_failures == 0 ? 0 : 1;
}
