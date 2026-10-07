/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * engine_env.h - the stress harness's environment wrapper.
 *
 * The wrapper stands between the urb engine (components/linuxkpi/usb) and the
 * synthetic controller of the sibling host suite
 * (tests/host/linuxkpi-usb-test).  It brings both up, registers the clean-room
 * command-driver stand-in through the public driver registration, and adds the
 * injection knobs this round needs:
 *
 *   - completion timing   (immediate / delayed K harness steps / never),
 *   - completion order    (FIFO / reverse / permutation over one step's batch),
 *   - per-transfer error  (a seam code the transfer reports instead of moving),
 *   - double completion   (the must-go-red self-proof for the judgement rules),
 *   - a never-answering synchronous control lane (the -ETIMEDOUT source),
 * all expressed by programming the existing controller facilities - the
 * controller's own worker is held, so every transfer completes when the
 * harness's deterministic step counter says so, never on a wall clock.
 *
 * The wrapper is a pure adapter over the seam (lkpi_usb_backend.h): it hands a
 * per-transfer copy of the backend urb to the controller, funnels every
 * completion through one place, and mirrors the controller's own kill
 * semantics for transfers it has not handed over yet.
 *
 * Scheduling contract the harness relies on (documented, not assumed): the
 * harness task runs below the engine's drain thread and below the controller's
 * worker, and the controller worker is held; therefore a completion committed
 * by the harness is delivered by the drain before the harness runs again, and
 * every harness step observes a settleable world.
 */

#ifndef ENGINE_ENV_H
#define ENGINE_ENV_H

#include "os/cmsis2/cmsis2.h"
#include <linux/usb.h>

#include "linuxkpi_usb.h"
#include "lkpi_usb_backend.h"
#include "mock_hcd.h"

/* the tracked urb rows */
#define ENV_MAX_URBS	64
/* one per-row buffer arena: wide enough for the widest control stage the cells
 * stage (the 1024-byte short-read shape) */
#define ENV_ARENA	2048u

/* the settle loop's step ceiling (judge rule 6's budget) */
#define ENV_STEP_BUDGET	4096u

/* the endpoints the harness addresses (the peer's two bulk endpoints plus the
 * control endpoint the engine stages on) */
enum env_ep {
	ENV_EP_BULK_IN = 0,
	ENV_EP_BULK_OUT,
	ENV_EP_CTRL,
	ENV_EP_N,
};

/* one tracked urb row: the harness-side record of a urb's life.  It is a
 * separate object from the urb on purpose - a row survives the urb's release,
 * so a misuse step can name a released pointer without touching it. */
struct stress_urb {
	struct urb	*u;
	int		 tracked;
	int		 submits;	/* accepted submits (each owes one completion) */
	int		 seam_count;	/* completions the seam delivered */
	int		 cb_count;	/* completions the driver callback observed */
	int		 cb_first;	/* first observed status */
	int		 cb_last;	/* last observed status */
	osThreadId_t	 cb_tid;	/* thread the callback ran on */
	unsigned int	 cb_seq;	/* completion order index */
	int		 want_count;	/* declared expectation (judge rule 1) */
	int		 want_status;	/* declared expectation (judge rule 2),
					 * INT_MIN = any lawful status */
	/* optional per-urb observation hook, run at the end of the callback:
	 * the reentrancy cells live here (free/kill/resubmit inside a callback) */
	void		(*observe)(struct stress_urb *, struct urb *);
	void		*observe_arg;
	/* harness bookkeeping for the misuse cells */
	int		 dead_marked;	/* the guard's poison id was applied */
	u32		 saved_alloc_id;	/* the id restored after the marker */
	int		 freed;		/* usb_free_urb() was called */
};

/* bring-up: controller + device + driver stand-in + the engine seam.  0 = ok. */
int		 env_init(void);
struct mock_backend *env_backend(void);
struct usb_device *env_device(void);

/* ------------------------------------------------------------------ */
/* knobs (test-side parameters; sticky until env_reset_knobs)           */
/* ------------------------------------------------------------------ */

void	env_reset_knobs(void);
void	env_knob_delay(unsigned int steps);	/* due at clock + steps + 1 */
void	env_knob_park(void);			/* never completes: kill must drain it */
void	env_knob_passthrough(void);		/* into the controller queue at submit */
void	env_knob_error(int seam_code);		/* this transfer reports seam_code */
void	env_knob_order(unsigned int mode);	/* ENV_ORDER_* */
void	env_knob_double_complete(void);		/* give the next transfer back twice */
void	env_knob_control_timeout(int on);	/* sync control -> seam TIMEOUT */

enum env_order {
	ENV_ORDER_FIFO = 0,
	ENV_ORDER_REVERSE,
	ENV_ORDER_PERM,
};

/* ------------------------------------------------------------------ */
/* urb lifecycle (the engine's public face is used by the callers)      */
/* ------------------------------------------------------------------ */

struct urb	*env_alloc(struct stress_urb *s, int want_count, int want_status);
void	env_track(struct stress_urb *s, struct urb *u, int want_count,
		    int want_status);
void	env_forget(struct stress_urb *s);	/* drop the row, keep the urb */
void	env_free_urb(struct stress_urb *s);	/* usb_free_urb + untrack */

/* fill helpers: they also register the row's buffer with the wrapper, which
 * maps the engine's backend urb back to the row by its transfer identity */
void	env_fill_bulk(struct stress_urb *s, unsigned int ep, void *buf,
		    unsigned int len, int in);
void	env_fill_control(struct stress_urb *s, void *setup, void *buf,
		    unsigned int len);
int	env_submit(struct stress_urb *s);	/* usb_submit_urb + accounting */
/* re-attach the harness's driver-facing completion callback to the row's urb
 * (a cell that refilled the urb by hand after a sync probe uses this) */
void	env_attach_cb(struct stress_urb *s);

/* a caller that released the urb itself (a completion-callback free, the B6.3
 * shape) tells the wrapper, so no later condition inspects the released block */
void	env_note_released(struct stress_urb *s);
/* the guard's own dead-urb marker, applied to live storage on purpose: the
 * guard's contract is the poison id, and this keeps the cell free of the
 * access-after-release the guard itself performs (recorded in the cell) */
void	env_mark_dead_id(struct stress_urb *s);
void	env_clear_dead_id(struct stress_urb *s);

/* ------------------------------------------------------------------ */
/* the deterministic clock                                             */
/* ------------------------------------------------------------------ */

/* advance one step: forward every transfer whose due has arrived, then run the
 * controller queue empty.  No sleeps, no wall clock. */
void	env_step(void);
/* drive steps until nothing is left to commit and every committed completion
 * has been delivered; returns the steps used (0..ENV_STEP_BUDGET) */
unsigned int	env_settle(void);
/* bounded wait for the driver callbacks to catch up with the seam */
int	env_wait_cb(unsigned int total);
/* settle, then leave every row quiesced: kill what still owes a completion,
 * settle again, wait for the callbacks.  Called before a cell's final
 * accounting so the judge reads a settled world. */
int	env_drain_all(void);
unsigned int	env_cb_total(void);
unsigned int	env_seam_total(void);
unsigned int	env_pending_live(void);
unsigned int	env_at_controller(void);
unsigned int	env_clock_now(void);

/* judgement rule 3's predicate, sampled at a killer's return: the completion
 * this transfer owed has already run.  The engine publishes the quiesced state
 * only after its delivery chain ran the driver callback (linuxkpi_usb.c
 * lkpi_usb_drain_one: LKPI_Q_IDLE after lkpi_usb_deliver), whichever context
 * the delivery used - the seam's killer-context cancel, the engine's inline
 * FIFO cancel, or the drain thread.  So the predicate is the driver-face
 * count, and the seam tally is only a note (a FIFO-held urb never reaches the
 * seam at all). */
int	env_kill_ran_sync(const struct stress_urb *s);
void	env_op_bump(void);

/* ------------------------------------------------------------------ */
/* engine-side helpers                                                 */
/* ------------------------------------------------------------------ */

void	env_stats(struct lkpi_usb_mem_stats *out);
int	env_force_sync(int on);
struct stress_urb *env_row(const struct urb *u);	/* NULL when untracked */
struct stress_urb *env_row_n(unsigned int i);
unsigned int	env_row_count(void);
void	env_row_reset(struct stress_urb *s);

/* the seam the harness itself probes (the raw controller ops) */
const struct lkpi_usb_be_ops *env_mock_ops(void);
void	*env_mock_ctx(void);

/* the harness's own device/buffer arenas (one per row, so the wrapper's
 * transfer-identity mapping is unambiguous) */
u8	*env_buf(unsigned int row, unsigned int len);

#endif /* ENGINE_ENV_H */
