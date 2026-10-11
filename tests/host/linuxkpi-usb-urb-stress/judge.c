/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * judge.c - the six judgement rules (black box: seam counters + driver face).
 *
 * Every expectation in this harness funnels through judge_check(): the caller
 * states the rule it is checking, the urb the witness belongs to, and the
 * wanted/observed values; the judge keeps the first witness of the run and
 * exits 1 immediately, so a failing run stops exactly where it first saw the
 * violation (that is also what makes --replay stop at the same index).
 *
 * The evidence the rules read:
 *   - the seam's allocation counters and named refusals, through the engine's
 *     own accessors (linuxkpi_usb.h lkpi_usb_mem_stats_get);
 *   - the per-urb completion counts the environment wrapper keeps at the
 *     driver face and at the seam face (engine_env.h);
 *   - the harness's step counter (no-deadlock).
 * No rule reads an engine-internal object.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine_env.h"
#include "judge.h"

#define JUDGE_FAILSET_MAX	16

static const char *g_cell = "-";
static int g_cell_ok = 1;
static char g_detail[512];
static unsigned int g_cell_fail;
static unsigned int g_cell_pass;
static unsigned int g_failset_n;
static const void *g_failset[JUDGE_FAILSET_MAX];
static int g_exiting;
static const char *g_repro;
static unsigned int g_pairing_urbs;	/* urbs the exactly-once rule judged */

void
judge_repro_set(const char *text)
{

	g_repro = text;
}

void
judge_begin(const char *id)
{

	g_cell = id;
	g_cell_ok = 1;
	g_detail[0] = '\0';
}

/* append to the cell's detail buffer (the FAIL/PASS line carries it) */
void
judge_note(const char *fmt, ...)
{
	va_list ap;
	size_t at = strlen(g_detail);
	size_t room;

	if (at >= sizeof(g_detail) - 1u)
		return;
	room = sizeof(g_detail) - at - 1u;
	g_detail[at++] = ' ';
	va_start(ap, fmt);
	(void)vsnprintf(g_detail + at, room - 1u, fmt, ap);
	va_end(ap);
}

static void
failset_add(const void *urb)
{
	unsigned int i;

	if (urb == NULL)
		return;
	for (i = 0; i < g_failset_n; i++) {
		if (g_failset[i] == urb)
			return;
	}
	if (g_failset_n < JUDGE_FAILSET_MAX)
		g_failset[g_failset_n++] = urb;
}

static void
failset_print(void)
{
	unsigned int i;

	printf("URBSTRESS: failset n=%u", g_failset_n);
	for (i = 0; i < g_failset_n; i++)
		printf(" urb=%p", g_failset[i]);
	printf("\n");
	fflush(stdout);
}

int
judge_cell_end(int ok)
{

	g_cell_ok = ok;
	if (ok) {
		g_cell_pass++;
		printf("URBSTRESS: %s PASS%s\n", g_cell,
		    g_detail[0] != '\0' ? g_detail : "");
	} else {
		g_cell_fail++;
		printf("URBSTRESS: %s FAIL%s\n", g_cell,
		    g_detail[0] != '\0' ? g_detail : "");
	}
	fflush(stdout);
	g_cell = "-";
	g_detail[0] = '\0';
	return ok;
}

int
judge_fail_count(void)
{

	return (int)g_cell_fail;
}

void
judge_summary(void)
{
	struct lkpi_usb_mem_stats st;

	env_stats(&st);
	printf("URBSTRESS: matrix cells=%u pass=%u fail=%u\n",
	    g_cell_pass + g_cell_fail, g_cell_pass, g_cell_fail);
	/* the two bookkeeping anchors: the rules' own evidence, printed whether
	 * the cells passed or failed (a wrapper can grep for them to prove the
	 * exactly-once and allocation-balance checks were run, not implied) */
	printf("URBSTRESS: pairing cells=%u callbacks_per_submit=1 "
	    "witness_urbs=%u\n", g_cell_pass + g_cell_fail, g_pairing_urbs);
	printf("URBSTRESS: balance n_urb_alloc=%u n_urb_free=%u "
	    "n_coherent_alloc=%u n_coherent_free=%u delta=%d\n",
	    st.n_urb_alloc, st.n_urb_free, st.n_coherent_alloc,
	    st.n_coherent_free,
	    (int)((st.n_urb_alloc - st.n_urb_free) +
	    (st.n_coherent_alloc - st.n_coherent_free)));
	fflush(stdout);
}

