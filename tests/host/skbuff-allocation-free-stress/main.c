/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * main.c - the skbuff contract harness entry point.
 *
 * Modes (all output lines carry the SKBST: prefix, results go to stdout):
 *
 *   (default)                 the scenario matrix only: one line per cell and
 *                             a summary line, exit 0 only when every cell held
 *   --fuzz [--fuzz-iters M]   the matrix, then the three default seeds 1, 2, 3
 *                             with M iterations each (default 200000) and one
 *                             line per seed carrying the run digest
 *   --profile mcu             run under the constrained geometry (8 header
 *                             slots, 8 data slots of 256 bytes, no heap
 *                             fallback); combined with --fuzz for the MCU run
 *   --replay <seed> <n>       replay only the first n operations of that seed
 *   --list-cells              print the nineteen cell ids
 *   --selftest-must-red       the harness self-proof: a fixed misuse stream
 *                             whose expectation is deliberately wrong must go
 *                             red, and its own replay of the same prefix must
 *                             land on the same operation index.  The process
 *                             exits non-zero by design; a control suite runs
 *                             this as the "the harness can fail" case
 *   --verbose                 also print the pool's own refusal diagnostics
 *
 * The build carries ASan and UBSan: any sanitizer report is a failure of the
 * run even when the SKBST: lines look clean (see README.md).
 */

#include <errno.h>
#include <inttypes.h>

#include "judge.h"

#define DEFAULT_ITERS	200000ul
#define DEFAULT_SEEDS	3

static const struct skb_pool_cfg mcu_cfg = {
	.n_header = 8, .n_data = 8, .data_sz = 256, .heap_fallback = 0
};

static void
usage(const char *argv0)
{

	printf("usage: %s [--fuzz] [--fuzz-seed N] [--fuzz-iters M]\n"
	    "          [--profile host|mcu] [--replay <seed> <n_ops>]"
	    " [--list-cells] [--verbose]\n", argv0);
}

static int
parse_ul(const char *s, unsigned long *out)
{
	char *end = NULL;
	unsigned long v;

	if (s == NULL || *s == '\0')
		return -1;
	errno = 0;
	v = strtoul(s, &end, 0);
	if (errno != 0 || end == NULL || *end != '\0')
		return -1;
	*out = v;
	return 0;
}

static int
parse_u64(const char *s, uint64_t *out)
{
	char *end = NULL;
	unsigned long long v;

	if (s == NULL || *s == '\0')
		return -1;
	errno = 0;
	v = strtoull(s, &end, 0);
	if (errno != 0 || end == NULL || *end != '\0')
		return -1;
	*out = (uint64_t)v;
	return 0;
}

