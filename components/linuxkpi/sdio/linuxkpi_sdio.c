/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_sdio.c
 * @brief   The SDIO engine: the Linux SDIO function API over the narrow backend
 *          seam.
 *
 * @design ports & adapters, the core side. The engine speaks Linux semantics
 *         (the interface in linux/mmc/sdio_func.h) and reaches the card through
 *         lkpi_sdio_backend.h only - six operations and one card record. It owns
 *         exactly the semantics that are the interface's, not the transport's:
 *         the exclusive bus claim, the function power-up sequence, the transfer
 *         splitter, the block-size bookkeeping, the interrupt pump and the
 *         driver registry. Shape references:
 *           - components/linuxkpi/usb/linuxkpi_usb.c:88-172 (the driver registry
 *             and the module-section walker this engine's registry follows);
 *           - components/linuxkpi/usb/linuxkpi_usb.c:2027-2062 (the pump thread
 *             and the idempotent start);
 *           - the interface's own bodies, which this file transcribes in
 *             behaviour and rewrites in code: the claim, the function enable
 *             sequence and the block-size rules come from the SDIO core, and the
 *             transfer splitter is the core's own helper (the multi-block path
 *             bounded by the host's block count and the nine-bit extended
 *             transfer's 511-block ceiling, the remainder in byte mode).
 *
 *         Two things the interface leaves to the port and this file therefore
 *         decides, both recorded in the component's DIVERGENCES file:
 *           - the pump thread is created once, at bring-up, and never torn down.
 *             It queries nothing while no handler is registered, so releasing the
 *             last IRQ stops delivery without a thread teardown whose races would
 *             cost more than the idle poll does;
 *           - a transfer whose ceiling is unstated (a function whose CIS ceiling
 *             was never read, reported as zero) is treated as "no ceiling" rather
 *             than as a zero-byte ceiling, which is what keeps the byte-mode chunk
 *             size from collapsing.
 * @date    12.10.2026
 */

#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"

#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>

#include "lkpi_sdio_backend.h"
#include "linuxkpi_sdio.h"

#ifdef CONFIG_LKPI_CENSUS
#include "lkpi_census.h"
#endif

/* ------------------------------------------------------------------ */
/* constants and state                                                 */
/* ------------------------------------------------------------------ */

/* registered drivers (the USB engine's table depth; one driver per line) */
#define	LKPI_SDIO_DRIVERS_MAX		4

/* the interface caps a single extended transfer at 511 blocks */
#define	LKPI_SDIO_EXT_MAX_BLOCKS	511u

/*
 * The largest single byte-mode transfer this engine hands down. The seam's
 * backend performs a byte-mode request as one extended transfer per 508 bytes
 * plus a direct (single-byte) transfer for each byte of the remainder, because
 * the host cannot move a count that is not a multiple of four. A chunk of 512
 * therefore ends in four direct transfers - and a direct transfer to a FIFO port
 * is not a data transfer: the card's receive FIFO pointer does not advance with
 * it, so the next read starts four bytes into the next frame and every frame
 * after that parses as garbage. Keeping the chunk at the backend's own ceiling
 * keeps every chunk a multiple of four, which is also what the vendor driver
 * aligns its own receive reads to (RND4 in its sd_recv_rxfifo), so the direct
 * path is never reached for a data port.
 */
#define	LKPI_SDIO_BYTE_CHUNK_MAX	508u

/* the byte-mode ceiling the interface states for a card without quirks */
#define	LKPI_SDIO_BYTE_MODE_MAX		512u

/* the function-enable poll budget, milliseconds (the interface's default) */
#define	LKPI_SDIO_ENABLE_TIMEOUT_MS	500u

/* the scratch buffer the 2-byte and 4-byte calls use */
#define	LKPI_SDIO_TMPBUF_SIZE		4u

/* the pump's idle period, milliseconds (the interface polls a host without an
 * interrupt line every 10 ms) */
#define	LKPI_SDIO_PUMP_IDLE_MS		10u

/* the pump runs a driver's handler, so its stack covers that handler's own
 * frames; sized from the engine's needs, raised if a handler overflows it */
#define	LKPI_SDIO_PUMP_STACK		8192u

/* the longest a pump_kick() waits for its cycle to finish */
#define	LKPI_SDIO_KICK_TIMEOUT_MS	200u

/* The drain-to-empty loop's bound: rounds one claim may serve before yielding
 * the bus back, so a card that never stops reporting cannot hold it. */
#define	LKPI_SDIO_DRAIN_MAX_ROUNDS	16u

/*
 * The pump's cadence modes (LKPI_SDIO_PUMP_MODE_*, linuxkpi_sdio.h). FIXED
 * spaces every cycle by the idle period above - the shape a polled host has
 * always had here. ADAPTIVE follows Linux' polled-host IRQ thread
 * (drivers/mmc/core/sdio_irq.c sdio_irq_thread(): idle_period 10 ms,
 * `period /= 2` after a cycle that found work, `period++` back toward idle
 * after one that found none) with one adaptation this engine's situation
 * demands: on this port the wait IS the mechanism that discovers work (there
 * is no interrupt line), so the backoff must not begin while traffic is
 * recent. A cycle that moved bytes therefore drops the period straight to
 * zero and stamps the moment; while less than LKPI_SDIO_PUMP_HOT_MS has
 * passed since that stamp, an empty cycle only falls back to the hot floor
 * (1 ms - one tick), and the step-back toward the idle period starts once
 * the stamp is old. Without the hold, a card that releases in bursts (its
 * own aggregation timer) would be rediscovered only after the period has
 * climbed several milliseconds, and the first adaptive round measured
 * exactly that: the gain stopped at the rediscovery latency.
 */
#define	LKPI_SDIO_PUMP_HOT_MS		20u
#define	LKPI_SDIO_PUMP_HOT_FLOOR_MS	1u

struct lkpi_sdio_card {
	bool				present;
	struct lkpi_sdio_be_card_info	info;
	struct mmc_host			host;
	struct mmc_card			card;
	/* one object per function number; [0] is the card itself */
	struct sdio_func		func[LKPI_SDIO_BE_MAX_FUNCS + 1];
	/* a function a driver's probe already claimed is not offered again */
	bool				claimed[LKPI_SDIO_BE_MAX_FUNCS + 1];
	u8				tmpbuf[LKPI_SDIO_BE_MAX_FUNCS + 1][LKPI_SDIO_TMPBUF_SIZE];
	char				name[LKPI_SDIO_BE_MAX_FUNCS + 1][12];
	osMutexId_t			claim;
	osThreadId_t			pump;
	osThreadId_t			claim_owner;
	osSemaphoreId_t			pump_req;
	osSemaphoreId_t			pump_done;
	bool				pump_running;
	/* the pump's cadence: the mode is the runtime knob, the period is the
	 * adaptive controller's state (milliseconds, idle period when fixed) */
	unsigned int			pump_mode;
	unsigned int			pump_period_ms;
	/* the card-interrupt mode: when on, the wait is a backstop and the card's
	 * own events (through lkpi_sdio_pump_notify) carry discovery; when off,
	 * the period is the discovery mechanism, as it always was */
	bool				pump_irq;
	/* set by the notify path (interrupt context) just before it releases the
	 * wait, so the pump can classify its own wake */
	volatile bool			irq_wake;
	/* the wake-chain knobs (the align round's second arm): a pump raised to
	 * the normal band competes with the driver threads instead of yielding to
	 * them; drain-to-empty keeps dispatching inside one cycle while the card
	 * still reports pending work. Both default to the shapes every earlier
	 * measurement ran. */
	bool				pump_prio_normal;
	bool				pump_drain;
	/* when the last cycle that moved bytes finished (the hot hold's stamp) */
	uint64_t			pump_hot_ns;
	/* always-on accounting of the bytes the splitter moved, by direction.
	 * The adaptive controller measures "did this cycle move anything" by
	 * these, so the progress signal is the transfers themselves and not a
	 * pending bit that may still be set while the card has nothing to give;
	 * the counters are plain sums, so a lost increment under a preemption
	 * costs one backoff step, never a stall. */
	uint64_t			rx_bytes;
	uint64_t			tx_bytes;
	/* how many threads the pump's own claims have left waiting (the fairness
	 * yield's input). A driver thread that wants the bus while the pump holds it
	 * parks here; the pump reads the count before it takes the claim, so it can
	 * stand aside for a turn instead of re-taking the bus ahead of them (see
	 * lkpi_sdio_pump_yield_set). */
	unsigned int			claim_waiters;
	bool				pump_yield;
};

static struct lkpi_sdio_card lkpi_sdio_card_state;
static struct lkpi_sdio_stats lkpi_sdio_stats;
static bool lkpi_sdio_started;

/*
 * The census this engine contributes: the physical transfers the splitter
 * issued, the claim, and the pump's own cadence. The gap histograms are the
 * discrimination a throughput round turns on - batches with clear space between
 * them are a cadence limit, evenly spaced transactions are a per-op cost limit.
 * The helpers below are no-ops when the census is off, which keeps every call
 * site unconditional.
 */
#ifdef CONFIG_LKPI_CENSUS
struct lkpi_sdio_census {
	/* the direct transfers */
	uint32_t	cmd52;
	struct lkpi_census_histo	cmd52_lat;
	/* the extended transfers, per direction */
	uint32_t	cmd53_r;
	uint32_t	cmd53_r_blk;
	uint32_t	cmd53_r_byt;
	uint64_t	cmd53_r_bytes;
	struct lkpi_census_histo	cmd53_r_lat;
	struct lkpi_census_histo	cmd53_r_sz;
	struct lkpi_census_histo	cmd53_r_gap;
	uint32_t	cmd53_w;
	uint32_t	cmd53_w_blk;
	uint32_t	cmd53_w_byt;
	uint64_t	cmd53_w_bytes;
	struct lkpi_census_histo	cmd53_w_lat;
	struct lkpi_census_histo	cmd53_w_sz;
	struct lkpi_census_histo	cmd53_w_gap;
	uint64_t	last_r_ns;
	uint64_t	last_w_ns;
	/* the driver-facing claim */
	uint32_t	claim_calls;
	uint64_t	claim_held_ns;
	struct lkpi_census_histo	claim_wait;
	struct lkpi_census_histo	claim_hold;
	/* the pump */
	uint32_t	wake_kick;
	uint32_t	wake_timeout;
	uint32_t	wake_hot;
	/* the card interrupt: notify calls (ISR side) and the pump's wakes
	 * classified as interrupt-sourced; both zero on a polling boot */
	uint32_t	irq_notify;
	uint32_t	wake_irq;
	/* drain-to-empty: rounds dispatched beyond the first inside one cycle */
	uint32_t	drain_extra;
	/* the event-to-service latency of the interrupt path: the ISR stamps the
	 * moment, the pump takes it when it starts the cycle that answers it */
	uint64_t	irq_stamp_ns;
	struct lkpi_census_histo	irq_lat;
	uint32_t	disp;	/* handler dispatches (functions served) */
	uint32_t	cycles;
	uint32_t	cycles_busy;
	uint32_t	cycles_empty;
	uint32_t	cycles_err;
	uint32_t	yields;	/* cycles the fairness knob gave away */
	struct lkpi_census_histo	cyc_term;
	struct lkpi_census_histo	cyc_claim_wait;
	struct lkpi_census_histo	cyc_handler;
	/* the service gap between consecutive busy cycles: the active-interval
	 * denominator (first/last busy boundaries plus the count inside them),
	 * so a measurement window's idle tail stops diluting the cadence share. */
	uint64_t	busy_first_ns;
	uint64_t	busy_last_ns;
	uint32_t	busy_span;
	struct lkpi_census_histo	busy_gap;
	struct lkpi_census_histo	busy_bytes;
	/* transmit age: from the stack handing a frame to the driver's transmit
	 * entry (lkpi_census_tx_request_note) to the card write that carried it -
	 * "how long an ACK waited behind the claim", the number the RX-cadence
	 * discussion turns on. */
	uint32_t	tx_age_n;
	struct lkpi_census_histo	tx_age;
};

static struct lkpi_sdio_census lkpi_sdio_census_state;

static uint64_t
lkpi_sdio_census_now(void)
{

	return lkpi_census_now_ns();
}

static void
lkpi_sdio_census_note_direct(uint64_t t0, uint64_t t1)
{

	lkpi_sdio_census_state.cmd52++;
	lkpi_census_histo_add(&lkpi_sdio_census_state.cmd52_lat, t1 - t0);
}

static void
lkpi_sdio_census_note_ext(bool write, bool block, unsigned int bytes,
    uint64_t t0, uint64_t t1)
{
	struct lkpi_sdio_census *c = &lkpi_sdio_census_state;
	uint64_t *last = write ? &c->last_w_ns : &c->last_r_ns;

	if (*last != 0u)
		lkpi_census_histo_add(write ? &c->cmd53_w_gap : &c->cmd53_r_gap,
		    t0 - *last);
	*last = t1;
	if (write) {
		uint64_t req = lkpi_census_tx_request_take();

		/* dated to the write's start, so the age is "how long the frame
		 * waited to reach the bus" rather than to its completion */
		if (req != 0u && t0 > req) {
			lkpi_census_histo_add(&c->tx_age, t0 - req);
			c->tx_age_n++;
		}
		c->cmd53_w++;
		if (block)
			c->cmd53_w_blk++;
		else
			c->cmd53_w_byt++;
		c->cmd53_w_bytes += bytes;
		lkpi_census_histo_add(&c->cmd53_w_lat, t1 - t0);
		lkpi_census_histo_add(&c->cmd53_w_sz, bytes);
	} else {
		c->cmd53_r++;
		if (block)
			c->cmd53_r_blk++;
		else
			c->cmd53_r_byt++;
		c->cmd53_r_bytes += bytes;
		lkpi_census_histo_add(&c->cmd53_r_lat, t1 - t0);
		lkpi_census_histo_add(&c->cmd53_r_sz, bytes);
	}
}

static void
lkpi_sdio_census_note_claim_wait(uint64_t t0, uint64_t t1)
{

	lkpi_sdio_census_state.claim_calls++;
	lkpi_census_histo_add(&lkpi_sdio_census_state.claim_wait, t1 - t0);
}

static void
lkpi_sdio_census_note_claim_hold(uint64_t t1)
{

	lkpi_sdio_census_state.claim_held_ns = t1;
}

static void
lkpi_sdio_census_note_claim_release(uint64_t t1)
{
	struct lkpi_sdio_census *c = &lkpi_sdio_census_state;

	if (c->claim_held_ns != 0u) {
		lkpi_census_histo_add(&c->claim_hold, t1 - c->claim_held_ns);
		c->claim_held_ns = 0u;
	}
}

static void
lkpi_sdio_census_note_wake(bool kicked, unsigned int wait_ms)
{

	if (kicked)
		lkpi_sdio_census_state.wake_kick++;
	else if (wait_ms == 0u)
		lkpi_sdio_census_state.wake_hot++;
	else
		lkpi_sdio_census_state.wake_timeout++;
}

/* interrupt context */
static void
lkpi_sdio_census_note_irq_notify(void)
{

	lkpi_sdio_census_state.irq_notify++;
}

static void
lkpi_sdio_census_note_wake_irq(void)
{

	lkpi_sdio_census_state.wake_irq++;
}

/* interrupt context: stamp the moment the card event arrived */
static void
lkpi_sdio_census_note_irq_stamp(uint64_t t)
{

	lkpi_sdio_census_state.irq_stamp_ns = t;
}

/* the pump takes the stamp when it starts the cycle that answers it */
static void
lkpi_sdio_census_note_irq_lat(uint64_t t)
{
	struct lkpi_sdio_census *c = &lkpi_sdio_census_state;

	if (c->irq_stamp_ns != 0u && t > c->irq_stamp_ns) {
		lkpi_census_histo_add(&c->irq_lat, t - c->irq_stamp_ns);
		c->irq_stamp_ns = 0u;
	}
}

static void
lkpi_sdio_census_note_dispatch(void)
{

	lkpi_sdio_census_state.disp++;
}

static void
lkpi_sdio_census_note_drain(uint32_t extra)
{

	lkpi_sdio_census_state.drain_extra += extra;
}

static void
lkpi_sdio_census_note_yield(void)
{

	lkpi_sdio_census_state.yields++;
}

static void
lkpi_sdio_census_note_pump(void)
{

	lkpi_census_pump_probe();
}

static void
lkpi_sdio_census_note_cycle(uint64_t t0, uint64_t t1, uint64_t t2, bool busy,
    bool err, uint64_t moved_bytes)
{
	struct lkpi_sdio_census *c = &lkpi_sdio_census_state;

	c->cycles++;
	if (err)
		c->cycles_err++;
	else if (busy)
		c->cycles_busy++;
	else
		c->cycles_empty++;
	lkpi_census_histo_add(&c->cyc_term, t2 - t0);
	lkpi_census_histo_add(&c->cyc_claim_wait, t1 - t0);
	lkpi_census_histo_add(&c->cyc_handler, t2 - t1);
	if (busy && !err) {
		if (c->busy_first_ns == 0u)
			c->busy_first_ns = t0;
		else
			lkpi_census_histo_add(&c->busy_gap, t0 - c->busy_last_ns);
		c->busy_last_ns = t2;
		c->busy_span++;
		lkpi_census_histo_add(&c->busy_bytes, moved_bytes);
	}
}

void
lkpi_sdio_census_reset(void)
{

	(void)memset(&lkpi_sdio_census_state, 0, sizeof(lkpi_sdio_census_state));
}

void
lkpi_sdio_census_print(void)
{
	struct lkpi_sdio_census *c = &lkpi_sdio_census_state;

	lkpi_printf(ULOG_INFO_LEVEL, "perf census: A cmd52 n=%u\n", c->cmd52);
	lkpi_census_histo_print("perf census: A cmd52_lat", &c->cmd52_lat);
	lkpi_printf(ULOG_INFO_LEVEL,
	    "perf census: A cmd53rx n=%u blk=%u byt=%u bytes=%llu\n",
	    c->cmd53_r, c->cmd53_r_blk, c->cmd53_r_byt, c->cmd53_r_bytes);
	lkpi_census_histo_print("perf census: A cmd53rx_lat", &c->cmd53_r_lat);
	lkpi_census_histo_print("perf census: A cmd53rx_sz", &c->cmd53_r_sz);
	lkpi_census_histo_print("perf census: A cmd53rx_gap", &c->cmd53_r_gap);
	lkpi_printf(ULOG_INFO_LEVEL,
	    "perf census: A cmd53tx n=%u blk=%u byt=%u bytes=%llu\n",
	    c->cmd53_w, c->cmd53_w_blk, c->cmd53_w_byt, c->cmd53_w_bytes);
	lkpi_census_histo_print("perf census: A cmd53tx_lat", &c->cmd53_w_lat);
	lkpi_census_histo_print("perf census: A cmd53tx_sz", &c->cmd53_w_sz);
	lkpi_census_histo_print("perf census: A cmd53tx_gap", &c->cmd53_w_gap);
	lkpi_printf(ULOG_INFO_LEVEL, "perf census: A claim n=%u\n",
	    c->claim_calls);
	lkpi_census_histo_print("perf census: A claim_wait", &c->claim_wait);
	lkpi_census_histo_print("perf census: A claim_hold", &c->claim_hold);
	lkpi_printf(ULOG_INFO_LEVEL,
	    "perf census: D pump cycles=%u kick=%u timeout=%u hot=%u busy=%u empty=%u err=%u yields=%u mode=%s period_ms=%u span_ms=%llu busy_span=%u\n",
	    c->cycles, c->wake_kick, c->wake_timeout, c->wake_hot,
	    c->cycles_busy, c->cycles_empty, c->cycles_err, c->yields,
	    (lkpi_sdio_card_state.pump_mode == LKPI_SDIO_PUMP_MODE_ADAPTIVE) ?
	    "adaptive" : "fixed",
	    lkpi_sdio_card_state.pump_period_ms,
	    (unsigned long long)((c->busy_last_ns - c->busy_first_ns) / 1000000u),
	    c->busy_span);
	lkpi_census_histo_print("perf census: D pump_cyc", &c->cyc_term);
	lkpi_census_histo_print("perf census: D pump_wait", &c->cyc_claim_wait);
	lkpi_census_histo_print("perf census: D pump_handler", &c->cyc_handler);
	lkpi_census_histo_print("perf census: D pump_gap", &c->busy_gap);
	lkpi_census_histo_print("perf census: D pump_byt", &c->busy_bytes);
	/* the card-interrupt line, its own shape so no earlier anchor moves */
	lkpi_printf(ULOG_INFO_LEVEL,
	    "perf census: D pumpirq on=%u notify=%u wake=%u prio=%s drain=%u\n",
	    lkpi_sdio_card_state.pump_irq ? 1u : 0u, c->irq_notify, c->wake_irq,
	    lkpi_sdio_card_state.pump_prio_normal ? "normal" : "below",
	    lkpi_sdio_card_state.pump_drain ? 1u : 0u);
	/* the round's timeline and receive-release shapes: the four boundaries a
	 * frame crosses (the stack's handoff notes, the bus claim, the wire write,
	 * the handler dispatch) and the receive side's read account. The middle
	 * split - assembly inside the vendored driver - has no host-side boundary
	 * to stand on (zero-patch), so the write-age histogram above is its
	 * evidence instead. */
	lkpi_census_histo_print("perf tl: irq_wake", &c->irq_lat);
	lkpi_printf(ULOG_INFO_LEVEL,
	    "perf tl: ndo=%u claim=%u wire=%u wbytes=%llu disp=%u\n",
	    c->tx_age_n, c->claim_calls, c->cmd53_w,
	    (unsigned long long)c->cmd53_w_bytes, c->disp);
	lkpi_printf(ULOG_INFO_LEVEL,
	    "perf rxrel: reads=%u rbytes=%llu busy_cyc=%u disp=%u drain=%u\n",
	    c->cmd53_r, (unsigned long long)c->cmd53_r_bytes, c->cycles_busy,
	    c->disp, c->drain_extra);
	lkpi_printf(ULOG_INFO_LEVEL, "perf census: D txage n=%u\n", c->tx_age_n);
	lkpi_census_histo_print("perf census: D txage", &c->tx_age);
}

#else	/* !CONFIG_LKPI_CENSUS */

static uint64_t
lkpi_sdio_census_now(void)
{

	return 0u;
}

static void
lkpi_sdio_census_note_direct(uint64_t t0, uint64_t t1)
{

	(void)t0;
	(void)t1;
}

static void
lkpi_sdio_census_note_ext(bool write, bool block, unsigned int bytes,
    uint64_t t0, uint64_t t1)
{

	(void)write;
	(void)block;
	(void)bytes;
	(void)t0;
	(void)t1;
}

static void
lkpi_sdio_census_note_claim_wait(uint64_t t0, uint64_t t1)
{

	(void)t0;
	(void)t1;
}

static void
lkpi_sdio_census_note_claim_hold(uint64_t t1)
{

	(void)t1;
}

static void
lkpi_sdio_census_note_claim_release(uint64_t t1)
{

	(void)t1;
}

static void
lkpi_sdio_census_note_wake(bool kicked, unsigned int wait_ms)
{

	(void)kicked;
	(void)wait_ms;
}

static void
lkpi_sdio_census_note_irq_notify(void)
{
}

static void
lkpi_sdio_census_note_wake_irq(void)
{
}

static void
lkpi_sdio_census_note_irq_stamp(uint64_t t)
{

	(void)t;
}

static void
lkpi_sdio_census_note_irq_lat(uint64_t t)
{

	(void)t;
}

static void
lkpi_sdio_census_note_dispatch(void)
{
}

static void
lkpi_sdio_census_note_drain(uint32_t extra)
{

	(void)extra;
}

static void
lkpi_sdio_census_note_yield(void)
{
}

static void
lkpi_sdio_census_note_pump(void)
{
}

static void
lkpi_sdio_census_note_cycle(uint64_t t0, uint64_t t1, uint64_t t2, bool busy,
    bool err, uint64_t moved_bytes)
{

	(void)t0;
	(void)t1;
	(void)t2;
	(void)busy;
	(void)err;
	(void)moved_bytes;
}

#endif	/* CONFIG_LKPI_CENSUS */

static struct sdio_driver *lkpi_sdio_drivers[LKPI_SDIO_DRIVERS_MAX];
static unsigned int lkpi_sdio_driver_count;

/* ------------------------------------------------------------------ */
/* the seam's error family into Linux errno                            */
/* ------------------------------------------------------------------ */

/*
 * The seam's codes keep the imported stack's numbers, so this is a relabelling
 * into the interface's errno family. A transfer the card answered with an error
 * is -EIO (the interface maps a failed I/O command the same way); a missing card
 * is -ENODEV; a request the host cannot perform at all is -EOPNOTSUPP.
 */
static int
lkpi_sdio_errno(int be_err)
{

	switch (-be_err) {
	case 0:
		return 0;
	case LKPI_SDIO_BE_ERR_INVAL:
		return -EINVAL;
	case LKPI_SDIO_BE_ERR_TIMEOUT:
		return -ETIMEDOUT;
	case LKPI_SDIO_BE_ERR_NOCARD:
	case LKPI_SDIO_BE_ERR_NOTSDIO:
		return -ENODEV;
	case LKPI_SDIO_BE_ERR_HOST:
	case LKPI_SDIO_BE_ERR_CARD:
		return -EOPNOTSUPP;
	default:
		return -EIO;
	}
}

static struct lkpi_sdio_card *
lkpi_sdio_live(void)
{

	if (!lkpi_sdio_card_state.present)
		return NULL;
	return &lkpi_sdio_card_state;
}

/* ------------------------------------------------------------------ */
/* the bus claim                                                       */
/* ------------------------------------------------------------------ */

void
sdio_claim_host(struct sdio_func *func)
{
	struct lkpi_sdio_card *c = lkpi_sdio_live();
	uint64_t t0;

	if (func == NULL || c == NULL || c->claim == NULL)
		return;
	t0 = lkpi_sdio_census_now();
	/* announced before the wait, so the pump can see the request and stand aside
	 * for a turn rather than re-taking the bus ahead of this caller */
	c->claim_waiters++;
	(void)osMutexAcquire(c->claim, osWaitForever);
	c->claim_waiters--;
	lkpi_sdio_census_note_claim_wait(t0, lkpi_sdio_census_now());
	lkpi_sdio_census_note_claim_hold(lkpi_sdio_census_now());
	c->claim_owner = osThreadGetId();
	c->host.claimed = true;
	lkpi_sdio_stats.claims++;
}

void
sdio_release_host(struct sdio_func *func)
{
	struct lkpi_sdio_card *c = lkpi_sdio_live();

	if (func == NULL || c == NULL || c->claim == NULL)
		return;
	c->host.claimed = false;
	c->claim_owner = NULL;
	lkpi_sdio_stats.releases++;
	lkpi_sdio_census_note_claim_release(lkpi_sdio_census_now());
	(void)osMutexRelease(c->claim);
}

bool
lkpi_sdio_claim_held_by_current(void)
{

	return lkpi_sdio_card_state.host.claimed &&
	    lkpi_sdio_card_state.claim_owner == osThreadGetId();
}

/* ------------------------------------------------------------------ */
/* single-register access (one direct transfer)                        */
/* ------------------------------------------------------------------ */

static int
lkpi_sdio_direct(unsigned int func, unsigned int addr, bool write, u8 *value)
{
	int st;
	uint64_t t0;

	t0 = lkpi_sdio_census_now();
	st = lkpi_sdio_be_io_direct(func, addr, write, value);
	lkpi_sdio_census_note_direct(t0, lkpi_sdio_census_now());
	lkpi_sdio_stats.io_direct++;
	if (st != 0)
		lkpi_sdio_stats.errors++;
	return st;
}

u8
sdio_readb(struct sdio_func *func, unsigned int addr, int *err_ret)
{
	u8 val = 0;
	int st;

	if (func == NULL) {
		if (err_ret != NULL)
			*err_ret = -EINVAL;
		return 0xff;
	}
	st = lkpi_sdio_direct(func->num, addr, false, &val);
	if (st != 0) {
		if (err_ret != NULL)
			*err_ret = lkpi_sdio_errno(st);
		return 0xff;
	}
	if (err_ret != NULL)
		*err_ret = 0;
	return val;
}

void
sdio_writeb(struct sdio_func *func, u8 b, unsigned int addr, int *err_ret)
{
	int st;

	if (func == NULL) {
		if (err_ret != NULL)
			*err_ret = -EINVAL;
		return;
	}
	st = lkpi_sdio_direct(func->num, addr, true, &b);
	if (err_ret != NULL)
		*err_ret = st != 0 ? lkpi_sdio_errno(st) : 0;
}

unsigned char
sdio_f0_readb(struct sdio_func *func, unsigned int addr, int *err_ret)
{
	u8 val = 0;
	int st;

	if (func == NULL) {
		if (err_ret != NULL)
			*err_ret = -EINVAL;
		return 0xff;
	}
	st = lkpi_sdio_direct(0u, addr, false, &val);
	if (st != 0) {
		if (err_ret != NULL)
			*err_ret = lkpi_sdio_errno(st);
		return 0xff;
	}
	if (err_ret != NULL)
		*err_ret = 0;
	return val;
}

void
sdio_f0_writeb(struct sdio_func *func, unsigned char b, unsigned int addr,
    int *err_ret)
{
	int st;

	if (func == NULL) {
		if (err_ret != NULL)
			*err_ret = -EINVAL;
		return;
	}
	/* the interface restricts function-0 writes to the vendor range; this port
	 * carries no lenient-function-0 quirk, so the restriction always holds */
	if (addr < 0xf0u || addr > 0xffu) {
		if (err_ret != NULL)
			*err_ret = -EINVAL;
		return;
	}
	st = lkpi_sdio_direct(0u, addr, true, &b);
	if (err_ret != NULL)
		*err_ret = st != 0 ? lkpi_sdio_errno(st) : 0;
}

/* ------------------------------------------------------------------ */
/* buffered access (extended transfers)                                */
/* ------------------------------------------------------------------ */

/*
 * The largest byte-mode transfer this card and host allow: the controller's
 * block ceiling, the function's own ceiling when it stated one, and the
 * interface's 512-byte byte-mode maximum. A zero function ceiling means the CIS
 * never stated one, which is not the same as a zero-byte ceiling.
 */
static unsigned int
sdio_max_byte_size(const struct sdio_func *func)
{
	unsigned int mval = func->card->host->max_blk_size;

	if (func->max_blksize != 0u && mval > func->max_blksize)
		mval = func->max_blksize;
	if (mval > LKPI_SDIO_BYTE_CHUNK_MAX)
		mval = LKPI_SDIO_BYTE_CHUNK_MAX;
	if (mval == 0u)
		mval = LKPI_SDIO_BYTE_CHUNK_MAX;
	return mval;
}

/*
 * Split an arbitrarily sized transfer into extended transfers the card and the
 * host can each perform: whole blocks first (only when the card supports
 * multi-block transfers and the request is larger than one byte-mode chunk),
 * each command bounded by the host's block count and the interface's 511-block
 * ceiling, then the remainder in byte mode. The increment-address flag travels
 * with the request.
 */
static int
sdio_io_rw_ext_helper(struct sdio_func *func, bool write, unsigned int addr,
    bool incr, u8 *buf, unsigned int size)
{
	struct lkpi_sdio_card *c = lkpi_sdio_live();
	unsigned int remainder = size;
	unsigned int max_blocks;
	unsigned int base;
	uint64_t t0;

	if (func == NULL || c == NULL)
		return -ENODEV;
	if (func->num > (unsigned int)LKPI_SDIO_BE_MAX_FUNCS)
		return -EINVAL;
	if (size == 0u)
		return 0;

	base = write ? LKPI_SDIO_BE_F_WRITE : 0u;
	if (incr)
		base |= LKPI_SDIO_BE_F_INCR;

	if (func->card->cccr.multi_block != 0 &&
	    size > sdio_max_byte_size(func) && func->cur_blksize != 0u) {
		max_blocks = func->card->host->max_blk_count;
		if (max_blocks > LKPI_SDIO_EXT_MAX_BLOCKS)
			max_blocks = LKPI_SDIO_EXT_MAX_BLOCKS;
		if (max_blocks == 0u)
			max_blocks = LKPI_SDIO_EXT_MAX_BLOCKS;
		while (remainder >= func->cur_blksize) {
			unsigned int blocks = remainder / func->cur_blksize;
			unsigned int bytes;
			int st;

			if (blocks > max_blocks)
				blocks = max_blocks;
			bytes = blocks * func->cur_blksize;
			t0 = lkpi_sdio_census_now();
			st = lkpi_sdio_be_io_extended(func->num, addr, buf, blocks,
			    base | LKPI_SDIO_BE_F_BLOCK);
			lkpi_sdio_census_note_ext(write, true, bytes, t0,
			    lkpi_sdio_census_now());
			lkpi_sdio_stats.io_extended++;
			if (st != 0) {
				lkpi_sdio_stats.errors++;
				return lkpi_sdio_errno(st);
			}
			if (write)
				c->tx_bytes += bytes;
			else
				c->rx_bytes += bytes;
			remainder -= bytes;
			buf += bytes;
			if (incr)
				addr += bytes;
		}
	}

	while (remainder > 0u) {
		unsigned int n = sdio_max_byte_size(func);
		int st;

		if (n > remainder)
			n = remainder;
		t0 = lkpi_sdio_census_now();
		st = lkpi_sdio_be_io_extended(func->num, addr, buf, n, base);
		lkpi_sdio_census_note_ext(write, false, n, t0,
		    lkpi_sdio_census_now());
		lkpi_sdio_stats.io_extended++;
		if (st != 0) {
			lkpi_sdio_stats.errors++;
			return lkpi_sdio_errno(st);
		}
		if (write)
			c->tx_bytes += n;
		else
			c->rx_bytes += n;
		remainder -= n;
		buf += n;
		if (incr)
			addr += n;
	}
	return 0;
}

/*
 * A receive-FIFO read carries the hardware's own descriptor, so when a driver's
 * receive path misbehaves the raw bytes are the evidence that separates "the
 * card wrote what the driver expects" from "the port delivered something else".
 * The receive FIFO is the one port whose address sits in the 0xE000 window
 * (WLAN_RX0FF_DEVICE_ID = 7 shifted into the command address, the vendor
 * driver's HalSdioGetCmdAddr8188FSdio), and the dump is bounded so a busy
 * receive path cannot drown the console.
 */
#define	LKPI_SDIO_RX_DUMP_MAX	24

static unsigned int lkpi_sdio_rx_dumps;

static u32 lkpi_sdio_le32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	    ((u32)p[3] << 24);
}

