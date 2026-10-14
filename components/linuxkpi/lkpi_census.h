/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    lkpi_census.h
 * @brief   The throughput census: the accounting block every instrumented layer
 *          reports through, and the time seam it timestamps with.
 *
 * @design statistics counter block - the shape lkpi_sdio_stats established
 *         (components/linuxkpi/sdio/linuxkpi_sdio.h:30): plain counters a reader
 *         snapshots, plus bounded histograms for the two questions counters
 *         alone cannot answer (how the work clustered, where the time went).
 *         Every module keeps its own census state private and exposes only a
 *         reset and a print; this header names those faces so the image's
 *         snapshot command can call them all from one place.
 *
 *         The time seam: the kernel tick (1 kHz) cannot resolve a single
 *         transfer, so the census timestamps through lkpi_census_now_ns(),
 *         whose implementation lives with the image that defines
 *         CONFIG_LKPI_CENSUS (the board side, on the architectural counter) -
 *         the portable layers themselves carry no aarch64 register access.
 *
 *         The whole census compiles only under CONFIG_LKPI_CENSUS, which the
 *         rtl8189 image defines; every other image and every host suite links
 *         none of it.
 * @date    14.10.2026
 */

#ifndef LKPI_CENSUS_H
#define LKPI_CENSUS_H

#include <stdint.h>

#ifdef CONFIG_LKPI_CENSUS

/*
 * A histogram over powers of two: bucket i counts samples in [2^i, 2^(i+1)) ns,
 * so the print's bucket index is the sample's floor(log2(ns)) - index 10 is a
 * microsecond, 20 a millisecond, 23 about 8 ms. The range covers 1 ns to
 * 134 ms; anything above lands in overflow.
 */
#define LKPI_CENSUS_BUCKETS	28

/* how many caller sites the per-primitive tables keep (top by count, the
 * least-used entry evicted) */
#define LKPI_CENSUS_SITES	8

struct lkpi_census_histo {
	uint32_t	count;
	uint32_t	overflow;
	uint64_t	total_ns;
	uint64_t	max_ns;
	uint32_t	bucket[LKPI_CENSUS_BUCKETS];
};

struct lkpi_census_site {
	const void	*where;		/* the caller's return address */
	uint32_t	count;
	uint64_t	total_ns;
};

struct lkpi_census_sites {
	uint32_t	total_count;
	uint64_t	total_ns;
	struct lkpi_census_site	site[LKPI_CENSUS_SITES];
};

/* the image's clock: nanoseconds from a free-running architectural counter.
 * Implemented where the counter is reachable (the board side of the image that
 * defines CONFIG_LKPI_CENSUS). */
uint64_t	lkpi_census_now_ns(void);

void	lkpi_census_histo_reset(struct lkpi_census_histo *h);
void	lkpi_census_histo_add(struct lkpi_census_histo *h, uint64_t delta_ns);
void	lkpi_census_histo_print(const char *tag, const struct lkpi_census_histo *h);

void	lkpi_census_sites_reset(struct lkpi_census_sites *s);
void	lkpi_census_sites_add(struct lkpi_census_sites *s, const void *where,
	    uint64_t delta_ns);
void	lkpi_census_sites_print(const char *tag, const struct lkpi_census_sites *s);

/* the modules' census faces (each module owns its counters; reset zeroes them
 * for a fresh phase, print emits its `perf census:` lines) */
void	lkpi_sdio_census_reset(void);
void	lkpi_sdio_census_print(void);
void	lkpi_core_census_reset(void);
void	lkpi_core_census_print(void);
void	lkpi_osdep_census_reset(void);
void	lkpi_osdep_census_print(void);
void	lkpi_fsl_census_reset(void);
void	lkpi_fsl_census_print(void);

#endif /* CONFIG_LKPI_CENSUS */

#endif /* LKPI_CENSUS_H */
