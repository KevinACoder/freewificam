/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/refcount.h
 * @brief   The include the interface keeps its reference count in; the
 *          record and operations are the component's.
 *
 * @design declaration-only compatibility face.  The interface's refcount.h is
 *         the atomic reference count (struct refcount_struct, refcount_set/
 *         inc/dec/..., the saturating debug paths).  This component's contract
 *         header already owns the record and the operations its compiled
 *         consumers use (linuxkpi_core.h, the refcount_t section); restating
 *         them here would be the second spelling of one mechanism, and a second
 *         record definition is a hard conflict.  The real net/cfg80211.h and
 *         net/mac80211.h include this header by name and do not call any of the
 *         operations the core lacks, so the contract header answers.
 * @date    10.11.2026
 */

#ifndef _LINUXKPI_LINUX_REFCOUNT_H_
#define _LINUXKPI_LINUX_REFCOUNT_H_

#include "../../linuxkpi_core.h"
#include <linux/refcount_types.h>

#endif /* _LINUXKPI_LINUX_REFCOUNT_H_ */
