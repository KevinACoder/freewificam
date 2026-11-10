/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   linux80211.h
 * @brief  The component's entry header: what an image calls to bring the
 *         kernel-API layer and the bus engines up, and the version it is
 *         linking.
 *
 * The Linux face (include/linux/... and include/net/...) is the interface the
 * stack and the drivers compile against; this header is only the component's
 * own small surface.  The OS, bus and network seams are the headers beside
 * this one: linux80211/net.h (egress, draft), linux80211/lkpi_*_backend.h
 * (the bus backends), and the CMSIS-RTOS2 facade the OS port supplies.
 */

#ifndef LINUX80211_H
#define LINUX80211_H

#include <stddef.h>
#include <stdint.h>

#define LINUX80211_VERSION_MAJOR 0
#define LINUX80211_VERSION_MINOR 1

/* The kernel-API layer's one-time bring-up (system workqueue, allocator
 * bookkeeping).  Idempotent; every engine entry assumes it ran. */
void lkpi_core_init(void);

/* "0.1" - the component version, compiled in. */
const char *linux80211_version(void);

#endif /* LINUX80211_H */
