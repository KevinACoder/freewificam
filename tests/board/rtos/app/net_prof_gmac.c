/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   net_prof_gmac.c
 * @brief  The wired provider of the per-frame cost instrument.
 *
 * This is the only file that knows the DWC EQOS counter names and which of
 * them nests inside which. The nesting rules are the instrument's correctness
 * condition, so they are restated here as code comments next to the maths:
 *
 *   irq_ticks          every ISR entry to exit, whole
 *     irq_rx_sweep_ticks   ...of which the descriptor sweep ran in the ISR
 *   rx_walk_ticks      the sweep in WHICHEVER context ran it, whole
 *     irq_rx_copy_ticks    ...of which the staging copy inside the sweep
 *     irq_rx_cache_ticks   ...and the cache maintenance around it
 *   prep_ticks         pbuf_alloc + classify + pbuf_take, whole
 *     prep_copy_ticks      ...of which the frame memcpy
 *
 * So: NP_IRQ = irq - irq_sweep, NP_SWEEP = walk - irq_sweep - copy,
 * NP_COPY = copy + prep_copy, NP_FEED = (prep - prep_copy) + input.
 * Each tick is counted exactly once; the residual is whatever the wall
 * clock has left over and the reporter prints it.
 */

#include <stddef.h>
#include <string.h>

#include "dwc_eqos.h"
#include "ethnetif.h"

#include "net_prof.h"

/* One instance per arm; the case binds its port at start. Default 0 is the
 * compile-time default of the driver's own index space, not a guess. */
static unsigned g_port;

void net_prof_bind(unsigned port)
{
	g_port = port;
}

/* Unsigned difference that saturates at zero. The buckets are read without
 * stopping the data path, so a tick can land between two reads and make a
 * child momentarily larger than its parent; a negative difference here would
 * wrap into an absurd usize and poison the whole cell. */
static uint64_t dsub(uint64_t parent, uint64_t child)
{
	return (parent > child) ? (parent - child) : 0u;
}

void net_prof_take(struct net_prof_snap *s)
{
	struct dwc_eqos_prof prof;
	struct dwc_eqos_stats stats;
	struct dwc_eqos_mmc_stats mmc;
	struct dwc_eqos_diag diag;
	unsigned long polls = 0u, pool_fail = 0u, input_fail = 0u, fed = 0u;
	uint64_t prep_ticks = 0u, input_ticks = 0u;
	uint64_t prep_copy_ticks;
	unsigned long input_calls = 0u;

	memset(&prof, 0, sizeof(prof));
	memset(&stats, 0, sizeof(stats));
	memset(&mmc, 0, sizeof(mmc));
	memset(&diag, 0, sizeof(diag));

	dwc_eqos_get_prof(g_port, &prof);
	dwc_eqos_get_stats(g_port, &stats);
	dwc_eqos_get_mmc_stats(g_port, &mmc);
	dwc_eqos_get_diag(g_port, &diag);
	ethnetif_get_diag(&polls, &pool_fail, &input_fail, &fed);
	ethnetif_get_stack_prof(&prep_ticks, &input_ticks, &input_calls);
	prep_copy_ticks = ethnetif_get_prep_copy_ticks();

	/* Interrupt: the whole ISR minus the sweep it may have run. */
	s->ticks[NP_IRQ] = dsub(prof.irq_ticks, prof.irq_rx_sweep_ticks);

	/* Sweep: the walk, minus the ISR's share of it, minus the staging copy
	 * that happens inside the walk. */
	s->ticks[NP_SWEEP] = dsub(dsub(prof.rx_walk_ticks,
				       prof.irq_rx_sweep_ticks),
				  prof.irq_rx_copy_ticks);

	/* Copy: the driver's staging copy plus the bridge's pbuf copy. */
	s->ticks[NP_COPY] = prof.irq_rx_copy_ticks + prep_copy_ticks;

	/* Feed: pbuf bookkeeping outside the copy, plus the stack's own input
	 * path (ethernet_input and everything below it). */
	s->ticks[NP_FEED] = dsub(prep_ticks, prep_copy_ticks) + input_ticks;

	/* App: charged by the consumer thread. */
	s->ticks[NP_APP] = net_prof_app_ticks();

	s->irqs = stats.rx_irqs;
	s->sweeps = prof.rx_sweeps;
	s->walk_slots = prof.rx_walk_slots;
	s->polls = polls;
	s->rx_staged = prof.rx_frames;
	s->rx_dropped = stats.rx_dropped;
	s->stage_high_water = prof.rx_stage_high_water;

	/* Transmit leg: straight copy, no nesting to undo. tx_total_ticks is
	 * send_frame entry to exit (including the ring-full park), and the
	 * memcpy/flush buckets are subsets of it, so a reader can see how much
	 * of one send is the driver's own buffer work. */
	s->tx_frames = prof.tx_frames;
	s->tx_ticks = prof.tx_total_ticks;
	s->tx_memcpy = prof.tx_memcpy_ticks;
	s->tx_flush = prof.tx_flush_ticks;
	s->irq_tx = prof.irq_ent_tx;

	/* frames is the caller's to set: only it knows how many datagrams the
	 * application actually consumed, and that - not the driver's count -
	 * is the denominator the decomposition is per. */
}