static void
lkpi_sdio_rx_read_show(unsigned int addr, const void *buf, unsigned int count,
    int ret)
{
	const u8 *p = buf;
	unsigned int n = count & ~3u;
	char chain[96];
	char types[64];
	size_t clen = 0;
	size_t tlen = 0;
	unsigned int frames = 0;
	unsigned int off = 0;

	if ((addr & 0xE000u) != 0xE000u || count < 32u)
		return;
	lkpi_sdio_rx_dumps++;
	/* The first reads of a run are the interesting ones, and a sparse sample
	 * after them keeps a later window (a join's authentication exchange, whose
	 * frames are small) from being hidden by an earlier busy one. */
	if (lkpi_sdio_rx_dumps > LKPI_SDIO_RX_DUMP_MAX &&
	    (lkpi_sdio_rx_dumps % LKPI_SDIO_RX_DUMP_MAX) != 0u)
		return;

	/*
	 * Walk the buffer the way the driver's receive loop does
	 * (hal/rtl8188f/sdio/rtl8189fs_recv.c:59-99): the descriptor at a position
	 * gives pkt_len (bits 13:0), drvinfo_sz (bits 19:16, in eights) and shift
	 * (bits 25:24), and the next frame starts 24 + drvinfo_sz + shift + pkt_len
	 * further on. Printing the chain says whether the frames inside one read are
	 * contiguous - which is what separates "the port delivered a buffer the
	 * driver cannot walk" from "the driver's own stride is wrong". The frame's
	 * own header follows the report, and its type and address-3 identify what
	 * was received (a beacon is 0x80 and carries the access point's BSSID in
	 * address 3) - the question a stalled join turns on.
	 */
	while (off + 24u <= n && frames < 4u) {
		u32 w0 = lkpi_sdio_le32(p + off);
		unsigned int plen = w0 & 0x3FFFu;
		unsigned int dinfo = ((w0 >> 16) & 0xFu) << 3;
		unsigned int shift = (w0 >> 24) & 0x3u;
		unsigned int step = 24u + dinfo + shift + plen;
		const u8 *hdr = p + off + 24u + dinfo + shift;
		int wrote;

		wrote = snprintf(chain + clen, sizeof(chain) - clen, "%s%u+%u",
		    frames != 0u ? " " : "", off, step);
		if (wrote < 0 || (size_t)wrote >= sizeof(chain) - clen)
			break;
		clen += (size_t)wrote;
		if (off + 24u + dinfo + shift + 22u <= n) {
			wrote = snprintf(types + tlen, sizeof(types) - tlen,
			    "%s%02x%02x:%02x%02x%02x%02x%02x%02x",
			    frames != 0u ? " " : "", hdr[0], hdr[1],
			    hdr[16], hdr[17], hdr[18], hdr[19], hdr[20], hdr[21]);
			if (wrote > 0 && (size_t)wrote < sizeof(types) - tlen)
				tlen += (size_t)wrote;
		}
		if (step < 24u || off + step > n)
			break;
		off += step;
		frames++;
	}

	lkpi_printf(ULOG_DEBUG_LEVEL,
	    "sdiokpi: rxread#%u addr=0x%x count=%u ret=%d walk=[%s] hdr=[%s] walked=%u\n",
	    lkpi_sdio_rx_dumps, addr, count, ret, chain, types, off);
}

