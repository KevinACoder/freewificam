/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * judge.h - the six judgement rules of the stress harness.
 *
 * The judge is black-box: it reads the seam's own counters
 * (lkpi_usb_mem_stats_get(), the per-urb completion counters the harness keeps
 * at the driver face) and never the engine's internals.  Six named rules:
 *
 *   exactly-once    one completion per accepted submit, at the seam and at the
 *                   driver callback, over a batch
 *   error-code      the programmed transfer's expected errno vs the observed
 *                   completion status
 *   kill-sync       the killer returns only after the completion ran
 *   uaf-named       a use-after-free is named by the seam counters, never
 *                   silent (the counters are read as deltas around the step)
 *   alloc-balance   n_urb_alloc - n_urb_free == 0 and the coherent pair
 *                   likewise, at the end of every cell and fuzz block
 *   no-deadlock     the cell / block finished inside its step budget
 *
 * On a violation the judge prints the first witness in the harness's fixed
 * format and exits 1:
 *
 *   URBSTRESS: first-witness rule=<name> cell=<id> urb=<ptr> want=<v> got=<v> op=<index>
 *
 * followed by the failure set (the urbs the run has named), the cell's FAIL
 * line, and the minimal repro command for the failing scenario.
 */

#ifndef JUDGE_H
#define JUDGE_H

#include "linuxkpi_usb.h"

#define JUDGE_RULE_EXACTLY_ONCE	"exactly-once"
#define JUDGE_RULE_ERROR_CODE	"error-code"
#define JUDGE_RULE_KILL_SYNC	"kill-sync"
#define JUDGE_RULE_UAF_NAMED	"uaf-named"
#define JUDGE_RULE_BALANCE	"alloc-balance"
#define JUDGE_RULE_NO_DEADLOCK	"no-deadlock"

/* cell / block lifecycle */
void	judge_begin(const char *id);
/* FAIL verdicts so far this run (the entry point's exit condition) */
int	judge_fail_count(void);
void	judge_note(const char *fmt, ...);
int	judge_cell_end(int ok);	/* prints "<id> PASS|FAIL <detail>" */
void	judge_summary(void);

/* the general funnel (used by the six rules below and by direct expectations) */
void	judge_check(int cond, const char *rule, const void *urb, long want,
	    long got);

/* rule 1: exactly-once completion per urb.  The driver face is the primary
 * count (one callback per accepted submission); the seam face is a bound
 * (the backend never gives the same transfer back more times than it was
 * submitted - a submit that dies inside the engine, e.g. a kill of a urb the
 * endpoint FIFO still held, never reaches the seam at all). */
void	judge_rule_exactly_once(const void *urb, long want_cb, long got_cb,
	    long want_seam_max, long got_seam);

/* a cell expectation that is not one of the six rules (the witness line keeps
 * the same shape, with rule="cell-expect") */
void	judge_expect(int cond, const char *what, const void *urb, long want,
	    long got);
/* rule 2: expected vs observed completion status */
void	judge_rule_error_code(const void *urb, long want, long got);
/* rule 3: the completion ran before the killer returned */
void	judge_rule_kill_sync(const void *urb, int cb_ran_before_return);
/* rule 4: the named counters moved exactly as declared (never silent) */
void	judge_rule_uaf_named(const void *urb,
	    const struct lkpi_usb_mem_stats *before,
	    const struct lkpi_usb_mem_stats *after, unsigned int want_double_free,
	    unsigned int want_stray_free, unsigned int want_uaf_write,
	    unsigned int want_alloc_fail);
/* rule 5: allocation pairing */
void	judge_rule_balance(const struct lkpi_usb_mem_stats *st);
/* rule 6: the step budget was not exhausted */
void	judge_rule_no_deadlock(unsigned int steps_used, unsigned int budget);

/* the repro line printed with a violation (set once by the entry point) */
void	judge_repro_set(const char *text);

#endif /* JUDGE_H */
