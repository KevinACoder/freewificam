/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/workqueue.h
 * @brief   The workqueue surface under the name a driver includes it.
 *
 * @design declaration-only compatibility face, and a pure forward: the work and
 *         delayed-work records, their initializers and the queue operations already live
 *         in the contract header (components/linuxkpi/linuxkpi_core.h, whose workqueue
 *         section is the single definition of them - the imported rtl8188eu generation
 *         of this face put them there). The interface splits the same names across
 *         include/linux/workqueue.h and include/linux/workqueue_types.h; this face keeps
 *         one header and states nothing twice, so a unit that includes the interface's
 *         name reaches the one mechanism. A name the interface has and the contract
 *         header does not yet carry arrives at this header, next to that section's own
 *         comment, rather than as a second definition here.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_WORKQUEUE_H_
#define _LINUXKPI_LINUX_WORKQUEUE_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_WORKQUEUE_H_ */