int
sdio_memcpy_fromio(struct sdio_func *func, void *dst, unsigned int addr,
    int count)
{
	int ret;

	if (count <= 0)
		return -EINVAL;
	ret = sdio_io_rw_ext_helper(func, false, addr, true, dst,
	    (unsigned int)count);
	lkpi_sdio_rx_read_show(addr, dst, (unsigned int)count, ret);
	return ret;
}

int
sdio_memcpy_toio(struct sdio_func *func, unsigned int addr, void *src,
    int count)
{

	if (count <= 0)
		return -EINVAL;
	return sdio_io_rw_ext_helper(func, true, addr, true, src,
	    (unsigned int)count);
}

int
sdio_readsb(struct sdio_func *func, void *dst, unsigned int addr, int count)
{

	if (count <= 0)
		return -EINVAL;
	return sdio_io_rw_ext_helper(func, false, addr, false, dst,
	    (unsigned int)count);
}

int
sdio_writesb(struct sdio_func *func, unsigned int addr, void *src, int count)
{

	if (count <= 0)
		return -EINVAL;
	return sdio_io_rw_ext_helper(func, true, addr, false, src,
	    (unsigned int)count);
}

/* the 2-byte and 4-byte calls ride the buffered path through the function's
 * scratch buffer, in the interface's byte order */
