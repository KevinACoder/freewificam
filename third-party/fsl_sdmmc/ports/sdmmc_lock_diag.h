/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   sdmmc_lock_diag.h
 * @brief  Owner/latency snapshot of the fsl_sdmmc OSA mutexes.
 *
 * Plain C only: the SDIO observation case lives outside the fsl_sdmmc world.
 * The tracker is compiled in every image but costs one branch per lock; the
 * snapshot is read only by WLAN_OBS.
 */

#ifndef SDMMC_LOCK_DIAG_H
#define SDMMC_LOCK_DIAG_H

struct sdmmc_lock_diag {
	const char	*owner;		/* "-" free, "?" unknown */
	unsigned int	held_ms;
	unsigned int	waiters;	/* callers currently blocked/acquiring */
	unsigned int	valid;
};

void sdmmc_osa_lock_diag_get(struct sdmmc_lock_diag *out);
/* one mutex, by handle: the card lock's owner/waiters without the ambiguity of
 * "the last slot that has an owner" */
void sdmmc_osa_lock_diag_get_one(void *handle, struct sdmmc_lock_diag *out);
/* the fsl card service's own lock (the one every transfer takes) */
void lkpi_sdio_fsl_card_lock_get(struct sdmmc_lock_diag *out);

#endif /* SDMMC_LOCK_DIAG_H */
