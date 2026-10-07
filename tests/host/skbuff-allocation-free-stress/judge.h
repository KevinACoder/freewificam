/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * judge.h - the invariant judge and the two driver entry points.
 *
 * After every operation the judge re-checks four rule families, all of them
 * read off the production headers rather than off the double:
 *
 *   1. geometry:  0 <= data - head <= tail <= end and len == tail -
 *                 (data - head) with data_len == 0, for every live skb
 *                 (skbuff_pool.h line 14, linuxkpi_core.h lines 2057-2058).
 *   2. pairing:   n_alloc - n_free equals the number of live skbs, and the
 *                 used counters equal the pool's actual occupancy
 *                 (skbuff_pool.h lines 85-98).
 *   3. named:     double_free / stray_free / uaf_write / fence_bad move by
 *                 exactly the amount the operation stream says they should,
 *                 and by nothing otherwise (skbuff_pool.h lines 94-97).
 *   4. queue:     qlen equals enqueued - dequeued - unlinked, the chain has
 *                 no cycle and every queued skb still carries an owner
 *                 generation (linuxkpi_core.h struct sk_buff_head, line
 *                 1793-1798, and the ->list tracking note at line 1885-1888).
 *
 * On the first violation the judge prints
 *   SKBST: first-witness op=<index> kind=<family> detail=<...>
 * and the caller stops; the process exit code follows.
 */
#ifndef SKB_JUDGE_H
#define SKB_JUDGE_H

#include "skb_contract.h"

struct skb_bad {
	char	kind[24];
	char	detail[220];
};

/* drop the attached queue and the pre-op snapshot */
void judge_reset(void);
/* suppress the witness line only (the failure is still reported to the
 * caller); used by the self-proof's replay phase, which prints its own
 * summary line instead */
void judge_set_quiet(int on);
/* the queue under test plus the stream's occupancy bookkeeping */
void judge_attach(struct sk_buff_head *q, const unsigned long *enqueued,
    const unsigned long *dequeued, const unsigned long *unlinked);

/* snapshot before the operation, judge after it; returns -1 when the
 * operation was the first violation (the witness line is already printed) */
void judge_pre(void);
int judge_post(unsigned long op_index, const char *op_kind,
    int expect_double_free, int expect_stray_free, int expect_uaf_write,
    int expect_fence_bad);

/* the four families, also usable stand-alone by the matrix cells */
int judge_check_geometry(struct skb_bad *bad);
int judge_check_pairing(struct skb_bad *bad);
int judge_check_queue(struct skb_bad *bad);
int judge_check_named(struct skb_bad *bad,
    const struct lkpi_skb_pool_stats *before,
    const struct lkpi_skb_pool_stats *after,
    int expect_double_free, int expect_stray_free, int expect_uaf_write,
    int expect_fence_bad);

/* print the first-witness line; also used by the fuzz's own model checks */
void judge_witness(unsigned long op_index, const char *kind, const char *fmt,
    ...) __attribute__((format(printf, 3, 4)));

/* ------------------------------------------------------------------ */
/* the two drivers main.c reaches                                      */
/* ------------------------------------------------------------------ */

/* cases.c: run the whole matrix, print one line per cell and the summary;
 * returns the number of failing cells (the summary line is printed here) */
int skb_matrix_run(void);
/* the cells' ids, for --list-cells */
const char *const *skb_matrix_cell_ids(unsigned int *count);

/* fuzz.c: run iters operations of the seed against the current profile */
struct skb_fuzz_result {
	unsigned long	ops;		/* operations executed */
	uint64_t	digest;		/* pool + stream digest */
};
int skb_fuzz_run(uint64_t seed, unsigned long iters,
    struct skb_fuzz_result *out);

/*
 * The harness self-proof: run the fixed misuse stream of the seed with one
 * expectation deliberately wrong (a named double free judged as if it must
 * not be counted), so the run has to go red and stop at the first double-free
 * operation.  Returns 1 when the witness line was printed and stores the
 * failing operation index, or 0 when the stream unexpectedly stayed green.
 * The witness report is the first-witness line alone.
 */
int skb_fuzz_selftest_must_red(uint64_t seed, unsigned long budget,
    unsigned long *fail_op, int replay_phase);

#endif /* SKB_JUDGE_H */