u16
sdio_readw(struct sdio_func *func, unsigned int addr, int *err_ret)
{
	int ret;

	if (func == NULL) {
		if (err_ret != NULL)
			*err_ret = -EINVAL;
		return 0xffff;
	}
	ret = sdio_memcpy_fromio(func, func->tmpbuf, addr, 2);
	if (err_ret != NULL)
		*err_ret = ret;
	if (ret != 0)
		return 0xffff;
	return le16_to_cpup((const __le16 *)func->tmpbuf);
}

u32
sdio_readl(struct sdio_func *func, unsigned int addr, int *err_ret)
{
	int ret;

	if (func == NULL) {
		if (err_ret != NULL)
			*err_ret = -EINVAL;
		return 0xffffffffu;
	}
	ret = sdio_memcpy_fromio(func, func->tmpbuf, addr, 4);
	if (err_ret != NULL)
		*err_ret = ret;
	if (ret != 0)
		return 0xffffffffu;
	return le32_to_cpup((const __le32 *)func->tmpbuf);
}

void
sdio_writew(struct sdio_func *func, u16 b, unsigned int addr, int *err_ret)
{
	int ret;

	if (func == NULL) {
		if (err_ret != NULL)
			*err_ret = -EINVAL;
		return;
	}
	*(__le16 *)func->tmpbuf = cpu_to_le16(b);
	ret = sdio_memcpy_toio(func, addr, func->tmpbuf, 2);
	if (err_ret != NULL)
		*err_ret = ret;
}

