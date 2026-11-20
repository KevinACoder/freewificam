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

/* The backend's guard: 1 when the caller holds the claim, 0 (and counted)
 * otherwise.  A non-zero count after a boot is a serialization defect. */
/* The guard is off until the measured window opens: the engine's own bring-up
 * reads run before a driver owns the bus, and counting them would report a
 * violation that is not one. */
void	lkpi_sdio_claim_guard_set(bool on);
int	lkpi_sdio_claim_check(int extended);
unsigned int	lkpi_sdio_claim_violations(void);
/* The thread the claim is held by, or NULL: the freeze watchdog names the
 * holder's own last instruction, which is not the pump when the holder is a
 * vendor thread. */
void	*lkpi_sdio_claim_owner_thread(void);
const char	*lkpi_sdio_claim_violation_thread(void);
const char	*lkpi_sdio_claim_violation_owner(void);
unsigned int	lkpi_sdio_claim_violation_progress(void);
unsigned int	lkpi_sdio_claim_violation_tick(void);

/* who holds the bus claim right now, and for how long. "-" when free; the
 * episode recorder uses these to separate "nobody is reading" from "the card
 * has nothing to give". */
const char	*lkpi_sdio_claim_owner(void);
unsigned int	lkpi_sdio_claim_held_ms(void);

/* The pump's current stage: "-" idle, "claim", "pending", "handler",
 * "release". A claim held with stage "handler" is a driver handler that did
 * not return; "pending" is a card-status read that did not complete. */
const char	*lkpi_sdio_pump_stage(void);

/* The transfer the claim holder is inside: "-", "c52r", "c52w", "ext-r",
 * "ext-w", "done". A holder stuck in one of these names the bus operation. */
const char	*lkpi_sdio_claim_op(void);

/* The pump thread's CMSIS-RTOS2 state: running/ready/blocked/terminated/error. */
const char	*lkpi_sdio_pump_state(void);

/* The pump thread's kernel handle (the value osThreadNew returned), so a
 * sampler can ask what this thread was executing; NULL before the pump
 * starts. */
void	*lkpi_sdio_pump_thread(void);

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
/* 0 = below normal, 1 = normal, 2 = above normal; the observation round's
 * starvation arm uses 2 without changing the default. */
void	lkpi_sdio_pump_prio_level_set(unsigned int level);
unsigned int	lkpi_sdio_pump_prio_level_get(void);
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
	unsigned int	blk_max;	/* that function's ceiling, 0 if unset */
	unsigned int	max_req_size;	/* the largest single transfer the host takes */
};

void	lkpi_sdio_bus_info_get(struct lkpi_sdio_bus_info *out);

void	lkpi_sdio_stats_get(struct lkpi_sdio_stats *out);

#ifdef CONFIG_LKPI_CENSUS
/*
 * The receive presentation as the engine saw it: the card's offer (the
 * SDIO_REG_RX0_REQ_LEN answer), the read issued for it, and the interrupt bits.
 * A stall that leaves the pump running and the pool healthy is either the card
 * not offering work (`offer_zero` growing) or the host not asking
 * (`offer_reads` flat); these counters are what separates the two.
 */
struct lkpi_sdio_rx_stats {
	uint32_t	offer_reads;
	uint32_t	offer_zero;
	uint64_t	offer_bytes;
	uint32_t	read_ok;
	uint32_t	read_short;
	uint32_t	read_fail;
	uint32_t	hisr_rxreq;
	uint32_t	hisr_aval;
	uint32_t	hisr_rxerr;
	uint32_t	hisr_rxfovw;
	uint64_t	last_ok_ns;	/* completion of the last successful read */
	uint64_t	now_ns;		/* the clock the two above are on */
};

void	lkpi_sdio_rx_stats_get(struct lkpi_sdio_rx_stats *out);

/*
 * The transfer trace (diagnostic images): the operation the engine is inside
 * right now and a short ring of the ones that completed.  A frozen bring-up
 * says which claim stage the pump is in; this says which transfer inside that
 * stage never came back - its kind, its address, its length and its block
 * count.  The site string is the innermost layer the transfer reached
 * ("c52"/"blk"/"byt" in this file, then whatever the backend and the host
 * driver publish), so a frozen read can be placed without a debugger.
 *
 * One writer at a time by construction: every transfer runs under the bus
 * claim.  The reader may see a torn ring entry during a dump; entries carry a
 * sequence number so a torn one can be skipped rather than misread.
 */
enum lkpi_sdio_op_kind {
	LKPI_SDIO_OP_C52R = 0,
	LKPI_SDIO_OP_C52W,
	LKPI_SDIO_OP_BLK_R,
	LKPI_SDIO_OP_BLK_W,
	LKPI_SDIO_OP_BYT_R,
	LKPI_SDIO_OP_BYT_W,
};

struct lkpi_sdio_op {
	uint32_t	seq;		/* 0 = empty, else the write order */
	uint32_t	t0_us;
	uint32_t	dur_us;
	uint32_t	addr;
	uint32_t	bytes;
	uint16_t	blocks;		/* block mode only */
	uint8_t		kind;		/* enum lkpi_sdio_op_kind */
	int8_t		ret;
};

struct lkpi_sdio_diag {
	const char	*site;		/* the innermost layer reached */
	uint32_t	progress;	/* completed transfers since boot */
	uint32_t	ops;		/* ring entries written since boot */
	uint32_t	inflight_t0_us;
	uint32_t	inflight_addr;
	uint32_t	inflight_bytes;
	uint32_t	inflight_blocks;
	uint8_t		inflight_valid;
	uint8_t		inflight_kind;
	uint8_t		inflight_func;
};

void	lkpi_sdio_site_set(const char *site);
void	lkpi_sdio_diag_get(struct lkpi_sdio_diag *out);
/* last `max` completed transfers, oldest first; returns the count written */
unsigned int	lkpi_sdio_op_dump(struct lkpi_sdio_op *out, unsigned int max);
#else	/* !CONFIG_LKPI_CENSUS */
static inline void
lkpi_sdio_site_set(const char *site)
{

	(void)site;
}
#endif	/* CONFIG_LKPI_CENSUS */

#endif /* LKPI_SDIO_H */
