/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/refcount_types.h
 * @brief   The include that names refcount_t; the type is the contract
 *          header's.
 *
 * @design declaration-only compatibility face.  The interface keeps the
 *         reference-count record in this header and its operations in
 *         linux/refcount.h; this component keeps both in linuxkpi_core.h,
 *         which every file of the face includes.  The two record shapes differ
 *         (the core's is { int refs; }, the interface's wraps atomic_t), and
 *         only one may exist in a translation unit - so this header carries no
 *         type of its own and the contract header's is the winner.  The stack
 *         includes it through the real cfg80211/mac80211 headers but never
 *         reads the record's layout.
 * @date    10.11.2026
 */

#ifndef _LINUXKPI_LINUX_REFCOUNT_TYPES_H_
#define _LINUXKPI_LINUX_REFCOUNT_TYPES_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_REFCOUNT_TYPES_H_ */