int
main(int argc, char **argv)
{
	int want_fuzz = 0;
	int want_matrix = 1;
	int replay = 0;
	int selftest = 0;
	int profile_mcu = 0;
	unsigned long iters = DEFAULT_ITERS;
	int verbose = 0;
	uint64_t seed = 0;
	unsigned long replay_ops = 0;
	int have_explicit_seed = 0;
	int failures = 0;
	int i;

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (strcmp(a, "--fuzz") == 0) {
			want_fuzz = 1;
		} else if (strcmp(a, "--fuzz-seed") == 0 && i + 1 < argc) {
			want_fuzz = 1;
			if (parse_u64(argv[++i], &seed) != 0) {
				printf("SKBST: bad seed %s\n", argv[i]);
				return 2;
			}
			have_explicit_seed = 1;
		} else if (strcmp(a, "--fuzz-iters") == 0 && i + 1 < argc) {
			want_fuzz = 1;
			if (parse_ul(argv[++i], &iters) != 0 || iters == 0) {
				printf("SKBST: bad iteration count %s\n", argv[i]);
				return 2;
			}
		} else if (strcmp(a, "--profile") == 0 && i + 1 < argc) {
			const char *p = argv[++i];

			if (strcmp(p, "mcu") == 0)
				profile_mcu = 1;
			else if (strcmp(p, "host") == 0)
				profile_mcu = 0;
			else {
				printf("SKBST: unknown profile %s\n", p);
				return 2;
			}
		} else if (strcmp(a, "--replay") == 0 && i + 2 < argc) {
			if (parse_u64(argv[i + 1], &seed) != 0 ||
			    parse_ul(argv[i + 2], &replay_ops) != 0) {
				printf("SKBST: bad replay arguments\n");
				return 2;
			}
			i += 2;
			replay = 1;
			have_explicit_seed = 1;
			iters = replay_ops;
		} else if (strcmp(a, "--selftest-must-red") == 0) {
			selftest = 1;
		} else if (strcmp(a, "--verbose") == 0) {
			verbose = 1;
		} else if (strcmp(a, "--list-cells") == 0) {
			const char *const *ids;
			unsigned int n, k;

			ids = skb_matrix_cell_ids(&n);
			for (k = 0; k < n; k++)
				printf("SKBST: cell %s\n", ids[k]);
			return 0;
		} else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
			usage(argv[0]);
			return 0;
		} else {
			printf("SKBST: unknown argument %s\n", a);
			usage(argv[0]);
			return 2;
		}
	}

	if (replay || selftest)
		want_matrix = 0;

	if (selftest) {
		unsigned long fail_at = 0;
		unsigned long seen = 0;

		skb_double_set_verbose(verbose);
		if (profile_mcu)
			skb_double_init_cfg(&mcu_cfg);
		else
			skb_double_init(SKB_PROFILE_HOST);

		if (!skb_fuzz_selftest_must_red(seed != 0 ? seed : 1, iters,
		    &fail_at, 0)) {
			printf("SKBST: selftest seed=%" PRIu64 " UNEXPECTED-GREEN ok=0\n",
			    seed != 0 ? seed : 1);
			return 3;
		}
		if (!skb_fuzz_selftest_must_red(seed != 0 ? seed : 1, fail_at + 1,
		    &seen, 1) || seen != fail_at) {
			printf("SKBST: replay seed=%" PRIu64 " op=%lu expected-index=%lu ok=0\n",
			    seed != 0 ? seed : 1, seen, fail_at);
			return 4;
		}
		printf("SKBST: replay seed=%" PRIu64 " op=%lu expected-index=%lu ok=1\n",
		    seed != 0 ? seed : 1, seen, fail_at);
		/* the control of this mode: it must fail, so the exit is non-zero */
		return 1;
	}

	skb_double_set_verbose(verbose);
	if (profile_mcu)
		skb_double_init_cfg(&mcu_cfg);
	else
		skb_double_init(SKB_PROFILE_HOST);

	printf("SKBST: profile=%s header=%u data=%u data_sz=%u heap_fallback=%d\n",
	    skb_double_profile_name(), skb_double_cfg()->n_header,
	    skb_double_cfg()->n_data, skb_double_cfg()->data_sz,
	    skb_double_cfg()->heap_fallback);

	if (want_matrix)
		failures += skb_matrix_run();

	if (replay) {
		struct skb_fuzz_result res;

		skb_double_init(profile_mcu ? SKB_PROFILE_MCU : SKB_PROFILE_HOST);
		if (skb_fuzz_run(seed, iters, &res) != 0) {
			printf("SKBST: replay seed=%" PRIu64 " ops=%lu FAIL\n",
			    seed, res.ops);
			return 1;
		}
		printf("SKBST: replay seed=%" PRIu64 " ops=%lu digest=%016" PRIx64 " ok=1\n",
		    seed, res.ops, res.digest);
		return 0;
	}

	if (want_fuzz) {
		int s;

		for (s = 0; s < DEFAULT_SEEDS; s++) {
			struct skb_fuzz_result res;
			uint64_t use = have_explicit_seed && s == 0 ? seed
			    : (uint64_t)(s + 1);

			if (have_explicit_seed && s > 0)
				break;
			skb_double_init(profile_mcu ? SKB_PROFILE_MCU
						    : SKB_PROFILE_HOST);
			if (skb_fuzz_run(use, iters, &res) != 0) {
				printf("SKBST: fuzz seed=%" PRIu64 " iters=%lu ops=%lu FAIL\n",
				    use, iters, res.ops);
				failures++;
				continue;
			}
			printf("SKBST: fuzz seed=%" PRIu64 " iters=%lu ops=%lu digest=%016" PRIx64 " ok=1\n",
			    use, iters, res.ops, res.digest);
		}
	}

	return failures == 0 ? 0 : 1;
}
