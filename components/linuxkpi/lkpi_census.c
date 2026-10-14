/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    lkpi_census.c
 * @brief   The census helpers' bodies: the power-of-two histogram, the caller-
 *          site table and their print forms.
 *
 * @design statistics counter block + bounded histogram (see lkpi_census.h).
 *         The print forms are one line each so a snapshot after a measurement
 *         window stays readable and a single line never outgrows the log's
 *         own line buffer.
 * @date    14.10.2026
 */

#include "linuxkpi_core.h"

#include "lkpi_census.h"

#ifdef CONFIG_LKPI_CENSUS

void
lkpi_census_histo_reset(struct lkpi_census_histo *h)
{

	(void)memset(h, 0, sizeof(*h));
}

void
lkpi_census_histo_add(struct lkpi_census_histo *h, uint64_t delta_ns)
{
	unsigned int idx = 0u;
	uint64_t v = delta_ns;

	while ((v >>= 1) != 0u)
		idx++;
	h->count++;
	h->total_ns += delta_ns;
	if (delta_ns > h->max_ns)
		h->max_ns = delta_ns;
	if (idx >= (unsigned int)LKPI_CENSUS_BUCKETS)
		h->overflow++;
	else
		h->bucket[idx]++;
}

void
lkpi_census_histo_print(const char *tag, const struct lkpi_census_histo *h)
{
	char hot[96];
	size_t len = 0;
	unsigned int i;
	unsigned int shown = 0;
	unsigned int skipped = 0;
	uint64_t avg_us = h->count != 0u ? (h->total_ns / h->count) / 1000u : 0u;

	/* the busiest buckets are what the shape question turns on ("is the mode
	 * about a millisecond?"), so six of them plus a count of the rest beat a
	 * dump of all twenty-eight */
	hot[0] = '\0';
	for (i = 0u; i < (unsigned int)LKPI_CENSUS_BUCKETS; i++) {
		int wrote;

		if (h->bucket[i] == 0u)
			continue;
		if (shown >= 6u) {
			skipped++;
			continue;
		}
		if (len != 0u)
			hot[len++] = ' ';
		wrote = snprintf(&hot[len], sizeof(hot) - len, "e%u:%u", i,
		    h->bucket[i]);
		if (wrote < 0 || (size_t)wrote >= sizeof(hot) - len)
			break;
		len += (size_t)wrote;
		shown++;
	}
	if (skipped != 0u)
		(void)snprintf(&hot[len], sizeof(hot) - len, "%s+%u",
		    len != 0u ? " " : "", skipped);
	lkpi_printf(ULOG_INFO_LEVEL,
	    "%s n=%u avg_us=%llu max_us=%llu hot=[%s] over=%u\n", tag,
	    h->count, avg_us, h->max_ns / 1000u, hot, h->overflow);
}

void
lkpi_census_sites_reset(struct lkpi_census_sites *s)
{

	(void)memset(s, 0, sizeof(*s));
}

void
lkpi_census_sites_add(struct lkpi_census_sites *s, const void *where,
    uint64_t delta_ns)
{
	unsigned int i;
	int victim = -1;

	s->total_count++;
	s->total_ns += delta_ns;
	for (i = 0u; i < (unsigned int)LKPI_CENSUS_SITES; i++) {
		if (s->site[i].where == where) {
			s->site[i].count++;
			s->site[i].total_ns += delta_ns;
			return;
		}
		if (s->site[i].where == NULL && victim < 0)
			victim = (int)i;
	}
	if (victim < 0) {
		/* evict the least-used caller: the table's point is the repeated one */
		victim = 0;
		for (i = 1u; i < (unsigned int)LKPI_CENSUS_SITES; i++) {
			if (s->site[i].count < s->site[victim].count)
				victim = (int)i;
		}
	}
	s->site[victim].where = where;
	s->site[victim].count = 1u;
	s->site[victim].total_ns = delta_ns;
}

void
lkpi_census_sites_print(const char *tag, const struct lkpi_census_sites *s)
{
	char sites[176];
	size_t len = 0;
	unsigned int i;

	for (i = 0u; i < (unsigned int)LKPI_CENSUS_SITES; i++) {
		int wrote;

		if (s->site[i].where == NULL)
			continue;
		wrote = snprintf(&sites[len], sizeof(sites) - len, "%s%lx:%u:%llu",
		    len != 0u ? " " : "", (unsigned long)s->site[i].where,
		    s->site[i].count, s->site[i].total_ns / 1000u);
		if (wrote < 0 || (size_t)wrote >= sizeof(sites) - len)
			break;
		len += (size_t)wrote;
	}
	sites[len] = '\0';
	lkpi_printf(ULOG_INFO_LEVEL, "%s n=%u spent_us=%llu sites=[%s]\n", tag,
	    s->total_count, s->total_ns / 1000u, sites);
}

/* ------------------------------------------------------------------ */
/* the transmit-request stamp                                          */
/* ------------------------------------------------------------------ */

/*
 * One stamp, the oldest transmit request still unwritten: a frame entering the
 * driver's transmit entry notes its time here, and the transfer path that writes
 * it to the card takes the stamp and adds the difference to its histogram. A
 * request that is never written (a dropped frame) ages until the next write
 * claims it - rare enough to be one outlier sample, and the write path's own
 * counters show whether drops happen at all.
 */
static uint64_t lkpi_census_tx_request_ns;

void
lkpi_census_tx_request_note(void)
{

	if (lkpi_census_tx_request_ns == 0u)
		lkpi_census_tx_request_ns = lkpi_census_now_ns();
}

uint64_t
lkpi_census_tx_request_take(void)
{
	uint64_t t = lkpi_census_tx_request_ns;

	lkpi_census_tx_request_ns = 0u;
	return t;
}

#endif /* CONFIG_LKPI_CENSUS */
