/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/rcupdate.h
 * @brief   The include a driver names for the RCU pointer vocabulary; the model is
 *          the component's.
 *
 * @design declaration-only compatibility face. The interface (include/linux/
 *         rcupdate.h) owns the whole read-copy-update surface: the read-side
 *         section, the pointer accessors (rcu_dereference and the checked and
 *         protected variants, RCU_INIT_POINTER, rcu_assign_pointer), the
 *         synchronize/callback pair and the rcu_head record. In this component they
 *         already live in the core contract
 *         (components/linuxkpi/linuxkpi_core.h, its kref/rcu section), which every
 *         file of this face includes; this port runs one CPU with no grace period,
 *         so the model there is a plain store/load pair and the synchronize is a
 *         no-op. Restating them here would be the second spelling of one mechanism
 *         the project forbids, so this header carries no declarations of its own: it
 *         exists because the vendored bytes include it by name, and it is the
 *         contract header that answers. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_RCUPDATE_H_
#define _LINUXKPI_LINUX_RCUPDATE_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_RCUPDATE_H_ */
