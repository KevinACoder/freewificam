/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   kpi_test.h
 * @brief  The host semantic bench's whole framework: three assertions, one
 *         case record, two anchors.
 *
 * A case returns KPI_PASS (0), KPI_FAIL (1) or KPI_SKIP (2).  An assertion
 * prints its file:line and returns failure from the case; there is no fixture,
 * no registry macro and no external dependency, on purpose - the bench exists
 * to hold semantics, not to grow a test framework
 * (docs/HANDOFF-S0-HOST.md §3.2).
 */
#ifndef KPI_TEST_H
#define KPI_TEST_H

#include <stdint.h>
#include <stdio.h>

enum {
	KPI_PASS = 0,
	KPI_FAIL = 1,
	KPI_SKIP = 2,
};

struct kpi_case {
	const char *name;
	int (*run)(void);
};

/* set by main's --break: the selftest case inverts one assertion so the run
 * proves the bench can go red (the handoff's negative control). */
extern int kpi_break;

int kpi_fail(const char *file, int line, const char *what, const char *detail);
int kpi_skip(const char *file, int line, const char *reason);

#define KPI_ASSERT(cond, detail)					\
	do {								\
		if (!(cond))						\
			return kpi_fail(__FILE__, __LINE__, #cond, detail); \
	} while (0)

#define KPI_ASSERT_EQ(got, want, detail)				\
	do {								\
		long long __g = (long long)(got);			\
		long long __w = (long long)(want);			\
		if (__g != __w) {					\
			char __b[96];					\
			snprintf(__b, sizeof(__b), "%s got=%lld want=%lld", \
				 detail, __g, __w);			\
			return kpi_fail(__FILE__, __LINE__, #got, __b);	\
		}							\
	} while (0)

#define KPI_SKIP(reason)		return kpi_skip(__FILE__, __LINE__, reason)

uint64_t kpi_now_ms(void);
void kpi_delay_ms(uint32_t ms);

#endif /* KPI_TEST_H */