void
judge_check(int cond, const char *rule, const void *urb, long want, long got)
{

	if (cond)
		return;
	failset_add(urb);
	fprintf(stderr,
	    "URBSTRESS: first-witness rule=%s cell=%s urb=%p want=%ld got=%ld "
	    "op=%u\n", rule, g_cell, urb, want, got, env_op());
	failset_print();
	printf("URBSTRESS: %s FAIL <rule %s: urb=%p want=%ld got=%ld>\n",
	    g_cell, rule, urb, want, got);
	if (g_repro != NULL)
		printf("URBSTRESS: repro %s\n", g_repro);
	fflush(stdout);
	fflush(stderr);
	/* the first witness ends the run: every later observation would be a
	 * consequence of this one, and --replay must stop at the same index */
	g_exiting = 1;
	(void)g_exiting;
	exit(1);
}

/* ------------------------------------------------------------------ */
/* rule 1: exactly-once completion per urb                             */
/* ------------------------------------------------------------------ */

void
judge_expect(int cond, const char *what, const void *urb, long want, long got)
{

	if (cond)
		return;
	judge_note("%s", what);
	judge_check(0, "cell-expect", urb, want, got);
}

void
judge_rule_exactly_once(const void *urb, long want_cb, long got_cb,
    long want_seam_max, long got_seam)
{

	if (urb != NULL)
		g_pairing_urbs++;
	judge_check(got_cb == want_cb, JUDGE_RULE_EXACTLY_ONCE, urb,
	    want_cb, got_cb);
	judge_check(got_seam <= want_seam_max, JUDGE_RULE_EXACTLY_ONCE, urb,
	    want_seam_max, got_seam);
}

/* ------------------------------------------------------------------ */
/* rule 2: the completion status matches the programmed transfer       */
/* ------------------------------------------------------------------ */

void
judge_rule_error_code(const void *urb, long want, long got)
{

	judge_check(got == want, JUDGE_RULE_ERROR_CODE, urb, want, got);
}

/* ------------------------------------------------------------------ */
/* rule 3: the killer returns only after the completion ran            */
/* ------------------------------------------------------------------ */

void
judge_rule_kill_sync(const void *urb, int cb_ran_before_return)
{

	judge_check(cb_ran_before_return != 0, JUDGE_RULE_KILL_SYNC, urb, 1,
	    cb_ran_before_return);
}

/* ------------------------------------------------------------------ */
/* rule 4: a use-after-free is named by the seam counters, never silent */
/* ------------------------------------------------------------------ */

void
judge_rule_uaf_named(const void *urb, const struct lkpi_usb_mem_stats *before,
    const struct lkpi_usb_mem_stats *after, unsigned int want_double_free,
    unsigned int want_stray_free, unsigned int want_uaf_write,
    unsigned int want_alloc_fail)
{
	unsigned int got;

	got = after->double_free - before->double_free;
	judge_check(got == want_double_free, JUDGE_RULE_UAF_NAMED, urb,
	    (long)want_double_free, (long)got);
	got = after->stray_free - before->stray_free;
	judge_check(got == want_stray_free, JUDGE_RULE_UAF_NAMED, urb,
	    (long)want_stray_free, (long)got);
	got = after->uaf_write - before->uaf_write;
	judge_check(got == want_uaf_write, JUDGE_RULE_UAF_NAMED, urb,
	    (long)want_uaf_write, (long)got);
	got = after->alloc_fail - before->alloc_fail;
	judge_check(got == want_alloc_fail, JUDGE_RULE_UAF_NAMED, urb,
	    (long)want_alloc_fail, (long)got);
}

/* ------------------------------------------------------------------ */
/* rule 5: allocation pairing at the end of every cell / fuzz block     */
/* ------------------------------------------------------------------ */

void
judge_rule_balance(const struct lkpi_usb_mem_stats *st)
{

	judge_check(st->n_urb_alloc == st->n_urb_free, JUDGE_RULE_BALANCE, NULL,
	    (long)st->n_urb_alloc, (long)st->n_urb_free);
	judge_check(st->n_coherent_alloc == st->n_coherent_free,
	    JUDGE_RULE_BALANCE, NULL, (long)st->n_coherent_alloc,
	    (long)st->n_coherent_free);
}

/* ------------------------------------------------------------------ */
/* rule 6: the run stayed inside its step budget (no deadlock)          */
/* ------------------------------------------------------------------ */

void
judge_rule_no_deadlock(unsigned int steps_used, unsigned int budget)
{

	judge_check(steps_used < budget, JUDGE_RULE_NO_DEADLOCK, NULL,
	    (long)budget, (long)steps_used);
}