void
sdio_writel(struct sdio_func *func, u32 b, unsigned int addr, int *err_ret)
{
	int ret;

	if (func == NULL) {
		if (err_ret != NULL)
			*err_ret = -EINVAL;
		return;
	}
	*(__le32 *)func->tmpbuf = cpu_to_le32(b);
	ret = sdio_memcpy_toio(func, addr, func->tmpbuf, 4);
	if (err_ret != NULL)
		*err_ret = ret;
}

/* ------------------------------------------------------------------ */
/* function power                                                      */
/* ------------------------------------------------------------------ */

int
sdio_enable_func(struct sdio_func *func)
{
	struct lkpi_sdio_card *c = lkpi_sdio_live();
	unsigned int waited = 0;
	u8 reg = 0;
	int st;

	if (func == NULL || c == NULL)
		return -EINVAL;

	st = lkpi_sdio_direct(0u, SDIO_CCCR_IOEx, false, &reg);
	if (st != 0)
		return lkpi_sdio_errno(st);
	reg |= (u8)(1u << func->num);
	st = lkpi_sdio_direct(0u, SDIO_CCCR_IOEx, true, &reg);
	if (st != 0)
		return lkpi_sdio_errno(st);

	for (;;) {
		st = lkpi_sdio_direct(0u, SDIO_CCCR_IORx, false, &reg);
		if (st != 0)
			return lkpi_sdio_errno(st);
		if ((reg & (1u << func->num)) != 0)
			return 0;
		if (waited >= func->enable_timeout)
			return -ETIMEDOUT;
		lkpi_msleep(1);
		waited++;
	}
}

int
sdio_disable_func(struct sdio_func *func)
{
	struct lkpi_sdio_card *c = lkpi_sdio_live();
	u8 reg = 0;
	int st;

	if (func == NULL || c == NULL)
		return -EINVAL;

	st = lkpi_sdio_direct(0u, SDIO_CCCR_IOEx, false, &reg);
	if (st != 0)
		return lkpi_sdio_errno(st);
	reg &= (u8)~(1u << func->num);
	st = lkpi_sdio_direct(0u, SDIO_CCCR_IOEx, true, &reg);
	if (st != 0)
		return lkpi_sdio_errno(st);
	return 0;
}

int
sdio_set_block_size(struct sdio_func *func, unsigned int blksz)
{
	int st;

	if (func == NULL)
		return -EINVAL;
	if (blksz > func->card->host->max_blk_size)
		return -EINVAL;
	if (blksz == 0u) {
		blksz = func->card->host->max_blk_size;
		if (func->max_blksize != 0u && blksz > func->max_blksize)
			blksz = func->max_blksize;
		if (blksz > LKPI_SDIO_BYTE_MODE_MAX)
			blksz = LKPI_SDIO_BYTE_MODE_MAX;
	}
	st = lkpi_sdio_be_set_block_size(func->num, blksz);
	if (st != 0) {
		lkpi_sdio_stats.errors++;
		return lkpi_sdio_errno(st);
	}
	func->cur_blksize = blksz;
	return 0;
}

/* ------------------------------------------------------------------ */
/* the interrupt pump                                                  */
/* ------------------------------------------------------------------ */

