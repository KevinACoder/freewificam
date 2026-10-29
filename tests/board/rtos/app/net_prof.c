/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   net_prof.c
 * @brief  Driver-agnostic half of the per-frame cost instrument.
 *
 * The provider (net_prof_gmac.c today, a wireless provider later) owns the
 * counters and knows which one sits inside which. This file owns the two
 * things that must be identical on every arm: the arithmetic of a cell
 * (b - a, divided by the frames the application consumed) and the closure
 * check that makes the split falsifiable.
 *
 * Nothing here reads a register, allocates, or takes a lock: it runs at cell
 * boundaries only, never on the data path.
 */

#include <stddef.h>

#include "log.h"
#include "net_prof.h"

/* The application bucket. One consumer thread charges it, and a cell is only
 * closed after that thread has been quiesced, so a plain accumulator is
 * enough - no atomics, no critical section on the receive path. */
static uint64_t g_app_ticks;

void net_prof_app_charge(uint64_t ticks)
{
	g_app_ticks += ticks;
}

uint64_t net_prof_app_ticks(void)
{
	return g_app_ticks;
}

void net_prof_app_reset(void)
{
	g_app_ticks = 0u;
}

/* One slot as tenths of a microsecond per frame. The tick base is the 24 MHz
 * system counter, so ticks * 10 / 24 is 0.1 us units. Integer maths only: the
 * board builds with -mgeneral-regs-only and has no FPU to print a double. */
static unsigned long per_frame_x10(uint64_t dtick, uint64_t frames)
{
	if (frames == 0u)
		return 0ul;
	return (unsigned long)((dtick * 10u) / 24u / frames);
}

/* Same, but the input is already microseconds (the wall clock), not ticks.
 * Keeping the two conversions apart is the whole point: an earlier version
 * fed microseconds through the tick helper and divided by 24 twice, which
 * printed a wall 24x too small and made every closure verdict nonsense. */
static unsigned long per_us_x10(uint64_t us, uint64_t frames)
{
	if (frames == 0u)
		return 0ul;
	return (unsigned long)((us * 10u) / frames);
}

void net_prof_report(const char *tag, const struct net_prof_snap *a,
		     const struct net_prof_snap *b, uint64_t wall_us)
{
	uint64_t frames = b->frames - a->frames;
	/* The application bucket is a sampled measurement: only receives that
	 * found a datagram already queued can be charged without absorbing the
	 * wait. Scale the sample to the frame count so the five buckets cover
	 * the whole cell, and print the sample size so the reader can judge the
	 * estimate instead of trusting it. */
	uint64_t app_ticks = b->ticks[NP_APP] - a->ticks[NP_APP];
	uint64_t app_samples = b->app_charged - a->app_charged;
	uint64_t app_ticks_scaled = app_samples ?
		(app_ticks / app_samples) * frames : 0u;

	unsigned long irq = per_frame_x10(b->ticks[NP_IRQ] - a->ticks[NP_IRQ], frames);
	unsigned long sweep = per_frame_x10(b->ticks[NP_SWEEP] - a->ticks[NP_SWEEP], frames);
	unsigned long copy = per_frame_x10(b->ticks[NP_COPY] - a->ticks[NP_COPY], frames);
	unsigned long feed = per_frame_x10(b->ticks[NP_FEED] - a->ticks[NP_FEED], frames);
	unsigned long app = per_frame_x10(app_ticks_scaled, frames);
	unsigned long app_call = per_frame_x10(app_ticks, app_samples);
	unsigned long wall = per_us_x10(wall_us, frames);
	long sum = (long)irq + (long)sweep + (long)copy + (long)feed + (long)app;
	long other = (long)wall - sum;
	long sum_pct = 0;

	/* Closure is the instrument's own verdict on itself: the five buckets
	 * plus the residual are the wall by construction, so the honest
	 * question is whether the FIVE agree with the wall. A large positive
	 * excess means two buckets charged the same work; a large negative
	 * remainder means the cost lives somewhere that is not instrumented
	 * yet (a service thread, a second pass over the ring, idle). Both are
	 * printed, neither is hidden. */
	if (wall > 0L)
		sum_pct = (sum * 100L) / wall;

	log_emit(ULOG_ALWAYS_LEVEL,
		 "netio: prof tag=%s frames=%lu wall_x10=%lu irq_x10=%lu "
		 "sweep_x10=%lu copy_x10=%lu feed_x10=%lu app_x10=%lu "
		 "other_x10=%ld sum_pct=%ld app_call_x10=%lu app_samples=%lu\n",
		 tag, (unsigned long)frames, wall, irq, sweep, copy, feed,
		 app, other, sum_pct, app_call,
		 (unsigned long)app_samples);

	/* Counts are printed as absolute deltas. Dividing them by the frame
	 * count looked tidy and lied: an event that happens once per fourteen
	 * frames truncates to zero, which reads as "no interrupts at all". */
	log_emit(ULOG_ALWAYS_LEVEL,
		 "netio: profx tag=%s irqs=%lu sweeps=%lu walk_slots=%lu "
		 "polls=%lu staged=%lu rx_drop=%lu stage_hw=%lu "
		 "app_empty=%lu\n",
		 tag,
		 (unsigned long)(b->irqs - a->irqs),
		 (unsigned long)(b->sweeps - a->sweeps),
		 (unsigned long)(b->walk_slots - a->walk_slots),
		 (unsigned long)(b->polls - a->polls),
		 (unsigned long)(b->rx_staged - a->rx_staged),
		 (unsigned long)(b->rx_dropped - a->rx_dropped),
		 (unsigned long)b->stage_high_water,
		 (unsigned long)(b->app_empty - a->app_empty));
}
