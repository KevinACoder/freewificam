/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/delay.h
 * @brief   The include a driver names for the busy/sleep delays; the delays are the
 *          component's.
 *
 * @design declaration-only compatibility face. The interface (include/linux/delay.h)
 *         declares udelay/mdelay/msleep/usleep_range and the fsleep chooser; this
 *         component already states all of them in the contract header
 *         (components/linuxkpi/linuxkpi_core.h, its time section), with the ports behind
 *         them, and `msleep` there is a macro - restating the macro here would be a
 *         redefinition warning, restating the functions a second mechanism. The header
 *         therefore satisfies the include by name only. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_DELAY_H_
#define _LINUXKPI_LINUX_DELAY_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_DELAY_H_ */