static bool
lkpi_sdio_has_handler(void)
{
	unsigned int i;

	for (i = 1u; i <= (unsigned int)LKPI_SDIO_BE_MAX_FUNCS; i++) {
		if (lkpi_sdio_card_state.func[i].irq_handler != NULL)
			return true;
	}
	return false;
}

/*
 * One poll cycle, on the pump thread: claim the bus, read the card's pending
 * word, hand each pending function to its handler with the claim still held (the
 * interface's contract - a handler must not claim), then release. A function
 * with a pending bit but no handler is a finding, not a dispatch. Returns
 * whether the cycle moved any bytes - the adaptive cadence's progress signal.
 */
static bool
lkpi_sdio_pump_cycle(void)
{
	struct lkpi_sdio_card *c = &lkpi_sdio_card_state;
	u8 pending = 0;
	unsigned int i;
	uint64_t t0, t1, t2;
	uint64_t moved0 = c->rx_bytes + c->tx_bytes;
	bool busy = false;
	bool err = false;
	unsigned int rounds = 0u;

	lkpi_sdio_census_note_pump();
	t0 = lkpi_sdio_census_now();
	(void)osMutexAcquire(c->claim, osWaitForever);
	t1 = lkpi_sdio_census_now();
	c->claim_owner = osThreadGetId();
	c->host.claimed = true;
	lkpi_sdio_stats.claims++;

	/*
	 * One round dispatches the functions the card reports pending. With
	 * drain-to-empty on, a round that saw work is followed by another look
	 * while the card still reports some - the claim is held across the
	 * rounds, so the extra look costs one pending read instead of a
	 * claim/unclaim pair; the loop is bounded (LKPI_SDIO_DRAIN_MAX_ROUNDS)
	 * so a card that never stops reporting cannot hold the bus.
	 */
	for (;;) {
		lkpi_sdio_stats.pump_polls++;
		if (lkpi_sdio_be_irq_pending(&pending) != 0) {
			lkpi_sdio_stats.errors++;
			err = true;
			break;
		}
		if (pending == 0u)
			break;
		for (i = 1u; i <= (unsigned int)LKPI_SDIO_BE_MAX_FUNCS; i++) {
			if ((pending & (1u << i)) == 0)
				continue;
			if (c->func[i].irq_handler != NULL) {
				lkpi_sdio_stats.callbacks++;
				lkpi_sdio_census_note_dispatch();
				busy = true;
				c->func[i].irq_handler(&c->func[i]);
			} else {
				lkpi_printf(ULOG_WARNING_LEVEL,
				    "sdiokpi: pending IRQ on function %u with no handler\n",
				    i);
			}
		}
		rounds++;
		if ((!c->pump_drain) || (rounds >= LKPI_SDIO_DRAIN_MAX_ROUNDS))
			break;
	}
	if (rounds > 1u)
		lkpi_sdio_census_note_drain(rounds - 1u);

	t2 = lkpi_sdio_census_now();
	lkpi_sdio_census_note_cycle(t0, t1, t2, busy, err,
	    (c->rx_bytes + c->tx_bytes) - moved0);

	c->host.claimed = false;
	c->claim_owner = NULL;
	lkpi_sdio_stats.releases++;
	(void)osMutexRelease(c->claim);
	return (c->rx_bytes + c->tx_bytes) != moved0;
}

static void
lkpi_sdio_pump_entry(void *arg)
{
	struct lkpi_sdio_card *c = &lkpi_sdio_card_state;

	(void)arg;
	for (;;) {
		bool requested;
		bool progress = false;
		unsigned int wait_ms;

		/*
		 * Interrupt mode turns the wait into a backstop: the card's events
		 * arrive through lkpi_sdio_pump_notify, and the timeout only covers a
		 * lost line (a lost interrupt must not lose an event - the interface's
		 * rule). Out of interrupt mode the timeout IS the discovery mechanism,
		 * as it always was.
		 */
		wait_ms = c->pump_irq ? LKPI_SDIO_PUMP_IDLE_MS : c->pump_period_ms;
		requested = osSemaphoreAcquire(c->pump_req, wait_ms) == osOK;
		if (c->irq_wake) {
			c->irq_wake = false;
			lkpi_sdio_census_note_wake_irq();
		} else {
			lkpi_sdio_census_note_wake(requested, wait_ms);
		}
		/* the event-to-service latency: how long the card's interrupt waited
		 * for this pump cycle to begin (interrupt-mode phases only) */
		lkpi_sdio_census_note_irq_lat(lkpi_sdio_census_now());
		/*
		 * The fairness yield: a driver thread parked on the claim is waiting for
		 * the bus the pump is about to take again. When the knob is on, the pump
		 * gives that turn away rather than queueing in front of it - it skips one
		 * cycle and comes back after a tick, which is enough for the parked
		 * contend for the now-free claim. Off (the default), the pump's own
		 * cadence owns the bus, which is the shape every earlier measurement ran
		 * under. Interrupt mode does not yield: the wait there is a backstop, so
		 * a skipped cycle would strand the waking event for a full idle period.
		 */
		if (c->pump_yield && !c->pump_irq && c->claim_waiters != 0u) {
			lkpi_sdio_census_note_yield();
			(void)osDelay(1);
			if (requested)
				(void)osSemaphoreRelease(c->pump_done);
			continue;
		}
		/* Interrupt mode runs the cycle even before a driver has claimed a
		 * handler: the re-arm below follows the cycle, and a masked line that
		 * nobody re-arms would strand the card's events. */
		if (lkpi_sdio_has_handler() || c->pump_irq)
			progress = lkpi_sdio_pump_cycle();
		/*
		 * The card-interrupt re-arm: the ISR masked the line when it fired, and
		 * the handler that just ran cleared the card's own interrupt status -
		 * which is what released DAT1. Re-enabling here is what makes a
		 * still-pending event (or a new one) reach the GIC again.
		 */
		if (c->pump_irq)
			(void)lkpi_sdio_be_card_irq_mode(1u);
		if (requested)
			(void)osSemaphoreRelease(c->pump_done);
		if (c->pump_mode == LKPI_SDIO_PUMP_MODE_ADAPTIVE) {
			/* a cycle that moved bytes is followed by one right away,
			 * and stamps the hot hold; an empty cycle inside the hold
			 * keeps the hot floor, and only emptiness after it steps
			 * back toward the idle period. */
			if (progress) {
				c->pump_period_ms = 0u;
				c->pump_hot_ns = (uint64_t)lkpi_ktime_get();
			} else if (c->pump_hot_ns != 0u &&
			    (uint64_t)lkpi_ktime_get() - c->pump_hot_ns <
			    (uint64_t)LKPI_SDIO_PUMP_HOT_MS * 1000000u) {
				c->pump_period_ms = LKPI_SDIO_PUMP_HOT_FLOOR_MS;
			} else if (c->pump_period_ms < LKPI_SDIO_PUMP_IDLE_MS) {
				c->pump_period_ms++;
			}
		}
	}
}

void
lkpi_sdio_pump_kick(void)
{
	struct lkpi_sdio_card *c = &lkpi_sdio_card_state;

	if (!c->pump_running)
		return;
	(void)osSemaphoreRelease(c->pump_req);
	(void)osSemaphoreAcquire(c->pump_done, LKPI_SDIO_KICK_TIMEOUT_MS);
}

/*
 * The card-interrupt notify: called from interrupt context (the controller's
 * ISR, through the backend's card-interrupt upcall). It must only poke the
 * pump - the card's pending word is read and the driver's handler runs on the
 * pump thread, under the claim, exactly as in the polling shape. The release
 * is the backend's ISR-safe primitive (CMSIS-RTOS2 semaphores are).
 */
void
lkpi_sdio_pump_notify(void)
{
	struct lkpi_sdio_card *c = &lkpi_sdio_card_state;

	if (!c->pump_running)
		return;
	c->irq_wake = true;
	lkpi_sdio_census_note_irq_notify();
	lkpi_sdio_census_note_irq_stamp(lkpi_sdio_census_now());
	(void)osSemaphoreRelease(c->pump_req);
}

/*
 * The card-interrupt mode: on arms the backend's line (and the pump's backstop
 * wait), off puts the boot back on the polling cadence. The switch is refused
 * when the backend carries no card-interrupt path, so a caller can always tell
 * whether the mechanism exists rather than believing a knob.
 */
int
lkpi_sdio_pump_irq_set(bool on)
{
	struct lkpi_sdio_card *c = &lkpi_sdio_card_state;
	int st;

	if (!c->present)
		return -LKPI_SDIO_BE_ERR_NOCARD;
	if (c->pump_irq == on)
		return 0;

	st = lkpi_sdio_be_card_irq_mode(on ? 1u : 0u);
	if (st != 0)
		return st;

	c->pump_irq = on;
	if (!on)
		c->irq_wake = false;
	return 0;
}

bool
lkpi_sdio_pump_irq_get(void)
{

	return lkpi_sdio_card_state.pump_irq;
}

