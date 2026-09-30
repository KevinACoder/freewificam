/*
 * @file   main.c
 * @brief  The bare-metal C entry (R1).
 *
 * This function IS the milestone's shape proof: start.S has set up stack,
 * vectors, BSS and the identity-mapped MMU with caches on, then called here.
 * Everything from the first line on is ordinary C.
 *
 * Sequence (board anchors in SPEC v3 §4/R1, all whole-line):
 *   1. re-program the UART FIRST (a UART inherited from the loader has
 *      unreliable THRE semantics - banner bursts get out, then bytes drop);
 *   2. the anchor lines: C-main proof, EL/counter, MMU state, cache API
 *      selftest, printf selftests, fragment-concatenation protocol;
 *   3. the alive loop: one line per second off the generic counter.
 */

#include <stddef.h>
#include <stdint.h>

#include "cache.h"
#include "gicv3.h"
#include "log.h"
#include "mmu.h"
#include "minilibc.h"
#include "tick.h"
#include "uart.h"
#include "cmsis_os2.h"

#ifdef CONFIG_GDB_STUB
#include "gdb_glue.h"
#endif

/* What U-Boot's `go` passed in x0 (start.S observed it after BSS clear).
 * Semantics unproven - logged as evidence only, never used. */
extern unsigned long long boot_arg_x0;

#ifndef CONFIG_CMSIS2_VERSION
#define CONFIG_CMSIS2_VERSION "baremetal"
#endif

static uint64_t read_cntfrq(void)
{
	uint64_t v;

	__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

static uint64_t read_sctlr(void)
{
	uint64_t v;

	__asm__ __volatile__("mrs %0, sctlr_el1" : "=r"(v));
	return v;
}

static uint64_t read_currentel(void)
{
	uint64_t v;

	__asm__ __volatile__("mrs %0, CurrentEL" : "=r"(v));
	return v;
}

/* Exercise the cache maintenance API on real buffers: flush the source,
 * invalidate the destination, copy, flush+invalidate again, compare. */
static void cachetest(void)
{
	static unsigned char buf_a[128];
	static unsigned char buf_b[128];
	unsigned int i;

	for (i = 0U; i < sizeof(buf_a); i++) {
		buf_a[i] = (unsigned char)(i * 7U + 3U);
	}
	cache_dcache_flush((uintptr_t)buf_a, sizeof(buf_a));
	cache_dcache_invalidate((uintptr_t)buf_b, sizeof(buf_b));
	(void)memcpy(buf_b, buf_a, sizeof(buf_a));
	cache_dcache_flush_invalidate((uintptr_t)buf_b, sizeof(buf_b));

	if (memcmp(buf_a, buf_b, sizeof(buf_a)) == 0) {
		board_log("cachetest: flush/inval/copy ok\n");
	} else {
		board_log("cachetest: FAIL (copy mismatch)\n");
	}
}

int main(void)
{
	/* 1. The polled console speaks only after a full re-program. */
	uart_early_init();

#ifdef CONFIG_GDB_STUB
	/* The stub owns the dbgport before anything can trap; its setup
	 * prints and the ready banner follow. */
	gdb_glue_init();
#endif

	/* 2. Anchors. */
	board_log("main: hello from C (freewebcam baremetal)\n");

	board_log("board: currentel=0x%x cntfrq=%u\n",
		  (unsigned int)(read_currentel() & 0xcu),
		  (unsigned int)read_cntfrq());
	board_log("board: go x0=%llx\n", boot_arg_x0);

	/* mmu_enable() ran from start.S; verify it took effect instead of
	 * parroting success. */
	{
		uint64_t sctlr = read_sctlr();
		const uint64_t mci = (1ULL << 0) | (1ULL << 2) | (1ULL << 12);

		if ((sctlr & mci) == mci) {
			board_log("mmu: enabled ps=40bit t0sz=16 cache=mci\n");
		} else {
			board_log("mmu: NOT enabled sctlr=%llx\n", sctlr);
		}
	}

	cachetest();

	board_log("selftest: int=%d uint=%u hex=%x char=%c str=%s\n",
		  -42, 4294967295u, 0xdeadbeefu, 'X', "ok");
	board_log("selftest: width [%4d] zero [%05d]\n", 42, 42);

	/* Fragment protocol: three calls, one stamped line on the wire. */
	board_log("concat: frag1");
	board_log(" frag2");
	board_log(" done\n");

	/* 3. Interrupts: GIC bring-up + generic-timer tick (R2). */
	gicv3_init();
	if (OS_Tick_Setup(board_tick_config.freq_hz, 0) == 0) {
		board_log("tick: source=%s irq=%u freq=%uHz\n",
			  board_tick_config.source == OS_TICK_SOURCE_VIRT ?
				  "virt" : "phys_ns",
			  (unsigned)OS_Tick_GetIRQn(),
			  (unsigned)board_tick_config.freq_hz);

		/* Stamp switches to the tick count from the next line on. */
		board_log_stamp_use_tick();
		board_log("stamp: switched to tick\n");

		/* Everything is up: unmask IRQs (start.S masked DAIF via the
		 * eret-inherited SPSR; the tick starts counting now). */
		__asm__ __volatile__("msr daifclr, #2" ::: "memory");
		__asm__ __volatile__("isb" ::: "memory");
	} else {
		board_log("tick: SETUP FAILED\n");
	}

	/* 4. R4: CMSIS2 time-subset anchors (SPEC v3 §4/R4). osDelay's unit is
	 * the tick (CMSIS semantics): at the default 100 Hz, osDelay(100) is
	 * one second and the anchor literals below are tick-faithful. */
	{
		char kid[32];
		osVersion_t ver;

		(void)osKernelGetInfo(&ver, kid, sizeof(kid));
		board_log("cmsis2: kernel state=%s tick=%uHz version=%s\n",
			  osKernelGetState() == osKernelRunning ? "running" : "?",
			  (unsigned)osKernelGetTickFreq(),
			  CONFIG_CMSIS2_VERSION);

		/* First second boundary reported through the CMSIS counter. */
		while (osKernelGetTickCount() < board_tick_config.freq_hz) {
			__asm__ __volatile__("wfi" ::: "memory");
		}
		board_log("cmsis2: tickcount=%u\n",
			  (unsigned)osKernelGetTickCount());

		(void)osDelay(board_tick_config.freq_hz);
		board_log("cmsis2: osDelay(%u) returned, tickcount=%u\n",
			  (unsigned)board_tick_config.freq_hz,
			  (unsigned)osKernelGetTickCount());
	}

	/* 5. Steady state: osDelay-driven one-line-per-second reporting (the
	 * wait itself is the WFI in the shim, woken by the tick interrupt;
	 * even spacing is the R4 side-evidence). The wait runs in 1-tick
	 * granules so the gdb build polls the console between them - the
	 * gdb stub has no RX interrupt to wake a longer WFI. */
	{
		const uint32_t granule_ticks = 1U;
		uint32_t until_print = board_tick_config.freq_hz;

		for (;;) {
#ifdef CONFIG_GDB_STUB
			gdb_poll();
#endif
			(void)osDelay(granule_ticks);
			until_print -= granule_ticks;
			if (until_print == 0U) {
				until_print = board_tick_config.freq_hz;
				board_log("cmsis2: tickcount=%u systimer=%u\n",
					  (unsigned)osKernelGetTickCount(),
					  (unsigned)osKernelGetSysTimerCount());
			}
		}
	}
	return 0;	/* unreachable: start.S parks if main returns */
}
