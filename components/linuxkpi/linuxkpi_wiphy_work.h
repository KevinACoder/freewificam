/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_wiphy_work.h
 * @brief   The wiphy work engine's own evidence beside the interface it serves.
 *
 * @design the entry points themselves are declared by the interface face
 *         (include/net/cfg80211.h, which the driver includes); this header adds
 *         only what a test observes - the count of works the queue refused
 *         because they were never initialized.  Reference for the shape:
 *         components/linuxkpi/fw_registry.h:1-20.
 * @date    10.10.2026
 */

#ifndef _LINUXKPI_WIPHY_WORK_H_
#define _LINUXKPI_WIPHY_WORK_H_

#include "linuxkpi_core.h"

/* how many works reached the queue without an initializer (func == NULL) and
 * were refused instead of being called through a NULL pointer */
u32	lkpi_wiphy_work_uninit_refusals(void);

#endif /* _LINUXKPI_WIPHY_WORK_H_ */