/*
 * The wake-chain knobs (the align round's second arm). prio_set moves the pump
 * between the below-normal band it has always run at and the normal band the
 * driver's own threads sit on - at the normal band the pump competes for the
 * bus instead of yielding to the consumer threads, which is a trade this knob
 * exists to measure, not to assume. drain_set keeps a cycle dispatching while
 * the card still reports pending work (bounded; see LKPI_SDIO_DRAIN_MAX_ROUNDS).
 */
void
lkpi_sdio_pump_prio_set(bool normal)
{
	struct lkpi_sdio_card *c = &lkpi_sdio_card_state;

	c->pump_prio_normal = normal;
	if (c->pump != NULL) {
		(void)osThreadSetPriority(c->pump,
		    normal ? osPriorityNormal : osPriorityBelowNormal);
	}
}

bool
lkpi_sdio_pump_prio_get(void)
{

	return lkpi_sdio_card_state.pump_prio_normal;
}

void
lkpi_sdio_pump_drain_set(bool on)
{

	lkpi_sdio_card_state.pump_drain = on;
}

bool
lkpi_sdio_pump_drain_get(void)
{

	return lkpi_sdio_card_state.pump_drain;
}

void
lkpi_sdio_pump_mode_set(unsigned int mode)
{
	struct lkpi_sdio_card *c = &lkpi_sdio_card_state;

	c->pump_mode = (mode == LKPI_SDIO_PUMP_MODE_ADAPTIVE) ?
	    LKPI_SDIO_PUMP_MODE_ADAPTIVE : LKPI_SDIO_PUMP_MODE_FIXED;
	c->pump_period_ms = LKPI_SDIO_PUMP_IDLE_MS;
	c->pump_hot_ns = 0u;
}

unsigned int
lkpi_sdio_pump_mode_get(void)
{

	return lkpi_sdio_card_state.pump_mode;
}

unsigned int
lkpi_sdio_pump_period_get(void)
{

	return lkpi_sdio_card_state.pump_period_ms;
}

void
lkpi_sdio_pump_yield_set(bool on)
{

	lkpi_sdio_card_state.pump_yield = on;
}

bool
lkpi_sdio_pump_yield_get(void)
{

	return lkpi_sdio_card_state.pump_yield;
}

void
lkpi_sdio_bus_info_get(struct lkpi_sdio_bus_info *out)
{
	struct lkpi_sdio_card *c = &lkpi_sdio_card_state;
	struct sdio_func *f;
	unsigned int n;

	if (out == NULL)
		return;
	(void)memset(out, 0, sizeof(*out));
	out->clock_hz = c->host.ios.clock;
	out->bus_width = (c->host.ios.bus_width == MMC_BUS_WIDTH_4) ? 4u : 1u;
	/* the data function is the one that carries bulk traffic; the block size it
	 * settled on is the unit the engine's splitter issues in */
	for (n = 1u; n <= (unsigned int)LKPI_SDIO_BE_MAX_FUNCS; n++) {
		f = &c->func[n];
		if (f->num != n || f->cur_blksize == 0u)
			continue;
		out->blk_size = f->cur_blksize;
		out->blk_max = f->max_blksize;
		break;
	}
}

int
sdio_claim_irq(struct sdio_func *func, sdio_irq_handler_t *handler)
{
	struct lkpi_sdio_card *c = lkpi_sdio_live();
	u8 reg = 0;
	int st;

	if (func == NULL || c == NULL)
		return -EINVAL;
	if (func->irq_handler != NULL)
		return -EBUSY;

	st = lkpi_sdio_direct(0u, SDIO_CCCR_IENx, false, &reg);
	if (st != 0)
		return lkpi_sdio_errno(st);
	reg |= (u8)(1u << func->num);
	reg |= 1u;	/* master interrupt enable */
	st = lkpi_sdio_direct(0u, SDIO_CCCR_IENx, true, &reg);
	if (st != 0)
		return lkpi_sdio_errno(st);

	func->irq_handler = handler;
	c->host.sdio_irqs++;
	lkpi_sdio_stats.irq_claims++;
	return 0;
}

int
sdio_release_irq(struct sdio_func *func)
{
	struct lkpi_sdio_card *c = lkpi_sdio_live();
	u8 reg = 0;
	int st;

	if (func == NULL || c == NULL)
		return -EINVAL;

	func->irq_handler = NULL;
	if (c->host.sdio_irqs > 0u)
		c->host.sdio_irqs--;

	st = lkpi_sdio_direct(0u, SDIO_CCCR_IENx, false, &reg);
	if (st != 0)
		return lkpi_sdio_errno(st);
	reg &= (u8)~(1u << func->num);
	/* the master bit goes with the last function interrupt */
	if ((reg & 0xfeu) == 0u)
		reg = 0u;
	st = lkpi_sdio_direct(0u, SDIO_CCCR_IENx, true, &reg);
	if (st != 0)
		return lkpi_sdio_errno(st);
	return 0;
}

/* ------------------------------------------------------------------ */
/* the driver registry                                                 */
/* ------------------------------------------------------------------ */

/*
 * The id table match: an entry matches when every field it states agrees with
 * the function, and the "any" value matches everything (the interface's own
 * comparison). The table ends at an all-zero entry.
 */
static const struct sdio_device_id *
lkpi_sdio_match(const struct sdio_func *func, const struct sdio_driver *drv)
{
	const struct sdio_device_id *id;

	if (drv->id_table == NULL)
		return NULL;
	for (id = drv->id_table; id->class != 0u || id->vendor != 0u ||
	    id->device != 0u; id++) {
		if (id->class != (u8)SDIO_ANY_ID && id->class != func->class)
			continue;
		if (id->vendor != (u16)SDIO_ANY_ID && id->vendor != func->vendor)
			continue;
		if (id->device != (u16)SDIO_ANY_ID && id->device != func->device)
			continue;
		return id;
	}
	return NULL;
}

/* hand every unclaimed function to a matching driver; the first claim wins */
static int
lkpi_sdio_probe(struct sdio_driver *drv)
{
	unsigned int i;

	for (i = 1u; i <= (unsigned int)LKPI_SDIO_BE_MAX_FUNCS; i++) {
		struct sdio_func *f = &lkpi_sdio_card_state.func[i];
		const struct sdio_device_id *id;
		int ret;

		if (!sdio_func_present(f) || lkpi_sdio_card_state.claimed[i])
			continue;
		id = lkpi_sdio_match(f, drv);
		if (id == NULL)
			continue;
		ret = drv->probe(f, id);
		if (ret == 0) {
			lkpi_sdio_card_state.claimed[i] = true;
			lkpi_sdio_stats.probes++;
			lkpi_printf(ULOG_INFO_LEVEL,
			    "sdiokpi: '%s' claimed %04x:%04x function %u\n",
			    drv->name, f->vendor, f->device, f->num);
			return 0;
		}
	}
	return -ENODEV;
}

int
__sdio_register_driver(struct sdio_driver *drv, struct module *owner)
{

	(void)owner;
	if (drv == NULL || drv->probe == NULL)
		return -EINVAL;
	if (lkpi_sdio_driver_count >= LKPI_SDIO_DRIVERS_MAX)
		return -ENOSPC;
	lkpi_sdio_drivers[lkpi_sdio_driver_count++] = drv;
	lkpi_sdio_stats.drivers++;
	lkpi_printf(ULOG_INFO_LEVEL, "sdiokpi: driver '%s' registered\n", drv->name);
	/* a driver that arrives after the card is up is probed at once, which is
	 * what the interface's bus does */
	if (lkpi_sdio_card_state.present)
		(void)lkpi_sdio_probe(drv);
	return 0;
}

void
sdio_unregister_driver(struct sdio_driver *drv)
{
	unsigned int i;

	for (i = 0u; i < lkpi_sdio_driver_count; i++) {
		if (lkpi_sdio_drivers[i] == drv)
			lkpi_sdio_drivers[i] = NULL;
	}
}

/* ------------------------------------------------------------------ */
/* power management                                                    */
/* ------------------------------------------------------------------ */

mmc_pm_flag_t
sdio_get_host_pm_caps(struct sdio_func *func)
{

	if (func == NULL)
		return 0;
	return func->card->host->pm_caps;
}

int
sdio_set_host_pm_flags(struct sdio_func *func, mmc_pm_flag_t flags)
{

	if (func == NULL)
		return -EINVAL;
	if ((flags & ~func->card->host->pm_caps) != 0u)
		return -EINVAL;
	func->card->host->pm_flags = flags;
	return 0;
}

/* ------------------------------------------------------------------ */
/* bring-up                                                            */
/* ------------------------------------------------------------------ */

/*
 * Read a function's standard-interface code from its own FBR block, which lives
 * in function 0's address space. The interface reads this at function
 * initialisation; a card that does not answer leaves the class unstated, which
 * is what a driver's class-based id entry then fails to match on.
 */
