/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    diag.h
 * @brief   The A4 transmit-stall round's observation face: NAPI scheduling
 *          health and the device-MMIO breadcrumbs.
 *
 * These are counters, not policy.  The 2026-11-12 AP round's stall left the
 * host with a frozen interrupt count while the firmware kept beaconing, so the
 * first question is who stopped polling whom.  The NAPI numbers answer whether
 * a poll handed the budget back without a re-arm; the MMIO breadcrumbs answer
 * which CSR access the last context made, and the interrupt half's own
 * started/done pair (linuxkpi_pci.h) answers whether that half returned at all.
 *
 * Nothing here changes a driver's behaviour: the counters are incremented at
 * the existing seam and read from a task at a census point.
 */

#ifndef LINUX80211_DIAG_H
#define LINUX80211_DIAG_H

#include <stdint.h>

/* One NAPI instance's scheduling history.  `budget_hits` counts polls that
 * returned at least the instance's weight: iwlwifi's receive poll re-enables
 * the device interrupt only when it returns less than the budget, so a hit
 * that is not followed by another poll leaves CSR_INT_MASK at zero. */
struct lkpi_napi_health {
	unsigned long	schedules;
	unsigned long	polls;
	unsigned long	budget_hits;
	unsigned long	completes;
	unsigned long	last_ret;
	unsigned long	last_weight;
	unsigned long	rounds_exhausted;	/* the re-poll cap was reached */
	unsigned long	last_schedule_jiffies;
	unsigned long	last_complete_jiffies;
};

void lkpi_napi_health_get(struct lkpi_napi_health *out);

/* The CSR offsets the breadcrumbs classify (the AC 7260's register map, the
 * same numbers the board adapter's interrupt trace reads). */
#define LKPI_MMIO_CSR_INT		0x008u
#define LKPI_MMIO_CSR_INT_MASK		0x00cu
#define LKPI_MMIO_CSR_FH_INT_STATUS	0x010u
#define LKPI_MMIO_HBUS_TARG_WRPTR	0x460u

#define LKPI_MMIO_RING_MAX	4

struct lkpi_mmio_op {
	unsigned long	tick;
	uint32_t	ofs;
	uint32_t	val;
	uint32_t	ctx;		/* lkpi_napi_poll_depth at the access */
	const void	*caller;
	uint8_t		write;
};

struct lkpi_mmio_health {
	unsigned long	mask_writes;
	unsigned long	mask_zero_writes;
	unsigned long	ack_writes;
	unsigned long	doorbell_writes;
	unsigned long	int_reads;
	unsigned long	fh_reads;
	unsigned long	int_enable_calls;
	unsigned long	int_disable_calls;
	struct lkpi_mmio_op ring[LKPI_MMIO_RING_MAX];
	unsigned int	ring_next;
	unsigned int	ring_count;
};

void lkpi_mmio_health_get(struct lkpi_mmio_health *out);

#endif /* LINUX80211_DIAG_H */
