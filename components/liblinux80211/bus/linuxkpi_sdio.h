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
	/* The read side's own account, and where a wake-up came from.  A card
	 * whose frames stop arriving has three candidate breaks (the engine's
	 * request, the card's interrupt, the handler's dispatch) and these
	 * counts say which of them stopped first. */
	unsigned int	ext_rx;		/* extended (CMD53) reads issued */
	unsigned int	ext_rx_bytes;	/* bytes those reads asked for */
	unsigned int	ext_rx_fail;	/* extended reads that failed */
	unsigned int	pending_reads;	/* CCCR pending-word reads */
	unsigned int	pending_nonzero;/* ones that reported work */
	unsigned int	dispatch_pending;/* handlers run off the pending word */
	unsigned int	dispatch_look;	/* handlers run off the card line */
	/* The wake chain's own two links, which the four counts above cannot
	 * separate: `card_notify` is the backend's interrupt upcall reaching
	 * the engine (the controller's ISR ran and called the card callback),
	 * and `irq_wakes` is the pump classifying a wake as that call rather
	 * than as its own backstop timeout.  A line that is armed but never
	 * fires leaves both at zero; a line that fires but whose wake is lost
	 * shows `card_notify` growing with `irq_wakes` flat. */
	unsigned int	card_notify;	/* ISR-to-engine card wake calls */
	unsigned int	irq_wakes;	/* pump wakes classified as card events */
};

/*
 * What the card itself says, read over the bus while the pump holds the claim.
 *
 * The host's counters say whether the line was armed and whether it fired; only
 * the card's own registers say whether it ever wanted to.  HIMR is the mask the
 * driver wrote (its HAL copy and the card's are two different things), HISR is
 * the pending event the card asserts DAT1 for, CCCR IEN is what sdio_claim_irq
 * wrote, CCCR INT PENDING is the function-level word the pump dispatches on, and
 * RX0_REQ_LEN is how many bytes the card's receive FIFO holds right now.  All of
 * these are reads; none clears the card's state.
 *
 * The probe runs at most once a second, under the claim, so it neither perturbs
 * the pump's timing materially nor touches the bus from a thread that does not
 * own it.
 */
struct lkpi_sdio_card_probe {
	unsigned int	ok;		/* reads that completed */
	unsigned int	fails;		/* reads refused by the bus */
	unsigned int	himr;		/* function 1, 0x14: the card's mask */
	unsigned int	hisr;		/* function 1, 0x18: pending events */
	unsigned int	cccr_ien;	/* function 0, 0x04: interrupt enable */
	unsigned int	cccr_pending;	/* function 0, 0x05: which function wants */
	unsigned int	rx_req_len;	/* function 1, 0x1c: FIFO bytes waiting */
	/* The card's transmit side, the two numbers the vendor's transmit path
	 * waits on before it writes a frame: the free page count per queue
	 * (function 1, 0x20; high/mid/low/public at bytes 0/2/4/6) and the
	 * outstanding-queue free space (function 1, 0x2a).  A card that reports
	 * zero here stalls every transmit before the SDIO write - the frame
	 * never reaches the FIFO, so nothing can appear on air. */
	unsigned int	txpg_hi;
	unsigned int	txpg_mid;
	unsigned int	txpg_lo;
	unsigned int	txpg_pub;
	unsigned int	oqt_free;
	/* Why the card can hold a frame it has already accepted: its MAC
	 * transmit enable (REG_CR bit 6) and its TX pause word (REG_TXPAUSE).
	 * Both are WLAN registers behind the IOREG device, read through the
	 * same claim as the FIFO state. */
	unsigned int	reg_cr;
	unsigned int	reg_txpause;
	/* The firmware's own state and the queue page allocation: a card whose
	 * firmware is not ready (or whose request-queue pages were never
	 * configured) accepts frames into the FIFO and never drains them, which
	 * looks exactly like "the transmit went nowhere". */
	unsigned int	reg_mcufwdl;	/* 0x0080: FW download/ready state */
	unsigned int	reg_rqpn;	/* 0x0200: request-queue page numbers */
	/* The firmware's host doorbell.  H2C commands travel through four
	 * message boxes; REG_HMETFR's bit n says box n has not been read by the
	 * firmware yet (the vendor polls exactly this: "MAC_0x1cc=0 means H2C
	 * done by FW").  The box contents say whether the host's write arrived
	 * at all, which is the difference between a firmware that ignores the
	 * host and a bus layer whose writes never land. */
	unsigned int	hmefr;		/* 0x01cc: H2C boxes the FW has not read */
	unsigned char	hmebox[16];	/* 0x01d0..0x01df: the four H2C boxes */
	unsigned char	hmebox_ex[16];	/* 0x01f0..0x01ff: their extension words */
	/* Two more MAC readbacks: the network type the driver set (MSR, the
	 * low half of REG_CR's network-type field) and the transmit DMA's own
	 * error word, which is where a FIFO overflow is reported next to the
	 * interrupt status. */
	unsigned int	reg_msr;	/* 0x0102: network type for port 0 */
	unsigned int	txdma_status;	/* 0x0210: TX DMA error status */
	/*
	 * The same card, sampled inside the join window at a rate that can see a
	 * transient: the free-page counts and the outstanding-queue space move
	 * down when the firmware takes a frame and back up when it lets go, so a
	 * one-second probe sees only the rest state.  The window keeps the range
	 * each field covered and how often the bits changed, not every sample.
	 */
	unsigned int	fast_period_ms;	/* the window's sampling period */
	unsigned int	fast_samples;	/* samples taken in the window */
	unsigned int	fast_txpg_min[4];/* hi/mid/lo/public minima */
	unsigned int	fast_txpg_max[4];/* ... and maxima */
	unsigned int	fast_oqt_min;
	unsigned int	fast_oqt_max;
	unsigned int	fast_hisr_or;	/* every HISR bit seen in the window */
	unsigned int	fast_hisr_changes;
	unsigned int	fast_rxl_max;	/* largest receive FIFO offer seen */
	unsigned int	fast_txpg_changed;/* a queue's page count moved */
	/* The firmware's doorbell sampled by the same window: OR of the "box not
	 * read" word, the last non-zero H2C box 0 word and whether one was ever
	 * seen.  A box the host wrote and the firmware never read is the one
	 * datum that tells "the firmware ignores the host" from "the write never
	 * landed", and it lives for a few milliseconds at each command. */
	unsigned int	fast_hmefr_or;
	unsigned int	fast_box0_last;
	unsigned int	fast_box0_seen;
	uint64_t	stamp_ns;	/* when the last complete probe finished */
};

void	lkpi_sdio_card_probe_set(bool on);
bool	lkpi_sdio_card_probe_enabled(void);
/* Snapshot of the last probe; zeroed when no probe has completed. */
void	lkpi_sdio_card_probe_get(struct lkpi_sdio_card_probe *out);
/*
 * The fast window: while on, the pump's probe runs at `period_ms` instead of
 * once a second and accumulates a range for the transmit-side counters (see
 * the fast_* fields above).  Switching it off keeps the accumulated range for
 * the case to print; switching it on again starts a new window.
 */
void	lkpi_sdio_card_probe_fast_set(bool on, unsigned int period_ms);
bool	lkpi_sdio_card_probe_fast_enabled(void);

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
/* Run a handler once per poll cycle even when the CCCR pending word is quiet
 * (cards that signal only through the DAT1 level). */
void	lkpi_sdio_pump_force_dispatch_set(bool on);
bool	lkpi_sdio_pump_force_dispatch_get(void);
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
