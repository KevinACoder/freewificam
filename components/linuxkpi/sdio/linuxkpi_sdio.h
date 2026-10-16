/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_sdio.h
 * @brief   The SDIO engine's own entry points: bring the card up, reach the
 *          function objects, drive one interrupt-poll cycle, read the counters.
 *
 * @design ports & adapters, the core side: the engine (linuxkpi_sdio.c) is the
 *         adapter-neutral core of the SDIO layer. It speaks Linux semantics to
 *         its callers (the interface in linux/mmc/sdio_func.h) and reaches the
 *         card through the narrow seam (lkpi_sdio_backend.h) only. This header
 *         publishes what the bring-up and the tests need; a driver needs none of
 *         it, since a driver only ever sees the interface.
 *         Shape reference: components/linuxkpi/usb/linuxkpi_usb.h:1-40.
 * @date    12.10.2026
 */

#ifndef LKPI_SDIO_H
#define LKPI_SDIO_H

#include "linuxkpi_core.h"

#include <linux/mmc/sdio_func.h>

/*
 * What the engine did, as counters a test or a bring-up can read. The split is
 * the seam's own vocabulary (direct and extended transfers, claim traffic) plus
 * the pump's (polls, callbacks), so a run can be checked against the operations
 * the caller asked for rather than against the card's behaviour.
 */
struct lkpi_sdio_stats {
	unsigned int	cards;		/* cards brought up */
	unsigned int	drivers;	/* drivers registered */
	unsigned int	probes;		/* probes that claimed a function */
	unsigned int	claims;		/* claim acquisitions */
	unsigned int	releases;	/* claim releases */
	unsigned int	io_direct;	/* single-register transfers */
	unsigned int	io_extended;	/* buffered transfers */
	unsigned int	pump_polls;	/* interrupt-poll cycles that queried the card */
	unsigned int	callbacks;	/* IRQ handler invocations */
	unsigned int	irq_claims;	/* function IRQs claimed */
	unsigned int	errors;		/* transfers or polls that failed */
};

/*
 * Bring the card up and hand it to the registered drivers: enumerate through the
 * backend, build the interface objects, then match and probe. Idempotent - a
 * second call does nothing. A missing backend or a failed bring-up leaves the
 * engine not ready and says so on the console.
 */
void	lkpi_sdio_start(void);

/* non-zero once a card is up and its function objects exist */
int	lkpi_sdio_ready(void);

/* the function object of the given function number, or NULL when the card does
 * not carry it (function 0 is the card itself and is always present once up) */
struct sdio_func	*lkpi_sdio_func(unsigned int num);

/* function 0's object, the card's own register window */
struct sdio_func	*lkpi_sdio_func0(void);

/* whether the calling thread holds the bus claim right now. The interrupt pump
 * holds it while it runs a handler, which is the contract a handler relies on
 * when it touches the card without claiming. */
bool	lkpi_sdio_claim_held_by_current(void);

/* run one interrupt-poll cycle and return once it finished. The pump thread
 * does this on its own timer; this entry makes a test's cycle deterministic. */
void	lkpi_sdio_pump_kick(void);

/*
 * The card interrupt (DAT1). The backend wires the card's own upcall to
 * lkpi_sdio_pump_notify(); that entry runs in interrupt context and only
 * wakes the pump - the pending word is read and the driver's handler runs on
 * the pump thread, under the claim, exactly as in the polling shape.
 *
 * lkpi_sdio_pump_irq_set()/get() are the runtime switch: on means card events
 * wake the pump (the wait becomes a backstop against a lost line), off is the
 * polling cadence this line always ran. Refused with the seam's error when the
 * installed backend carries no card-interrupt path.
 */
void	lkpi_sdio_pump_notify(void);
int	lkpi_sdio_pump_irq_set(bool on);
bool	lkpi_sdio_pump_irq_get(void);

/*
 * The wake-chain knobs (the align round's second arm, independent of the card
 * interrupt). prio: false (the default) leaves the pump below the normal band,
 * where the consumer threads always preempt it; true moves it up to compete for
 * the bus. drain: false (the default) dispatches once per cycle; true keeps
 * dispatching inside one claim while the card still reports pending work.
 */
void	lkpi_sdio_pump_prio_set(bool normal);
bool	lkpi_sdio_pump_prio_get(void);
void	lkpi_sdio_pump_drain_set(bool on);
bool	lkpi_sdio_pump_drain_get(void);

/*
 * The pump's cadence: FIXED holds the idle period between cycles (the polled
 * host's shape); ADAPTIVE shortens the period after a cycle that moved bytes
 * and lengthens it again after one that moved none, the shape Linux' polled-host
 * IRQ thread uses (drivers/mmc/core/sdio_irq.c sdio_irq_thread()). The mode is a
 * runtime knob so one boot can measure both under the same RF; default FIXED.
 */
#define	LKPI_SDIO_PUMP_MODE_FIXED	0u
#define	LKPI_SDIO_PUMP_MODE_ADAPTIVE	1u

void	lkpi_sdio_pump_mode_set(unsigned int mode);
unsigned int	lkpi_sdio_pump_mode_get(void);

/*
 * The fairness yield: when on, a pump cycle that would re-take the bus while a
 * driver thread is already parked on the claim skips that turn instead, so the
 * parked caller gets the bus rather than queueing behind the pump again. Off by
 * default - the pump's own cadence owns the bus, which is the shape every
 * earlier measurement ran under. It is a runtime knob so one boot can measure
 * both.
 */
void	lkpi_sdio_pump_yield_set(bool on);
bool	lkpi_sdio_pump_yield_get(void);

/* the adaptive controller's current period, milliseconds (the idle period when
 * the mode is fixed) */
unsigned int	lkpi_sdio_pump_period_get(void);

/*
 * The bus identity the transfers above actually ran on: the card clock the
 * driver negotiated, the data-line width, and the block size the data function
 * transfers in. The bring-up line reports these once, which is not enough for a
 * measurement - a throughput phase has to say which tier it ran at, or two
 * numbers from different boots are not comparable.
 */
struct lkpi_sdio_bus_info {
	uint32_t	clock_hz;
	unsigned int	bus_width;	/* data lines */
	unsigned int	blk_size;	/* function 1's current block size, 0 if none */
	unsigned int	blk_max;	/* that function's ceiling */
};

void	lkpi_sdio_bus_info_get(struct lkpi_sdio_bus_info *out);

void	lkpi_sdio_stats_get(struct lkpi_sdio_stats *out);

#endif /* LKPI_SDIO_H */
