/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   net_prof.h
 * @brief  Per-frame receive-cost decomposition: the instrument, not the tuning.
 *
 * One measured cell (a netio phase, a transfer, a soak window) has a wall
 * duration and a frame count, so it has a cost per frame. That single number
 * says nothing about where the cost sits. This module splits it into five
 * buckets that do not overlap, plus a residual that is always printed:
 *
 *   NP_IRQ    the interrupt, entry to exit, MINUS any sweep it ran
 *   NP_SWEEP  the descriptor-ring walk, MINUS the staging copy inside it
 *   NP_COPY   the staging copy (DMA buffer -> stage slot) AND the bridge's
 *             pbuf copy (linear scratch -> pool pbuf)
 *   NP_FEED   pbuf allocation/classification and the stack's own ethernet
 *             input path (ethernet_input -> ip -> udp, including any mailbox
 *             post), i.e. everything between the bridge and the socket
 *   NP_APP    the application's own receive call that consumed the frame
 *   residual  wall - (the five above); scheduling, service threads, idle and
 *             timer quantisation live here. Reporting it is mandatory: a
 *             decomposition that silently swallows the remainder is a
 *             decoration, not a measurement.
 *
 * THE OVERLAP RULE IS THE WHOLE INSTRUMENT. The driver charges the sweep in
 * whichever context runs it (ISR or RX thread) and charges the staging copy
 * *inside* that sweep; so NP_IRQ must subtract the ISR-side sweep and
 * NP_SWEEP must subtract the copy. A provider that forgets this produces
 * numbers that look plausible and double-count ~half the receive path.
 *
 * The header is deliberately free of driver types. A wired arm binds it with
 * net_prof_gmac.c; a future wireless arm writes its own provider over the
 * same slots, and the report, the parser and the closure check are reused
 * unchanged.
 */

#ifndef RTOS_APP_NET_PROF_H
#define RTOS_APP_NET_PROF_H

#include <stdint.h>

enum net_prof_slot {
	NP_IRQ = 0,	/* interrupt outside the sweep it may have run */
	NP_SWEEP,	/* the walk, outside the copy it contains */
	NP_COPY,	/* staging copy + bridge pbuf copy */
	NP_FEED,	/* pbuf prep outside the copy + ethernet_input */
	NP_APP,		/* the application's receive call */
	NP_SLOTS
};

/* A snapshot. Every ticks[] cell is cumulative since boot (or since the
 * provider last reset), so a cell is the difference of two snapshots - the
 * same rule the layered loss counters follow, and for the same reason: a
 * phase must not carry the previous phase's traffic. */
struct net_prof_snap {
	uint64_t ticks[NP_SLOTS];
	uint64_t frames;	/* frames the application consumed: the
				 * denominator, because the app is the consumer
				 * whose cost we are decomposing */
	uint64_t app_empty;	/* application receive calls that found no
				 * data: proves the app bucket excludes waiting */
	uint64_t app_charged;	/* receive calls whose duration was pure work
				 * and therefore entered the app bucket. In steady
				 * state this is only a sample, so the bucket is
				 * reported per charged call and scaled to the
				 * frame count - see net_prof_report. */
	uint64_t irqs;		/* interrupt entries in the cell */
	uint64_t sweeps;	/* eqos_rx_complete calls (any context) */
	uint64_t walk_slots;	/* descriptor probes the walk made */
	uint64_t polls;		/* bridge drains */
	uint64_t rx_staged;	/* frames the driver staged */
	uint64_t rx_dropped;	/* frames the driver dropped (ring full) */
	uint64_t stage_high_water; /* peak staged frames waiting for a reader */
};

/* Bind the provider to one port instance (the wired arms bind GMAC 1). A
 * provider that has only one instance may ignore it. */
void net_prof_bind(unsigned port);

/* Fill a snapshot from the bound port's counters. One implementation per
 * driver family; the caller does not care which one is linked. */
void net_prof_take(struct net_prof_snap *s);

/* Charge the application bucket. The consumer calls this around its receive
 * call with the ticks that call took and, when it consumed a frame, with the
 * tick of the frame's data being available. Two calls are accepted in either
 * order; only the sum is used. */
void net_prof_app_charge(uint64_t ticks);
uint64_t net_prof_app_ticks(void);
void net_prof_app_reset(void);

/* Report the cell (b - a) normalised per frame. Emits two anchors:
 *
 *   netio: prof  tag=<t> frames=<n> wall_x10=<tenths of a us per frame> ...
 *   netio: profx tag=<t> irqs= sweeps= walk_slots= polls= staged= rx_drop= ...
 *
 * `wall_us` is the cell's wall duration in microseconds, from the same clock
 * the buckets use (rtos_clock_us() on the board tier). */
void net_prof_report(const char *tag, const struct net_prof_snap *a,
		     const struct net_prof_snap *b, uint64_t wall_us);

#endif /* RTOS_APP_NET_PROF_H */
