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

/* What U-Boot's `go` passed in x0 (start.S observed it after BSS clear).
 * Semantics unproven - logged as evidence only, never used. */
extern unsigned long long boot_arg_x0;

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

	/* 4. Tick pace: one line per second of ticks, count monotonic
	 * (the R2 acceptance ladder). */
	{
		uint64_t next = os_tick_get_count64() + board_tick_config.freq_hz;

		for (;;) {
			uint64_t now = os_tick_get_count64();

			if (now >= next) {
				board_log("tick: %u\n", (unsigned int)now);
				next += board_tick_config.freq_hz;
			}
		}
	}
	return 0;	/* unreachable: start.S parks if main returns */
}