static u8
lkpi_sdio_read_class(unsigned int num)
{
	u8 val = 0;

	if (lkpi_sdio_direct(0u, SDIO_FBR_BASE(num) + SDIO_FBR_STD_IF, false,
	    &val) != 0)
		return 0;
	return val;
}

static void
lkpi_sdio_build_functions(void)
{
	struct lkpi_sdio_card *c = &lkpi_sdio_card_state;
	unsigned int i;

	(void)memset(c->claimed, 0, sizeof(c->claimed));
	c->card.host = &c->host;
	c->card.type = MMC_TYPE_SDIO;
	c->card.state = MMC_STATE_PRESENT;
	c->card.ocr = c->info.ocr;
	c->card.sdio_funcs = c->info.func_count;
	c->card.major_rev = (u8)((c->info.cccr_version >> 4) & 0x0fu);
	c->card.minor_rev = (u8)(c->info.cccr_version & 0x0fu);
	c->card.cccr.sdio_vsn = c->info.sdio_version;
	c->card.cccr.multi_block = c->info.multi_block ? 1u : 0u;
	c->card.cccr.wide_bus = (c->info.bus_width == 4u) ? 1u : 0u;
	c->card.cccr.high_speed = c->info.high_speed ? 1u : 0u;

	c->host.card = &c->card;
	c->host.max_blk_size = c->info.host_max_blk_size;
	c->host.max_blk_count = c->info.host_max_blk_count;
	c->host.ios.clock = c->info.clock_hz;
	c->host.ios.power_mode = MMC_POWER_ON;
	c->host.ios.bus_mode = MMC_BUSMODE_PUSHPULL;
	c->host.ios.bus_width = (c->info.bus_width == 4u) ? MMC_BUS_WIDTH_4 :
	    MMC_BUS_WIDTH_1;
	c->host.ios.timing = c->info.high_speed ? MMC_TIMING_SD_HS :
	    MMC_TIMING_LEGACY;
	c->host.ios.signal_voltage = MMC_SIGNAL_VOLTAGE_330;
	/* the card is hard-wired and always powered on this port, so the host can
	 * keep it powered across a suspend */
	c->host.pm_caps = MMC_PM_KEEP_POWER;
	c->host.pm_flags = MMC_PM_KEEP_POWER;

	for (i = 0u; i <= (unsigned int)LKPI_SDIO_BE_MAX_FUNCS; i++) {
		struct sdio_func *f = &c->func[i];

		(void)memset(f, 0, sizeof(*f));
		f->card = &c->card;
		f->num = i;
		f->tmpbuf = c->tmpbuf[i];
		f->enable_timeout = LKPI_SDIO_ENABLE_TIMEOUT_MS;
		(void)snprintf(c->name[i], sizeof(c->name[i]), "sdio%u", i);
		f->dev.name = c->name[i];
		f->vendor = c->info.vendor;
		f->device = c->info.device;
		f->major_rev = (u8)((c->info.cccr_version >> 4) & 0x0fu);
		f->minor_rev = (u8)(c->info.cccr_version & 0x0fu);
	}

	/* function 0 is the card itself and is always there */
	c->func[0].class = SDIO_CLASS_NONE;
	c->func[0].max_blksize = c->info.fn0_max_block;
	c->func[0].cur_blksize = c->info.fn0_max_block != 0u ?
	    c->info.fn0_max_block : c->info.host_max_blk_size;
	sdio_func_set_present(&c->func[0]);

	for (i = 1u; i <= c->info.func_count &&
	    i <= (unsigned int)LKPI_SDIO_BE_MAX_FUNCS; i++) {
		struct sdio_func *f = &c->func[i];

		f->class = lkpi_sdio_read_class(i);
		f->max_blksize = c->info.fn_max_block[i - 1u];
		/* the block size in effect starts at the largest the function, the
		 * host and the interface's byte-mode ceiling all allow */
		f->cur_blksize = f->max_blksize != 0u ? f->max_blksize :
		    c->info.host_max_blk_size;
		if (f->cur_blksize > c->info.host_max_blk_size)
			f->cur_blksize = c->info.host_max_blk_size;
		if (f->cur_blksize > LKPI_SDIO_BYTE_MODE_MAX)
			f->cur_blksize = LKPI_SDIO_BYTE_MODE_MAX;
		sdio_func_set_present(f);
		c->card.sdio_func[i - 1u] = f;
	}
}

static void
lkpi_sdio_pump_start(void)
{
	struct lkpi_sdio_card *c = &lkpi_sdio_card_state;
	osThreadAttr_t attr = { 0 };

	if (c->pump_running)
		return;
	c->pump_mode = LKPI_SDIO_PUMP_MODE_FIXED;
	c->pump_period_ms = LKPI_SDIO_PUMP_IDLE_MS;
	c->pump_req = osSemaphoreNew(1u, 0u, NULL);
	c->pump_done = osSemaphoreNew(1u, 0u, NULL);
	if (c->pump_req == NULL || c->pump_done == NULL) {
		lkpi_printf(ULOG_ERROR_LEVEL, "sdiokpi: cannot create the pump semaphores\n");
		return;
	}
	attr.name = "lkpi_sdio_pump";
	attr.stack_size = LKPI_SDIO_PUMP_STACK;
	/*
	 * Below the normal band on purpose: the OS port creates threads without a
	 * time slice, so an equal-priority pump would run until it blocks and
	 * starve the workqueue thread that runs the driver's receive bottom half -
	 * the thread that recycles the receive buffers the pump is about to ask
	 * for. A hot pump then finds the buffer queue empty, the driver answers
	 * with its 10 ms shortage sleep, and that sleep becomes the cadence. Below
	 * normal, the consumers (bottom half, stack, transmit thread) always
	 * preempt the producer, and the claim's priority inheritance still lets a
	 * drain finish promptly when a higher thread wants the bus. The port that
	 * has a time slice may run the pump at the normal band instead - and the
	 * round that measures that trade does it through pump_prio_set, which
	 * moves the thread at runtime and keeps this default the shape every
	 * earlier measurement ran.
	 */
	attr.priority = c->pump_prio_normal ? osPriorityNormal :
					      osPriorityBelowNormal;
	c->pump = osThreadNew(lkpi_sdio_pump_entry, c, &attr);
	if (c->pump == NULL) {
		lkpi_printf(ULOG_ERROR_LEVEL, "sdiokpi: cannot start the pump thread\n");
		return;
	}
	c->pump_running = true;
}

void
lkpi_sdio_start(void)
{
	struct lkpi_sdio_card *c = &lkpi_sdio_card_state;
	unsigned int i;
	int st;

	if (lkpi_sdio_started)
		return;
	lkpi_sdio_started = true;

	if (!lkpi_sdio_be_ready()) {
		lkpi_printf(ULOG_ERROR_LEVEL,
		    "sdiokpi: no backend installed; the card cannot come up\n");
		return;
	}

	(void)memset(&c->info, 0, sizeof(c->info));
	st = lkpi_sdio_be_card_init(&c->info);
	if (st != 0) {
		lkpi_printf(ULOG_ERROR_LEVEL, "sdiokpi: card bring-up failed (%d)\n", st);
		return;
	}
	c->present = true;
	lkpi_sdio_stats.cards++;

	c->claim = osMutexNew(NULL);
	if (c->claim == NULL) {
		lkpi_printf(ULOG_ERROR_LEVEL, "sdiokpi: cannot create the bus claim\n");
		c->present = false;
		return;
	}

	lkpi_sdio_build_functions();
	lkpi_sdio_pump_start();

	/* a driver registered before the card arrived is probed now */
	for (i = 0u; i < lkpi_sdio_driver_count; i++) {
		if (lkpi_sdio_drivers[i] != NULL)
			(void)lkpi_sdio_probe(lkpi_sdio_drivers[i]);
	}

	lkpi_printf(ULOG_INFO_LEVEL,
	    "sdiokpi: card %04x:%04x, %u IO func(s), CCCR v%u.%u, bus %u kHz (%s)\n",
	    c->info.vendor, c->info.device, c->info.func_count,
	    (unsigned int)((c->info.cccr_version >> 4) & 0x0fu),
	    (unsigned int)(c->info.cccr_version & 0x0fu), c->info.clock_hz,
	    c->info.high_speed ? "HS" : "default");
}

int
lkpi_sdio_ready(void)
{

	return lkpi_sdio_card_state.present ? 1 : 0;
}

struct sdio_func *
lkpi_sdio_func(unsigned int num)
{

	if (!lkpi_sdio_card_state.present ||
	    num > (unsigned int)LKPI_SDIO_BE_MAX_FUNCS)
		return NULL;
	if (!sdio_func_present(&lkpi_sdio_card_state.func[num]))
		return NULL;
	return &lkpi_sdio_card_state.func[num];
}

struct sdio_func *
lkpi_sdio_func0(void)
{

	return lkpi_sdio_func(0u);
}

void
lkpi_sdio_stats_get(struct lkpi_sdio_stats *out)
{

	if (out == NULL)
		return;
	*out = lkpi_sdio_stats;
}
