/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/errno.h
 * @brief   The include a driver names for the error numbers; the numbers are the
 *          component's.
 *
 * @design declaration-only compatibility face. The interface (include/linux/errno.h,
 *         which pulls the uapi numbers) defines the errno set; the set this component
 *         serves is already stated in the contract header (components/linuxkpi/
 *         linuxkpi_core.h, its error-number section), together with the project's
 *         private ENOTSUPP. A second statement here would risk a redefinition warning
 *         against those exact definitions, so this header satisfies the include by
 *         name only. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_ERRNO_H_
#define _LINUXKPI_LINUX_ERRNO_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_ERRNO_H_ */
