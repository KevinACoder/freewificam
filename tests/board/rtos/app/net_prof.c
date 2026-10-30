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

	/* The transmit leg gets its own line for the same reason the layer
	 * counters do: one line per leg is short enough to survive the log
	 * message limit (ULOG_MAX_MESSAGE_LENGTH=256) instead of being cut at
	 * the tail, where the interesting number would sit. The denominator is
	 * the frames the driver accepted, not the ones the app asked for -
	 * a send that never reached send_frame must not dilute its cost. */
	{
		uint64_t txf = b->tx_frames - a->tx_frames;

		log_emit(ULOG_ALWAYS_LEVEL,
			 "netio: proftx tag=%s tx_frames=%lu tx_x10=%lu "
			 "tx_memcpy_x10=%lu tx_flush_x10=%lu irq_tx=%lu\n",
			 tag, (unsigned long)txf,
			 per_frame_x10(b->tx_ticks - a->tx_ticks, txf),
			 per_frame_x10(b->tx_memcpy - a->tx_memcpy, txf),
			 per_frame_x10(b->tx_flush - a->tx_flush, txf),
			 (unsigned long)(b->irq_tx - a->irq_tx));
	}
}

/* --- measurement clock: PMCCNTR_EL0 ---------------------------------------
 *
 * See net_prof.h for why a cell carries cycles as well as microseconds. The
 * probe is deliberately small and self-checking: enable the cycle counter,
 * read PMCR back, and only claim success when the enable bit stuck. A core
 * whose EL2 traps PMU accesses (HCR_EL2.TPM) faults here instead of returning
 * a wrong number, which is why the case prints its own anchor immediately
 * before calling this and why CONFIG_CPU_PROBE exists as the off switch. */
#if defined(__aarch64__) && (!defined(CONFIG_CPU_PROBE) || CONFIG_CPU_PROBE)
#define NET_PROF_CPU_PROBE 1
#else
#define NET_PROF_CPU_PROBE 0
#endif

static int g_cpu_probe_ok;

#if NET_PROF_CPU_PROBE
static inline uint64_t read_pmcr(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, pmcr_el0" : "=r"(v));
	return v;
}

static inline uint64_t read_pmccntr(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, pmccntr_el0" : "=r"(v));
	return v;
}
#endif

int net_prof_cpu_probe_init(void)
{
#if NET_PROF_CPU_PROBE
	uint64_t v = read_pmcr();

	/* E: enable the counters; C: reset the cycle counter so the first
	 * cell does not inherit the boot's count; LC: ask for the 64-bit
	 * cycle counter (RAO on a core that has no 32-bit option). */
	v |= (1ull << 0) | (1ull << 2) | (1ull << 6);
	__asm__ volatile("msr pmcr_el0, %0" : : "r"(v));
	/* PMCNTENSET_EL0 bit 31 = cycle counter enable. */
	__asm__ volatile("msr pmcntenset_el0, %0" : : "r"(1ull << 31));
	__asm__ volatile("isb");

	g_cpu_probe_ok = ((read_pmcr() & 1ull) != 0ull) ? 1 : 0;
	return g_cpu_probe_ok ? 0 : -1;
#else
	g_cpu_probe_ok = 0;
	return -1;
#endif
}

uint64_t net_prof_cpu_cycles(void)
{
#if NET_PROF_CPU_PROBE
	if (!g_cpu_probe_ok)
		return 0u;
	return read_pmccntr();
#else
	return 0u;
#endif
}

int net_prof_cpu_probe_ok(void)
{
	return g_cpu_probe_ok;
}

void net_prof_cpu_cell(const char *tag, uint64_t cycles, uint64_t ticks,
		       uint64_t frames)
{
	unsigned long cyc_per_frame = frames ? (unsigned long)(cycles / frames) : 0ul;
	/* The gtimer runs at 24 MHz, so MHz = cycles / (ticks / 24), in
	 * hundredths: cycles * 24 * 100 / ticks. The intermediate is bounded
	 * by ~2.9e13 for a 6 s cell at 2 GHz - inside uint64_t. */
	unsigned long mhz_x100 = ticks ? (unsigned long)((cycles * 24ull * 100ull) / ticks) : 0ul;

	log_emit(ULOG_ALWAYS_LEVEL,
		 "netio: cpu tag=%s cycles=%lu cyc_per_frame=%lu mhz_x100=%lu "
		 "frames=%lu\n",
		 tag, (unsigned long)cycles, cyc_per_frame, mhz_x100,
		 (unsigned long)frames);
}
